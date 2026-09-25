// Sharing a game: a web version that runs in any browser, a game card picture to post,
// a zip ready for itch.io, and a small web server so anyone on the same Wi-Fi can play
// from a link or by scanning the card's QR code.
//
// Aven doesn't host games on the internet. The web build plus zip is what a free host
// (itch.io, GitHub Pages...) needs to give the game a public link.

#include "editor.h"

#include "aven/core/embedded.h"
#include "aven/core/fs.h"
#include "aven/render/scene_renderer.h"

#include <imgui.h>
#include <imgui_stdlib.h>
#include <qrcodegen.hpp>
#include <stb_image.h>
#include <stb_truetype.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
#define AVEN_CLOSE_SOCKET closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
#define INVALID_SOCKET (-1)
#define AVEN_CLOSE_SOCKET ::close
#endif

namespace aven::editor {

namespace {

// ---------------------------------------------------------------- a small image with text drawing

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    Image(int width, int height) : w(width), h(height), px(static_cast<size_t>(width) * height * 4, 0) {}
    void blend(int x, int y, Color c, float a) {
        if (x < 0 || y < 0 || x >= w || y >= h || a <= 0)
            return;
        a = std::min(1.0f, a * c.a);
        uint8_t* p = &px[(static_cast<size_t>(y) * w + x) * 4];
        p[0] = static_cast<uint8_t>(p[0] + (c.r * 255 - p[0]) * a);
        p[1] = static_cast<uint8_t>(p[1] + (c.g * 255 - p[1]) * a);
        p[2] = static_cast<uint8_t>(p[2] + (c.b * 255 - p[2]) * a);
        p[3] = static_cast<uint8_t>(std::min(255.0f, p[3] + (255 - p[3]) * a));
    }
    void fill(int x0, int y0, int x1, int y1, Color c) {
        for (int y = std::max(0, y0); y < std::min(h, y1); ++y)
            for (int x = std::max(0, x0); x < std::min(w, x1); ++x)
                blend(x, y, c, 1);
    }
    void roundRect(int x0, int y0, int x1, int y1, float r, Color c) {
        for (int y = std::max(0, y0); y < std::min(h, y1); ++y)
            for (int x = std::max(0, x0); x < std::min(w, x1); ++x) {
                float dx = std::max({static_cast<float>(x0) + r - x - 0.5f, 0.0f, x + 0.5f - (static_cast<float>(x1) - r)});
                float dy = std::max({static_cast<float>(y0) + r - y - 0.5f, 0.0f, y + 0.5f - (static_cast<float>(y1) - r)});
                float d = std::sqrt(dx * dx + dy * dy) - r;
                blend(x, y, c, std::clamp(0.5f - d, 0.0f, 1.0f));
            }
    }
    void image(const uint8_t* src, int sw, int sh, int x0, int y0, int dw, int dh) {
        for (int y = 0; y < dh; ++y)
            for (int x = 0; x < dw; ++x) {
                const uint8_t* s = &src[(static_cast<size_t>(y * sh / dh) * sw + x * sw / dw) * 4];
                blend(x0 + x, y0 + y, Color(s[0] / 255.0f, s[1] / 255.0f, s[2] / 255.0f), s[3] / 255.0f);
            }
    }
};

std::u32string utf8(const std::string& s) {
    std::u32string out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp = c;
        int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        if (extra)
            cp = c & (0x3F >> extra);
        for (int k = 1; k <= extra && i + static_cast<size_t>(k) < s.size(); ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + static_cast<size_t>(k)]) & 0x3F);
        out += cp;
        i += static_cast<size_t>(extra) + 1;
    }
    return out;
}

class CardFont {
public:
    CardFont() {
        std::size_t size = 0;
        data_ = embedded::find("Roboto-Medium.ttf", &size);
        ok_ = data_ && stbtt_InitFont(&info_, data_, stbtt_GetFontOffsetForIndex(data_, 0));
    }
    float width(const std::string& text, float px) {
        if (!ok_)
            return 0;
        float scale = stbtt_ScaleForPixelHeight(&info_, px), x = 0;
        auto cps = utf8(text);
        for (size_t i = 0; i < cps.size(); ++i) {
            int advance, lsb;
            stbtt_GetCodepointHMetrics(&info_, static_cast<int>(cps[i]), &advance, &lsb);
            x += advance * scale;
            if (i + 1 < cps.size())
                x += scale * stbtt_GetCodepointKernAdvance(&info_, static_cast<int>(cps[i]), static_cast<int>(cps[i + 1]));
        }
        return x;
    }
    // Draws one line with its top at `y`; returns the width.
    float draw(Image& img, float x, float y, float px, const std::string& text, Color color) {
        if (!ok_)
            return 0;
        float scale = stbtt_ScaleForPixelHeight(&info_, px);
        int ascent, descent, gap;
        stbtt_GetFontVMetrics(&info_, &ascent, &descent, &gap);
        float baseline = y + ascent * scale;
        float start = x;
        auto cps = utf8(text);
        for (size_t i = 0; i < cps.size(); ++i) {
            int cp = static_cast<int>(cps[i]);
            int advance, lsb;
            stbtt_GetCodepointHMetrics(&info_, cp, &advance, &lsb);
            int x0, y0, x1, y1;
            float fx = x - std::floor(x);
            stbtt_GetCodepointBitmapBoxSubpixel(&info_, cp, scale, scale, fx, 0, &x0, &y0, &x1, &y1);
            int gw = x1 - x0, gh = y1 - y0;
            if (gw > 0 && gh > 0) {
                std::vector<unsigned char> glyph(static_cast<size_t>(gw) * gh);
                stbtt_MakeCodepointBitmapSubpixel(&info_, glyph.data(), gw, gh, gw, scale, scale, fx, 0, cp);
                int ox = static_cast<int>(std::floor(x)) + x0, oy = static_cast<int>(std::round(baseline)) + y0;
                for (int gy = 0; gy < gh; ++gy)
                    for (int gx = 0; gx < gw; ++gx)
                        img.blend(ox + gx, oy + gy, color, glyph[static_cast<size_t>(gy) * gw + gx] / 255.0f);
            }
            x += advance * scale;
            if (i + 1 < cps.size())
                x += scale * stbtt_GetCodepointKernAdvance(&info_, cp, static_cast<int>(cps[i + 1]));
        }
        return x - start;
    }
    // Word-wrapped text; returns the lines used.
    int paragraph(Image& img, float x, float y, float px, float maxWidth, int maxLines, const std::string& text, Color color) {
        std::vector<std::string> words;
        std::string cur;
        for (char c : text + " ") {
            if (c == ' ' || c == '\n') {
                if (!cur.empty())
                    words.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
        std::string line;
        int lines = 0;
        for (size_t i = 0; i <= words.size() && lines < maxLines; ++i) {
            std::string next = i < words.size() ? (line.empty() ? words[i] : line + " " + words[i]) : "";
            if (i == words.size() || (width(next, px) > maxWidth && !line.empty())) {
                if (lines == maxLines - 1 && i < words.size())
                    line += "...";
                draw(img, x, y + lines * px * 1.25f, px, line, color);
                ++lines;
                line = i < words.size() ? words[i] : "";
            } else {
                line = next;
            }
        }
        return lines;
    }

private:
    stbtt_fontinfo info_{};
    const unsigned char* data_ = nullptr;
    bool ok_ = false;
};

// ---------------------------------------------------------------- zip (stored, no compression)

uint32_t crc32(const std::string& data) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char b : data)
        crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void put16(std::string& s, uint16_t v) {
    s += static_cast<char>(v & 0xFF);
    s += static_cast<char>(v >> 8);
}
void put32(std::string& s, uint32_t v) {
    put16(s, static_cast<uint16_t>(v & 0xFFFF));
    put16(s, static_cast<uint16_t>(v >> 16));
}

bool writeZip(const stdfs::path& zipPath, const stdfs::path& folder) {
    std::string out, central;
    uint16_t count = 0;
    std::error_code ec;
    for (auto it = stdfs::recursive_directory_iterator(folder, ec); !ec && it != stdfs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file())
            continue;
        std::string name = stdfs::relative(it->path(), folder, ec).generic_string();
        auto data = fs::readBinary(it->path());
        if (!data)
            return false;
        std::string bytes(reinterpret_cast<const char*>(data->data()), data->size());
        uint32_t crc = crc32(bytes), size = static_cast<uint32_t>(bytes.size()), offset = static_cast<uint32_t>(out.size());
        std::string header;
        put32(header, 0x04034b50);
        put16(header, 20);
        put16(header, 0x0800); // UTF-8 names
        put16(header, 0);      // stored
        put16(header, 0);
        put16(header, 0x21);   // 1980-01-01
        put32(header, crc);
        put32(header, size);
        put32(header, size);
        put16(header, static_cast<uint16_t>(name.size()));
        put16(header, 0);
        out += header + name + bytes;
        put32(central, 0x02014b50);
        put16(central, 20);
        put16(central, 20);
        put16(central, 0x0800);
        put16(central, 0);
        put16(central, 0);
        put16(central, 0x21);
        put32(central, crc);
        put32(central, size);
        put32(central, size);
        put16(central, static_cast<uint16_t>(name.size()));
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put32(central, 0);
        put32(central, offset);
        central += name;
        ++count;
    }
    uint32_t centralOffset = static_cast<uint32_t>(out.size());
    out += central;
    put32(out, 0x06054b50);
    put16(out, 0);
    put16(out, 0);
    put16(out, count);
    put16(out, count);
    put32(out, static_cast<uint32_t>(central.size()));
    put32(out, centralOffset);
    put16(out, 0);
    return fs::writeText(zipPath, out);
}

std::string mimeType(const std::string& ext) {
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".js") return "text/javascript";
    if (ext == ".wasm") return "application/wasm";
    if (ext == ".json" || ext == ".scene" || ext == ".prefab" || ext == ".aven" || ext == ".blocks") return "application/json";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".wav") return "audio/wav";
    if (ext == ".ogg") return "audio/ogg";
    if (ext == ".mp3") return "audio/mpeg";
    if (ext == ".css") return "text/css";
    return "application/octet-stream";
}

// The computer's address on the local network (how other devices on the Wi-Fi reach it).
std::string lanAddress() {
    socket_t s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET)
        return "127.0.0.1";
    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(53);
    inet_pton(AF_INET, "10.254.254.254", &remote.sin_addr); // no packet is sent; this just picks the route
    std::string ip = "127.0.0.1";
    if (::connect(s, reinterpret_cast<sockaddr*>(&remote), sizeof remote) == 0) {
        sockaddr_in local{};
        socklen_t len = sizeof local;
        if (::getsockname(s, reinterpret_cast<sockaddr*>(&local), &len) == 0) {
            char buf[INET_ADDRSTRLEN] = {};
            if (inet_ntop(AF_INET, &local.sin_addr, buf, sizeof buf))
                ip = buf;
        }
    }
    AVEN_CLOSE_SOCKET(s);
    return ip;
}


} // namespace

void openExternal(const std::string& target) {
#ifdef _WIN32
    std::string cmd = "start \"\" \"" + target + "\"";
#elif defined(__APPLE__)
    std::string cmd = "open \"" + target + "\"";
#else
    std::string cmd = "xdg-open \"" + target + "\" >/dev/null 2>&1 &";
#endif
    [[maybe_unused]] int r = std::system(cmd.c_str());
}

// ---------------------------------------------------------------- the share server

class ShareServer {
public:
    ~ShareServer() { stop(); }

    bool start(const stdfs::path& root, std::string& error) {
#ifdef _WIN32
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
        root_ = root;
        listen_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_ == INVALID_SOCKET) {
            error = "Couldn't open a network connection.";
            return false;
        }
        int yes = 1;
        setsockopt(listen_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof yes);
        for (int port = 8080; port < 8100; ++port) {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
            addr.sin_port = htons(static_cast<uint16_t>(port));
            if (::bind(listen_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0) {
                port_ = port;
                break;
            }
        }
        if (!port_ || ::listen(listen_, 16) != 0) {
            AVEN_CLOSE_SOCKET(listen_);
            listen_ = INVALID_SOCKET;
            error = "Couldn't find a free network port (8080-8099).";
            return false;
        }
        running_ = true;
        thread_ = std::thread([this] { loop(); });
        return true;
    }

    void stop() {
        if (!running_)
            return;
        running_ = false;
        if (thread_.joinable())
            thread_.join();
        AVEN_CLOSE_SOCKET(listen_);
        listen_ = INVALID_SOCKET;
    }

    bool running() const { return running_; }
    int port() const { return port_; }
    int requests() const { return requests_; }

private:
    void loop() {
        while (running_) {
#ifdef _WIN32
            fd_set set;
            FD_ZERO(&set);
            FD_SET(listen_, &set);
            timeval tv{0, 200000};
            if (select(0, &set, nullptr, nullptr, &tv) <= 0)
                continue;
#else
            pollfd p{listen_, POLLIN, 0};
            if (poll(&p, 1, 200) <= 0)
                continue;
#endif
            socket_t client = ::accept(listen_, nullptr, nullptr);
            if (client == INVALID_SOCKET)
                continue;
            handle(client);
            AVEN_CLOSE_SOCKET(client);
        }
    }

    void sendAll(socket_t c, const std::string& data) {
        size_t sent = 0;
        while (sent < data.size()) {
            auto n = ::send(c, data.data() + sent, static_cast<int>(std::min<size_t>(data.size() - sent, 1 << 16)), 0);
            if (n <= 0)
                return;
            sent += static_cast<size_t>(n);
        }
    }

    void handle(socket_t c) {
        std::string request;
        char buf[4096];
        while (request.find("\r\n\r\n") == std::string::npos && request.size() < 16384) {
            auto n = ::recv(c, buf, sizeof buf, 0);
            if (n <= 0)
                break;
            request.append(buf, static_cast<size_t>(n));
        }
        ++requests_;
        size_t sp1 = request.find(' '), sp2 = request.find(' ', sp1 + 1);
        if (request.compare(0, 4, "GET ") != 0 || sp2 == std::string::npos) {
            sendAll(c, "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            return;
        }
        std::string path = request.substr(sp1 + 1, sp2 - sp1 - 1);
        path = path.substr(0, path.find('?'));
        std::string decoded;
        for (size_t i = 0; i < path.size(); ++i) {
            if (path[i] == '%' && i + 2 < path.size()) {
                decoded += static_cast<char>(std::strtol(path.substr(i + 1, 2).c_str(), nullptr, 16));
                i += 2;
            } else {
                decoded += path[i];
            }
        }
        if (decoded.empty() || decoded.back() == '/')
            decoded += "index.html";
        if (decoded.find("..") != std::string::npos) {
            sendAll(c, "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            return;
        }
        auto data = fs::readBinary(root_ / decoded.substr(1));
        if (!data) {
            std::string body = "Not found";
            sendAll(c, "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(body.size()) +
                           "\r\nConnection: close\r\n\r\n" + body);
            return;
        }
        std::string header = "HTTP/1.1 200 OK\r\nContent-Type: " + mimeType(stdfs::path(decoded).extension().string()) +
                             "\r\nContent-Length: " + std::to_string(data->size()) +
                             "\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n";
        sendAll(c, header + std::string(reinterpret_cast<const char*>(data->data()), data->size()));
    }

    stdfs::path root_;
    socket_t listen_ = INVALID_SOCKET;
    int port_ = 0;
    std::atomic<bool> running_{false};
    std::atomic<int> requests_{0};
    std::thread thread_;
};

void ShareServerDeleter::operator()(ShareServer* s) const { delete s; }

// ---------------------------------------------------------------- exports

std::string Editor::safeGameName() const {
    std::string safeName;
    for (char c : settings_.name)
        safeName += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : (c == ' ' ? '-' : '_');
    return safeName.empty() ? "Game" : safeName;
}

void Editor::copyGameFiles(const stdfs::path& to, const stdfs::path& skip, std::vector<std::string>* list) {
    std::error_code ec;
    stdfs::create_directories(to, ec);
    for (auto it = stdfs::recursive_directory_iterator(projectDir_, ec); it != stdfs::recursive_directory_iterator(); it.increment(ec)) {
        stdfs::path rel = stdfs::relative(it->path(), projectDir_, ec);
        std::string first = rel.begin()->string();
        // Editor-only files and earlier exports stay behind.
        if (first == "exports" || first == "bug_reports" || first == "recipes" || (!first.empty() && first[0] == '.') ||
            rel == "tutorial.json" || (!skip.empty() && stdfs::equivalent(it->path(), skip, ec))) {
            if (it->is_directory())
                it.disable_recursion_pending();
            continue;
        }
        if (it->is_directory()) {
            stdfs::create_directories(to / rel, ec);
        } else {
            stdfs::copy_file(it->path(), to / rel, stdfs::copy_options::overwrite_existing, ec);
            if (list)
                list->push_back(rel.generic_string());
        }
    }
}

stdfs::path Editor::webPlayerDir() const {
    for (const stdfs::path& dir : {fs::executableDir() / "web", stdfs::path(AVEN_WEB_PLAYER_DIR)}) {
        std::error_code ec;
        if (stdfs::exists(dir / "aven-player.wasm", ec) && stdfs::exists(dir / "aven-player.js", ec) &&
            stdfs::exists(dir / "index.html", ec))
            return dir;
    }
    return {};
}

// What the controls are, read from the start scene's behaviors and scripts.
std::string Editor::gameControls() {
    Scene s;
    auto text = fs::readText(projectDir_ / settings_.startScene);
    if (!text || !s.load(Json::parse(*text)))
        return "";
    auto& reg = s.registry();
    std::vector<std::string> parts;
    auto add = [&](const std::string& p) {
        if (std::find(parts.begin(), parts.end(), p) == parts.end())
            parts.push_back(p);
    };
    std::set<std::string> keys;
    s.walk([&](Entity e, int) {
        if (reg.has<PlatformerController>(e)) {
            add("Arrow keys or A/D to run");
            add("Space to jump");
        }
        if (reg.has<TopDownController>(e))
            add("Arrow keys or WASD to move");
        if (reg.has<CharacterController>(e)) {
            add("WASD to move");
            add("Space to jump");
        }
        if (auto* sh = reg.tryGet<Shooter>(e))
            add(sh->action.rfind("mouse", 0) == 0 ? "Click to shoot" : sh->action + " to shoot");
        if (reg.has<Clickable>(e) || reg.has<Draggable>(e))
            add("Click and play with the mouse");
        if (auto* sc = reg.tryGet<Script>(e); sc && !sc->path.empty())
            for (auto& k : factsFor(sc->path).keys)
                keys.insert(k);
        return true;
    });
    if (parts.empty() && !keys.empty()) {
        std::string k;
        for (auto& key : keys)
            k += (k.empty() ? "" : ", ") + key;
        add("Keys: " + k);
    }
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i)
        out += (i ? "  ·  " : "") + parts[i];
    return out;
}

std::string Editor::gameDescription() const {
    if (!settings_.description.empty())
        return settings_.description;
    if (!recipeCard_.summary.empty())
        return recipeCard_.summary;
    return "A game made with Aven.";
}

bool Editor::exportWeb(const stdfs::path& folder, std::string& message) {
    if (folder.empty() || folder.is_relative()) {
        message = "Pick an output folder first.";
        return false;
    }
    stdfs::path player = webPlayerDir();
    if (player.empty()) {
        message = "The web player isn't built yet. Run tools/web/build_web_player.sh once (it needs Emscripten), then try again.";
        return false;
    }
    std::error_code ec;
    stdfs::path out = folder / (safeGameName() + "-web");
    if (stdfs::exists(out, ec)) {
        bool previous = stdfs::exists(out / "aven-player.wasm", ec) && stdfs::exists(out / "game" / "files.json", ec);
        bool empty = stdfs::is_directory(out, ec) && stdfs::directory_iterator(out, ec) == stdfs::directory_iterator();
        if (!previous && !empty) {
            message = "There's already a folder called '" + out.filename().string() + "' that isn't an Aven web build. Pick another folder.";
            return false;
        }
        stdfs::remove_all(out, ec);
    }
    stdfs::create_directories(out, ec);
    saveScene();
    saveAllScripts();
    std::vector<std::string> files;
    copyGameFiles(out / "game", folder, &files);
    Json list = Json::object();
    list["files"] = Json::array();
    for (auto& f : files)
        list["files"].push(Json(f));
    fs::writeText(out / "game" / "files.json", list.dump(1));
    for (const char* f : {"aven-player.js", "aven-player.wasm"})
        stdfs::copy_file(player / f, out / f, stdfs::copy_options::overwrite_existing, ec);

    // The page, filled in for this game.
    std::string page = fs::readText(player / "index.html").value_or("");
    auto html = [](std::string s) {
        std::string o;
        for (char c : s)
            o += c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '&' ? "&amp;" : c == '"' ? "&quot;" : std::string(1, c);
        return o;
    };
    Color accent = prefs.accentColor();
    char accentHex[16];
    std::snprintf(accentHex, sizeof accentHex, "#%02x%02x%02x", static_cast<int>(accent.r * 255), static_cast<int>(accent.g * 255),
                  static_cast<int>(accent.b * 255));
    auto replaceAll = [&](const std::string& key, const std::string& value) {
        for (size_t p; (p = page.find(key)) != std::string::npos;)
            page.replace(p, key.size(), value);
    };
    replaceAll("{{TITLE}}", html(settings_.name));
    replaceAll("{{DESCRIPTION}}", html(gameDescription()));
    replaceAll("{{CONTROLS}}", html(gameControls()));
    replaceAll("{{ACCENT}}", accentHex);
    replaceAll("{{ASPECT}}", std::to_string(settings_.width) + " / " + std::to_string(settings_.height));
    fs::writeText(out / "index.html", page);
    // The card doubles as the page's preview picture; the Aven icon as its tab icon.
    std::string cardMessage;
    makeGameCard(out / "card.png", "", cardMessage);
    std::size_t iconSize = 0;
    if (const unsigned char* icon = embedded::find("aven_64.png", &iconSize))
        fs::writeText(out / "icon.png", std::string(reinterpret_cast<const char*>(icon), iconSize));
    fs::writeText(out / "README.txt",
                  settings_.name + " for web browsers.\n\nBrowsers won't run a game opened straight from your disk. To play or share it:\n"
                                   "- In Aven: Build & Export > Share > Share on this Wi-Fi.\n"
                                   "- Online: upload the zip (Build & Export > Web > Make a zip for itch.io) to itch.io as an HTML game.\n"
                                   "- Any web server works: put these files in a folder it serves.\n");
    webExportDir_ = out.string();
    message = "Done! The web version is in:\n" + out.string();
    Log::info("Exported web build to ", out.string());
    milestone("exports");
    return true;
}

bool Editor::makeItchZip(std::string& message) {
    if (exportFolder_.empty())
        exportFolder_ = (projectDir_ / "exports").string();
    if (webExportDir_.empty() && !exportWeb(exportFolder_, message))
        return false;
    stdfs::path zip = stdfs::path(webExportDir_).parent_path() / (safeGameName() + "-web.zip");
    if (!writeZip(zip, webExportDir_)) {
        message = "Couldn't write " + zip.string();
        return false;
    }
    message = "Made " + zip.string() + ". On itch.io: create a new project, set 'Kind of project' to HTML, upload this zip and "
                                       "tick 'This file will be played in the browser'.";
    return true;
}

// ---------------------------------------------------------------- game card

std::vector<uint8_t> Editor::renderStartScene(int w, int h) {
    auto game = makeGame();
    if (!game->loadScene(settings_.startScene))
        return {};
    game->setScreenSize({static_cast<float>(w), static_cast<float>(h)});
    for (int i = 0; i < 20; ++i)
        game->update(1.0f / 60.0f);
    device_.beginFrame();
    auto overlay = std::move(renderer_.sceneOverlay); // no editor grid or gizmos in the picture
    renderer_.sceneOverlay = nullptr;
    game->render(renderer_, w, h);
    renderer_.sceneOverlay = std::move(overlay);
    std::vector<uint8_t> shot = renderer_.readOutput();
    game->stop();
    return shot.size() == static_cast<size_t>(w) * h * 4 ? shot : std::vector<uint8_t>{};
}

bool Editor::makeGameCard(const stdfs::path& png, const std::string& shareUrl, std::string& message) {
    constexpr int W = 1200, H = 630;
    Image card(W, H);
    // A picture of the game a moment after it starts.
    std::vector<uint8_t> shot = renderStartScene(W, H);
    if (shot.empty()) {
        message = "Couldn't load the start scene " + settings_.startScene;
        return false;
    }
    card.px = std::move(shot);
    // A dark band for the text.
    for (int y = H - 300; y < H; ++y) {
        float t = (y - (H - 300)) / 300.0f;
        for (int x = 0; x < W; ++x)
            card.blend(x, y, Color(0.04f, 0.06f, 0.12f), std::min(0.92f, t * 1.6f));
    }
    CardFont font;
    Color accent = prefs.accentColor();
    bool qr = !shareUrl.empty();
    float textW = qr ? W - 96 - 230 : W - 96;
    // "Made with Aven" badge.
    card.roundRect(32, 28, 32 + 230, 28 + 52, 26, Color(0.04f, 0.06f, 0.12f, 0.75f));
    std::size_t iconSize = 0;
    if (const unsigned char* icon = embedded::find("aven_64.png", &iconSize)) {
        int iw, ih, n;
        if (unsigned char* px = stbi_load_from_memory(icon, static_cast<int>(iconSize), &iw, &ih, &n, 4)) {
            card.image(px, iw, ih, 42, 34, 40, 40);
            stbi_image_free(px);
        }
    }
    font.draw(card, 92, 40, 26, "Made with Aven", Color(1, 1, 1));
    // Title, description and controls.
    float y = H - 230;
    font.draw(card, 48, y, 64, settings_.name, Color(1, 1, 1));
    y += 76;
    int lines = font.paragraph(card, 48, y, 28, textW, 2, gameDescription(), Color(0.8f, 0.85f, 0.93f));
    y += lines * 35 + 10;
    std::string controls = gameControls();
    if (!controls.empty())
        font.draw(card, 48, y, 24, controls, Color(accent.r * 0.5f + 0.5f, accent.g * 0.5f + 0.5f, accent.b * 0.5f + 0.5f));
    // QR code to play.
    if (qr) {
        qrcodegen::QrCode code = qrcodegen::QrCode::encodeText(shareUrl.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        int size = code.getSize();
        int box = 200, x0 = W - 48 - box, y0 = H - 48 - box - 26;
        card.roundRect(x0 - 12, y0 - 12, x0 + box + 12, y0 + box + 40, 14, Color(1, 1, 1));
        float cell = static_cast<float>(box) / static_cast<float>(size);
        for (int qy = 0; qy < size; ++qy)
            for (int qx = 0; qx < size; ++qx)
                if (code.getModule(qx, qy))
                    card.fill(x0 + static_cast<int>(qx * cell), y0 + static_cast<int>(qy * cell), x0 + static_cast<int>((qx + 1) * cell),
                              y0 + static_cast<int>((qy + 1) * cell), Color(0.05f, 0.07f, 0.12f));
        std::string label = "Scan to play";
        font.draw(card, x0 + box / 2.0f - font.width(label, 22) / 2, y0 + box + 6, 22, label, Color(0.05f, 0.07f, 0.12f));
    }
    for (size_t i = 3; i < card.px.size(); i += 4)
        card.px[i] = 255;
    std::error_code ec;
    stdfs::create_directories(png.parent_path(), ec);
    if (!Assets::savePng(png, card.px.data(), W, H, false)) {
        message = "Couldn't save " + png.string();
        return false;
    }
    // Show it in the Share tab.
    if (cardTexture_)
        device_.destroy(cardTexture_);
    rhi::TextureDesc desc;
    desc.width = W;
    desc.height = H;
    desc.data = card.px.data();
    desc.label = "game card";
    cardTexture_ = device_.createTexture(desc);
    cardPath_ = png.string();
    message = "Saved the game card: " + png.string();
    return true;
}

// ---------------------------------------------------------------- sharing on the local network

bool Editor::startSharing(std::string& message) {
    if (exportFolder_.empty())
        exportFolder_ = (projectDir_ / "exports").string();
    if (!exportWeb(exportFolder_, message))
        return false;
    stopSharing();
    shareServer_.reset(new ShareServer());
    std::string error;
    if (!shareServer_->start(webExportDir_, error)) {
        shareServer_.reset();
        message = error;
        return false;
    }
    shareUrl_ = "http://" + lanAddress() + ":" + std::to_string(shareServer_->port()) + "/";
    std::string cardMessage;
    makeGameCard(stdfs::path(webExportDir_) / "card.png", shareUrl_, cardMessage);
    message = "Sharing at " + shareUrl_;
    return true;
}

void Editor::stopSharing() {
    if (shareServer_)
        shareServer_->stop();
    shareServer_.reset();
    shareUrl_.clear();
}

// ---------------------------------------------------------------- UI

void Editor::drawExport() {
    ImGui::SetNextWindowSize({640, 560}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    if (!ImGui::Begin("Build & Share###Export", &showExport_)) {
        ImGui::End();
        return;
    }
    if (exportFolder_.empty())
        exportFolder_ = (projectDir_ / "exports").string();
    if (ImGui::BeginTabBar("##exporttabs")) {
        bool desktop = unlocked(Feature::Export);
        if (desktop && ImGui::BeginTabItem("This computer", nullptr, exportTab_ == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
            if (exportTab_ == 0)
                exportTab_ = -1;
            ImGui::TextWrapped("Makes a folder with your game that runs on this kind of computer without Aven. Zip it up and share it!");
            ImGui::Spacing();
            ImGui::InputText("Output folder", &exportFolder_);
            ImGui::TextDisabled("Game name: %s  |  Start scene: %s", settings_.name.c_str(), settings_.startScene.c_str());
            ImGui::Spacing();
            if (ImGui::Button("Export game", {160, 34})) {
                saveScene();
                saveAllScripts();
                exportGame(exportFolder_, exportResult_);
            }
            if (!exportResult_.empty()) {
                ImGui::Spacing();
                ImGui::TextWrapped("%s", exportResult_.c_str());
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Web browser", nullptr, exportTab_ == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
            if (exportTab_ == 1)
                exportTab_ = -1;
            ImGui::TextWrapped("Makes a web version that runs in any modern browser (Chrome, Edge, Firefox, Safari), on computers "
                               "and phones. No install needed.");
            ImGui::Spacing();
            bool ready = !webPlayerDir().empty();
            if (!ready)
                ImGui::TextColored({1, 0.75f, 0.35f, 1}, "The web player isn't built on this computer yet. Build it once with "
                                                          "tools/web/build_web_player.sh (it needs Emscripten).");
            ImGui::InputText("Output folder##web", &exportFolder_);
            ImGui::BeginDisabled(!ready);
            if (ImGui::Button("Build for the web", {180, 34}))
                exportWeb(exportFolder_, webResult_);
            ImGui::SameLine();
            if (ImGui::Button("Make a zip for itch.io", {0, 34}))
                makeItchZip(webResult_);
            ImGui::EndDisabled();
            if (!webExportDir_.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Open the folder", {0, 34}))
                    openExternal(webExportDir_);
            }
            if (!webResult_.empty())
                ImGui::TextWrapped("%s", webResult_.c_str());
            ImGui::Spacing();
            ImGui::TextDisabled("Tip: browsers block games opened straight from a file. Use Share to try it, or upload it.");
            ImGui::EndTabItem();
        }
        if (unlocked(Feature::Share) && ImGui::BeginTabItem("Share", nullptr, exportTab_ == 2 ? ImGuiTabItemFlags_SetSelected : 0)) {
            if (exportTab_ == 2)
                exportTab_ = -1;
            bool ready = !webPlayerDir().empty();
            // Game card.
            ui::sectionHeader("Game card");
            if (cardTexture_) {
                float w = std::min(ImGui::GetContentRegionAvail().x, 560.0f);
                ImGui::Image(static_cast<ImTextureID>(device_.nativeTexture(cardTexture_)), {w, w * 630.0f / 1200.0f});
            } else {
                ImGui::TextDisabled("A picture of your game with its name and controls, ready to post anywhere.");
            }
            if (ImGui::Button("Make a game card")) {
                stdfs::path png = projectDir_ / "exports" / (safeGameName() + "-card.png");
                makeGameCard(png, shareUrl_, shareResult_);
            }
            if (!cardPath_.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Open the picture"))
                    openExternal(cardPath_);
            }
            // Local network.
            ui::sectionHeader("Share on this Wi-Fi");
            if (shareServer_ && shareServer_->running()) {
                ImGui::PushFont(fonts.big);
                ImGui::TextUnformatted(shareUrl_.c_str());
                ImGui::PopFont();
                ImGui::TextWrapped("Anyone on the same Wi-Fi can open this link, or scan the QR code on the game card. It keeps "
                                   "working while Aven is open. (%d requests so far)",
                                   shareServer_->requests());
                if (ImGui::Button("Copy the link"))
                    ImGui::SetClipboardText(shareUrl_.c_str());
                ImGui::SameLine();
                if (ImGui::Button("Open it here"))
                    openExternal(shareUrl_);
                ImGui::SameLine();
                if (ImGui::Button("Update the game"))
                    startSharing(shareResult_);
                ImGui::SameLine();
                if (ImGui::Button("Stop sharing"))
                    stopSharing();
            } else {
                ImGui::TextWrapped("Builds the web version and serves it from this computer, so friends and classmates on the same "
                                   "network can play right away.");
                ImGui::BeginDisabled(!ready);
                if (ImGui::Button("Start sharing", {160, 34}))
                    startSharing(shareResult_);
                ImGui::EndDisabled();
                if (!ready)
                    ImGui::TextDisabled("(Needs the web player: see the Web browser tab.)");
            }
            // The internet.
            ui::sectionHeader("Put it online");
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted("Aven doesn't host games itself. For a link anyone in the world can open, upload the web "
                                   "version to a free game host:");
            ImGui::BulletText("itch.io: make the zip, create a project, choose HTML and upload it.");
            ImGui::BulletText("GitHub Pages or any web host: upload the web folder.");
            ImGui::PopTextWrapPos();
            ImGui::BeginDisabled(!ready);
            if (ImGui::Button("Make the zip for itch.io"))
                makeItchZip(shareResult_);
            ImGui::EndDisabled();
            if (!shareResult_.empty()) {
                ImGui::Spacing();
                ImGui::TextWrapped("%s", shareResult_.c_str());
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

bool Editor::exportGame(const stdfs::path& folder, std::string& message) {
    std::error_code ec;
    std::string safeName;
    for (char c : settings_.name)
        safeName += (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_') ? c : '_';
    if (safeName.empty())
        safeName = "Game";
    stdfs::path out = folder / safeName;
    // Only ever replace a previous export of a game; never delete an unrelated folder.
    if (stdfs::exists(out, ec)) {
        bool previousExport = ProjectSettings::isProject(out / "game") && stdfs::exists(out / "README.txt", ec);
        bool empty = stdfs::is_directory(out, ec) && stdfs::directory_iterator(out, ec) == stdfs::directory_iterator();
        if (!previousExport && !empty) {
            message = "There's already a folder called '" + safeName + "' in " + folder.string() +
                      " that isn't an Aven export. Pick a different folder so nothing gets overwritten.";
            return false;
        }
        stdfs::remove_all(out / "game", ec);
    }
    copyGameFiles(out / "game", folder, nullptr);
#ifdef _WIN32
    stdfs::path player = fs::executableDir() / "aven-player.exe";
    stdfs::path exe = out / (safeName + ".exe");
#else
    stdfs::path player = fs::executableDir() / "aven-player";
    stdfs::path exe = out / safeName;
#endif
    if (!stdfs::exists(player)) {
        message = "Couldn't find aven-player next to the editor, so only the game files were exported to " + out.string();
        return false;
    }
    stdfs::copy_file(player, exe, stdfs::copy_options::overwrite_existing, ec);
    if (ec) {
        message = "Export failed: " + ec.message();
        return false;
    }
    stdfs::permissions(exe, stdfs::perms::owner_exec | stdfs::perms::group_exec | stdfs::perms::others_exec,
                       stdfs::perm_options::add, ec);
    fs::writeText(out / "README.txt", settings_.name + " " + settings_.version + "\n\nRun " + exe.filename().string() +
                                          " to play.\nMade with Aven.\n");
    message = "Done! Your game is in:\n" + out.string() + "\nRun " + exe.filename().string() + " to play it.";
    Log::info("Exported game to ", out.string());
    milestone("exports");
    return true;
}

} // namespace aven::editor
