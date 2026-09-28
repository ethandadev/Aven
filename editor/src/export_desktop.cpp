// Desktop apps: the game as a Windows program, a macOS app and a Linux program, from any computer.
//
// Each needs Aven's player built for that system. The editor's own player covers the system it runs
// on; the others come from players/<system>/ beside the editor (release downloads include all three).
// The game's icon goes into each (inside the .exe, an .icns in the .app, a PNG for Linux), with its
// name, version, author and description. Signing runs the system's tools when they're installed:
// codesign and notarytool on a Mac, signtool on Windows (or osslsigncode anywhere).
//
// The slow part (copying, zipping, signing, notarizing) runs on a background thread with a log.

#include "editor.h"

#include "aven/core/fs.h"
#include "aven/core/icons.h"
#include "aven/core/log.h"
#include "aven/core/zip.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#define AVEN_POPEN _popen
#define AVEN_PCLOSE _pclose
#else
#include <sys/wait.h>
#define AVEN_POPEN popen
#define AVEN_PCLOSE pclose
#endif

namespace aven::editor {

namespace {

struct Target {
    const char* id;      // players/<id>
    const char* label;
    const char* program; // the player's file name
};
const Target kTargets[] = {
    {"windows-x64", "Windows", "aven-player.exe"},
    {"macos-arm64", "macOS (Apple silicon)", "aven-player"},
    {"linux-x64", "Linux", "aven-player"},
};

const char* hostTarget() {
#if defined(_WIN32)
    return "windows-x64";
#elif defined(__APPLE__)
    return "macos-arm64";
#else
    return "linux-x64";
#endif
}

// Letters, digits and dashes, for bundle ids and Linux file names.
std::string slug(const std::string& s, bool lower) {
    std::string out;
    for (unsigned char c : s) {
        if (c == '\'')
            continue; // "Sam's" -> "sams"
        if (std::isalnum(c))
            out += lower ? static_cast<char>(std::tolower(c)) : static_cast<char>(c);
        else if (!out.empty() && out.back() != '-')
            out += '-';
    }
    while (!out.empty() && out.back() == '-')
        out.pop_back();
    return out.empty() ? "game" : out;
}

std::string xmlEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;";
        else out += c;
    }
    return out;
}

// One argument for the shell popen() runs. Arguments with characters that can't be quoted safely
// there are refused rather than risk running something else.
bool quote(const std::string& arg, std::string& out) {
#if defined(_WIN32)
    if (arg.find_first_of("\"\r\n%") != std::string::npos)
        return false;
    out = "\"" + arg + "\"";
#else
    if (arg.find_first_of("\r\n") != std::string::npos)
        return false;
    out = "'";
    for (char c : arg)
        out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    out += "'";
#endif
    return true;
}

// Finds a program on the PATH (and, for signtool, in the Windows SDK).
stdfs::path findTool(const std::string& name) {
    std::error_code ec;
#if defined(_WIN32)
    const char sep = ';';
    std::string file = name + ".exe";
#else
    const char sep = ':';
    std::string file = name;
#endif
    if (const char* path = std::getenv("PATH")) {
        std::stringstream ss(path);
        std::string dir;
        while (std::getline(ss, dir, sep))
            if (!dir.empty() && stdfs::exists(stdfs::path(dir) / file, ec))
                return stdfs::path(dir) / file;
    }
#if defined(_WIN32)
    if (name == "signtool") {
        stdfs::path kits = "C:/Program Files (x86)/Windows Kits/10/bin";
        stdfs::path best;
        for (auto& e : stdfs::directory_iterator(kits, ec))
            if (stdfs::exists(e.path() / "x64" / "signtool.exe", ec) && e.path().filename().string() > best.parent_path().parent_path().filename().string())
                best = e.path() / "x64" / "signtool.exe";
        return best;
    }
#endif
    return {};
}

} // namespace

// ---------------------------------------------------------------- the background job

struct Editor::DesktopJob {
    std::mutex mutex;
    std::vector<std::string> log;
    std::atomic<bool> done{false};
    bool ok = true;
    std::vector<stdfs::path> results; // the zips made

    void say(const std::string& line) {
        std::lock_guard<std::mutex> lock(mutex);
        log.push_back(line);
        Log::info("Export: ", line);
    }
    // Runs a program; its output goes into the log. Returns its exit code (-1: couldn't run it).
    int run(const std::vector<std::string>& args, std::string* output = nullptr, bool quiet = false) {
        std::string cmd, q;
        for (size_t i = 0; i < args.size(); ++i) {
            if (!quote(args[i], q)) {
                say("Can't pass this safely to " + args[0] + ": " + args[i]);
                return -1;
            }
            cmd += (i ? " " : "") + q;
        }
        say("> " + stdfs::path(args[0]).filename().string() + (args.size() > 1 ? " " + args[1] + " ..." : ""));
#if defined(_WIN32)
        cmd = "\"" + cmd + " 2>&1\""; // cmd.exe drops the outer quotes
#else
        cmd += " 2>&1";
#endif
        FILE* pipe = AVEN_POPEN(cmd.c_str(), "r");
        if (!pipe) {
            say("Couldn't start " + args[0]);
            return -1;
        }
        char buffer[1024];
        while (std::fgets(buffer, sizeof buffer, pipe)) {
            std::string line = buffer;
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                line.pop_back();
            if (output)
                *output += line + "\n";
            if (!quiet && !line.empty())
                say("  " + line);
        }
        int status = AVEN_PCLOSE(pipe);
#if !defined(_WIN32)
        if (WIFEXITED(status))
            status = WEXITSTATUS(status);
#endif
        return status;
    }
};

struct DesktopPlan {
    ProjectSettings settings;
    std::string name;          // for files and the app
    stdfs::path out, staged;   // output folder, the game's files copied once
    std::map<int, std::vector<uint8_t>> icons; // PNGs by size
    std::vector<std::pair<std::string, stdfs::path>> targets; // target id, player folder
    std::string windowsPassword;
};

namespace {

std::string readme(const ProjectSettings& s, const std::string& how) {
    std::string text = s.name + " " + s.version + "\n";
    if (!s.description.empty())
        text += "\n" + s.description + "\n";
    if (!s.publish.author.empty())
        text += "\nBy " + s.publish.author + (s.publish.website.empty() ? "" : "  " + s.publish.website) + "\n";
    return text + "\n" + how + "\n\nMade with Aven.\n";
}

bool copyTree(const stdfs::path& from, const stdfs::path& to) {
    std::error_code ec;
    stdfs::create_directories(to, ec);
    stdfs::copy(from, to, stdfs::copy_options::recursive | stdfs::copy_options::overwrite_existing, ec);
    return !ec;
}

// Replaces a previous export folder, but never a folder that isn't one.
bool prepare(Editor::DesktopJob& job, const stdfs::path& dir) {
    std::error_code ec;
    if (stdfs::exists(dir, ec)) {
        if (!stdfs::exists(dir / ".aven-export", ec)) {
            job.say("There's already a folder called '" + dir.filename().string() +
                    "' that isn't an Aven export; pick another output folder so nothing gets overwritten.");
            return false;
        }
        stdfs::remove_all(dir, ec);
    }
    stdfs::create_directories(dir, ec);
    fs::writeText(dir / ".aven-export", "Made by Aven's Build & Share; replaced by the next export.\n");
    return !ec;
}

std::string copyright(const ProjectSettings& s) {
    if (!s.publish.copyright.empty())
        return s.publish.copyright;
    if (s.publish.author.empty())
        return "";
    std::time_t now = std::time(nullptr);
    return "Copyright (c) " + std::to_string(1900 + std::localtime(&now)->tm_year) + " " + s.publish.author;
}

bool exportWindows(Editor::DesktopJob& job, const DesktopPlan& p, const stdfs::path& player) {
    stdfs::path dir = p.out / (p.name + " for Windows");
    if (!prepare(job, dir))
        return false;
    job.say("Windows: " + dir.string());
    copyTree(p.staged, dir / "game");
    std::error_code ec;
    // Everything beside the player (the Visual C++ runtime DLLs in release downloads) comes along.
    for (auto& e : stdfs::directory_iterator(player, ec))
        if (fs::extension(e.path()) == ".dll")
            stdfs::copy_file(e.path(), dir / e.path().filename(), stdfs::copy_options::overwrite_existing, ec);
    auto exe = fs::readBinary(player / "aven-player.exe");
    if (!exe) {
        job.say("Couldn't read the Windows player.");
        return false;
    }
    std::string error;
    auto icon256 = p.icons.find(256);
    if (icon256 != p.icons.end() && !icons::replaceExeIcon(*exe, icon256->second, error))
        job.say("The icon couldn't go into the .exe (" + error + "); it still shows on the window.");
    stdfs::path exePath = dir / (p.name + ".exe");
    fs::writeBinary(exePath, exe->data(), exe->size());
    fs::writeText(dir / "README.txt", readme(p.settings, "Double-click " + exePath.filename().string() + " to play.\n"
                                                         "If Windows says it protected your PC (SmartScreen), click More info > Run anyway."));

    // Signing: signtool on Windows, or osslsigncode on any system.
    const PublishSettings& pub = p.settings.publish;
    if (!pub.windowsCertificate.empty()) {
        bool file = fs::extension(pub.windowsCertificate) == ".pfx" || fs::extension(pub.windowsCertificate) == ".p12";
        stdfs::path signtool = findTool("signtool"), ossl = findTool("osslsigncode");
        int code = -1;
        if (!signtool.empty()) {
            std::vector<std::string> a = {signtool.string(), "sign", "/fd", "SHA256", "/tr", pub.timestampUrl, "/td", "SHA256",
                                          "/d", p.settings.name};
            if (file) {
                a.insert(a.end(), {"/f", pub.windowsCertificate});
                if (!p.windowsPassword.empty())
                    a.insert(a.end(), {"/p", p.windowsPassword});
            } else {
                a.insert(a.end(), {"/sha1", pub.windowsCertificate});
            }
            a.push_back(exePath.string());
            code = job.run(a);
        } else if (!ossl.empty() && file) {
            stdfs::path signedExe = exePath.string() + ".signed";
            std::vector<std::string> a = {ossl.string(), "sign", "-pkcs12", pub.windowsCertificate, "-n", p.settings.name,
                                          "-h", "sha256", "-ts", pub.timestampUrl, "-in", exePath.string(), "-out", signedExe.string()};
            if (!p.windowsPassword.empty())
                a.insert(a.begin() + 4, {"-pass", p.windowsPassword});
            if (!pub.website.empty())
                a.insert(a.begin() + 2, {"-i", pub.website});
            code = job.run(a);
            if (code == 0)
                stdfs::rename(signedExe, exePath, ec);
        } else {
            job.say("Not signed: signtool (Windows SDK) or osslsigncode isn't installed" +
                    std::string(file ? "." : ", and a certificate thumbprint needs signtool on Windows."));
        }
        if (code == 0)
            job.say("Signed " + exePath.filename().string() + ".");
        else if (code > 0)
            job.say("Signing failed (see above); the game still works, unsigned.");
    }
    stdfs::path zipPath = p.out / (p.name + " for Windows.zip");
    if (!zip::write(zipPath, dir.parent_path(), [&](const std::string& f) { return f.rfind(dir.filename().string() + "/", 0) == 0 && f.find("/.aven-export") == std::string::npos; })) {
        job.say("Couldn't write " + zipPath.string());
        return false;
    }
    job.results.push_back(zipPath);
    return true;
}

bool exportMac(Editor::DesktopJob& job, const DesktopPlan& p, const stdfs::path& player) {
    stdfs::path dir = p.out / (p.name + " for macOS");
    if (!prepare(job, dir))
        return false;
    job.say("macOS: " + dir.string());
    stdfs::path app = dir / (p.name + ".app"), contents = app / "Contents";
    std::error_code ec;
    stdfs::create_directories(contents / "MacOS", ec);
    stdfs::create_directories(contents / "Resources", ec);
    copyTree(p.staged, contents / "Resources" / "game");
    stdfs::path binary = contents / "MacOS" / p.name;
    stdfs::copy_file(player / "aven-player", binary, stdfs::copy_options::overwrite_existing, ec);
    if (ec) {
        job.say("Couldn't copy the macOS player: " + ec.message());
        return false;
    }
    stdfs::permissions(binary, stdfs::perms::owner_all | stdfs::perms::group_read | stdfs::perms::group_exec |
                                   stdfs::perms::others_read | stdfs::perms::others_exec, ec);
    auto icns = icons::makeIcns(p.icons);
    fs::writeBinary(contents / "Resources" / "AppIcon.icns", icns.data(), icns.size());
    const PublishSettings& pub = p.settings.publish;
    std::string bundleId = !pub.bundleId.empty() ? pub.bundleId
                                                 : "com." + slug(pub.author.empty() ? "aven" : pub.author, true) + "." +
                                                       slug(p.settings.name, true);
    std::string plist = R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>)" + xmlEscape(p.settings.name) + R"(</string>
    <key>CFBundleDisplayName</key><string>)" + xmlEscape(p.settings.name) + R"(</string>
    <key>CFBundleIdentifier</key><string>)" + xmlEscape(bundleId) + R"(</string>
    <key>CFBundleExecutable</key><string>)" + xmlEscape(p.name) + R"(</string>
    <key>CFBundleIconFile</key><string>AppIcon</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>)" + xmlEscape(p.settings.version) + R"(</string>
    <key>CFBundleVersion</key><string>)" + xmlEscape(p.settings.version) + R"(</string>
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
    <key>NSHumanReadableCopyright</key><string>)" + xmlEscape(copyright(p.settings)) + R"(</string>
    <key>LSApplicationCategoryType</key><string>public.app-category.games</string>
    <key>LSMinimumSystemVersion</key><string>11.0</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
)";
    fs::writeText(contents / "Info.plist", plist);
    fs::writeText(contents / "PkgInfo", "APPL????");

    // Signing: only possible on a Mac. With a Team ID: Developer ID and hardened runtime, then
    // notarized when a notarytool profile is set; without one, ad hoc (enough to run on your Mac).
    bool signedApp = false;
    stdfs::path codesign = findTool("codesign");
    if (!codesign.empty()) {
        std::string identity = pub.appleIdentity;
        if (identity.empty() && !pub.appleTeamId.empty()) {
            // The "Developer ID Application: Name (TEAMID)" certificate in the keychain.
            std::string list;
            job.run({"security", "find-identity", "-v", "-p", "codesigning"}, &list, true);
            std::stringstream ss(list);
            std::string line;
            while (std::getline(ss, line))
                if (line.find("Developer ID Application") != std::string::npos &&
                    line.find("(" + pub.appleTeamId + ")") != std::string::npos) {
                    size_t q = line.find('"');
                    identity = line.substr(q + 1, line.rfind('"') - q - 1);
                    break;
                }
            if (identity.empty())
                job.say("No \"Developer ID Application\" certificate for team " + pub.appleTeamId +
                        " in the keychain (Xcode > Settings > Accounts > Manage Certificates can make one). Signing ad hoc.");
        }
        std::vector<std::string> a = {codesign.string(), "--force", "--deep", "--sign", identity.empty() ? "-" : identity};
        if (!identity.empty())
            a.insert(a.begin() + 3, {"--options", "runtime", "--timestamp"});
        a.push_back(app.string());
        signedApp = job.run(a) == 0;
        job.say(signedApp ? (identity.empty() ? "Signed ad hoc (runs on this Mac; others must right-click > Open)."
                                              : "Signed as " + identity + ".")
                          : "Signing failed (see above).");
        if (signedApp && !identity.empty() && !pub.notaryProfile.empty()) {
            stdfs::path upload = dir / "notarize.zip";
            job.run({"ditto", "-c", "-k", "--keepParent", app.string(), upload.string()});
            job.say("Sending to Apple to notarize (this can take a few minutes)...");
            bool notarized = job.run({"xcrun", "notarytool", "submit", upload.string(), "--keychain-profile", pub.notaryProfile,
                                      "--wait"}) == 0;
            stdfs::remove(upload, ec);
            if (notarized && job.run({"xcrun", "stapler", "staple", app.string()}) == 0)
                job.say("Notarized: it opens on any Mac without warnings.");
            else
                job.say("Notarizing failed (see above). The app is signed but Macs will warn before opening it.");
        }
    } else if (!pub.appleTeamId.empty()) {
        job.say("Not signed: signing a macOS app needs a Mac (codesign). Export on a Mac to sign and notarize it.");
    }
    fs::writeText(dir / "README.txt",
                  readme(p.settings, "Open " + app.filename().string() + " to play (drag it to Applications to keep it).\n"
                                     "If macOS says it can't check it for malicious software, right-click it and choose Open."));
    stdfs::path zipPath = p.out / (p.name + " for macOS.zip");
    bool zipped = false;
    if (!findTool("ditto").empty()) // keeps everything a signed app needs, exactly
        zipped = job.run({"ditto", "-c", "-k", "--keepParent", dir.string(), zipPath.string()}, nullptr, true) == 0;
    if (!zipped) {
        std::string top = dir.filename().string() + "/";
        std::string exec = top + app.filename().string() + "/Contents/MacOS/" + p.name;
        zipped = zip::write(zipPath, dir.parent_path(),
                            [&](const std::string& f) { return f.rfind(top, 0) == 0 && f.find("/.aven-export") == std::string::npos; },
                            [&](const std::string& f) { return f == exec; });
    }
    if (!zipped) {
        job.say("Couldn't write " + zipPath.string());
        return false;
    }
    job.results.push_back(zipPath);
    return true;
}

bool exportLinux(Editor::DesktopJob& job, const DesktopPlan& p, const stdfs::path& player) {
    stdfs::path dir = p.out / (p.name + " for Linux");
    if (!prepare(job, dir))
        return false;
    job.say("Linux: " + dir.string());
    copyTree(p.staged, dir / "game");
    std::string file = slug(p.settings.name, true);
    std::error_code ec;
    stdfs::copy_file(player / "aven-player", dir / file, stdfs::copy_options::overwrite_existing, ec);
    if (ec) {
        job.say("Couldn't copy the Linux player: " + ec.message());
        return false;
    }
    stdfs::permissions(dir / file, stdfs::perms::owner_all | stdfs::perms::group_read | stdfs::perms::group_exec |
                                       stdfs::perms::others_read | stdfs::perms::others_exec, ec);
    if (auto it = p.icons.find(256); it != p.icons.end())
        fs::writeBinary(dir / (file + ".png"), it->second.data(), it->second.size());
    // A launcher: copy it to ~/.local/share/applications (and fix the paths) to get a menu entry.
    std::string desktop = "[Desktop Entry]\nType=Application\nName=" + p.settings.name + "\nComment=" + p.settings.description +
                          "\nExec=./" + file + "\nIcon=" + file + "\nTerminal=false\nCategories=Game;\n";
    fs::writeText(dir / (file + ".desktop"), desktop);
    fs::writeText(dir / "README.txt", readme(p.settings, "Run ./" + file + " to play. " + file +
                                                         ".desktop is a launcher you can add to your applications menu."));
    stdfs::path zipPath = p.out / (p.name + " for Linux.zip");
    std::string top = dir.filename().string() + "/";
    if (!zip::write(zipPath, dir.parent_path(),
                    [&](const std::string& f) { return f.rfind(top, 0) == 0 && f.find("/.aven-export") == std::string::npos; },
                    [&](const std::string& f) { return f == top + file; })) {
        job.say("Couldn't write " + zipPath.string());
        return false;
    }
    job.results.push_back(zipPath);
    return true;
}

} // namespace

// ---------------------------------------------------------------- editor side

stdfs::path Editor::playerFolder(const std::string& target) const {
    std::error_code ec;
    for (auto& t : kTargets) {
        if (target != t.id)
            continue;
        stdfs::path bundled = fs::resourceDir() / "players" / t.id;
        if (stdfs::exists(bundled / t.program, ec))
            return bundled;
        if (target == hostTarget() && stdfs::exists(fs::executableDir() / t.program, ec))
            return fs::executableDir();
    }
    return {};
}

// The game's icon as RGBA: the chosen picture, or a picture of the start scene.
std::vector<uint8_t> Editor::gameIconPixels(int& w, int& h) {
    std::vector<uint8_t> px;
    const std::string& icon = settings_.publish.icon;
    if (!icon.empty() && Assets::loadImage(projectDir_ / icon, px, w, h))
        return px;
    w = h = 512;
    return renderStartScene(512, 512, false);
}

bool Editor::startDesktopExport(const std::vector<std::string>& targets, bool wait) {
    if (desktopJob_ && !desktopJob_->done)
        return false;
    saveScene();
    saveAllScripts();
    auto job = std::make_shared<DesktopJob>();
    desktopJob_ = job;
    auto plan = std::make_shared<DesktopPlan>();
    plan->settings = settings_;
    // File names keep the game's name as it is ("3D Obby.app"), minus what systems don't allow in them.
    for (char c : settings_.name)
        if (std::string("<>:\"/\\|?*").find(c) == std::string::npos && static_cast<unsigned char>(c) >= 32)
            plan->name += c;
    while (!plan->name.empty() && (plan->name.back() == ' ' || plan->name.back() == '.'))
        plan->name.pop_back();
    if (plan->name.empty())
        plan->name = "Game";
    plan->out = exportFolder_.empty() ? projectDir_ / "exports" : stdfs::path(exportFolder_);
    plan->windowsPassword = signPassword_;
    for (auto& t : targets) {
        stdfs::path folder = playerFolder(t);
        if (folder.empty())
            job->say("Skipping " + t + ": its player isn't in this copy of Aven (players/" + t + ").");
        else
            plan->targets.push_back({t, folder});
    }
    if (plan->targets.empty()) {
        job->ok = false;
        job->done = true;
        return false;
    }
    // On this thread: the icon (rendering needs the GPU) and one copy of the game's files.
    int w = 0, h = 0;
    std::vector<uint8_t> px = gameIconPixels(w, h);
    if (!px.empty())
        for (int size : {16, 32, 48, 64, 128, 256, 512, 1024}) {
            auto scaled = icons::resize(px.data(), w, h, size);
            plan->icons[size] = icons::encodePng(scaled.data(), size, size);
        }
    std::error_code ec;
    plan->staged = plan->out / ".staging" / "game";
    stdfs::remove_all(plan->staged, ec);
    copyGameFiles(plan->staged, plan->out, nullptr, GameCopy::Desktop);
    if (auto it = plan->icons.find(256); it != plan->icons.end())
        fs::writeBinary(plan->staged / "app-icon.png", it->second.data(), it->second.size());
    job->say("Exporting " + settings_.name + " " + settings_.version + "...");

    auto work = [job, plan] {
        for (auto& [id, folder] : plan->targets) {
            bool ok = id == std::string("windows-x64") ? exportWindows(*job, *plan, folder)
                    : id == std::string("macos-arm64") ? exportMac(*job, *plan, folder)
                                                       : exportLinux(*job, *plan, folder);
            job->ok = job->ok && ok;
        }
        std::error_code e;
        stdfs::remove_all(plan->out / ".staging", e);
        job->say(job->ok ? "Done! Zips ready to share:" : "Finished, with problems (see above).");
        for (auto& z : job->results)
            job->say("  " + z.string());
        job->done = true;
    };
    if (wait)
        work();
    else
        std::thread(work).detach();
    milestone("exports");
    return true;
}

void Editor::drawDesktopExport() {
    ImGui::TextWrapped("Makes your game into apps people can download and play without Aven: a Windows program, a "
                       "macOS app and a Linux program, each zipped and ready to upload (itch.io, your website, a USB stick).");
    ImGui::Spacing();
    for (auto& t : kTargets) {
        bool available = !playerFolder(t.id).empty();
        bool& on = exportTargets_[t.id];
        ImGui::BeginDisabled(!available);
        bool shown = on && available;
        if (ImGui::Checkbox(t.label, &shown))
            on = shown;
        ImGui::EndDisabled();
        if (!available) {
            ImGui::SameLine();
            ImGui::TextDisabled("(needs players/%s: included in Aven's release downloads)", t.id);
        }
    }
    ImGui::InputText("Output folder", &exportFolder_);
    if (exportFolder_.empty())
        exportFolder_ = (projectDir_ / "exports").string();

    PublishSettings& pub = settings_.publish;
    bool changed = false;
    if (ImGui::CollapsingHeader("Signing (optional)")) {
        ImGui::TextWrapped("Signed apps open without \"unknown developer\" warnings. Nothing secret is saved in the project.");
        ImGui::SeparatorText("macOS (needs a Mac and an Apple Developer account)");
        changed |= ImGui::InputTextWithHint("Team ID", "e.g. A1B2C3D4E5", &pub.appleTeamId);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Your Apple Developer Team ID (developer.apple.com > Account > Membership details).");
        changed |= ImGui::InputTextWithHint("Signing identity", "empty: the Developer ID Application certificate", &pub.appleIdentity);
        changed |= ImGui::InputTextWithHint("Notary profile", "empty: don't notarize", &pub.notaryProfile);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Make one once in Terminal:\n  xcrun notarytool store-credentials MyProfile --apple-id you@example.com "
                              "--team-id TEAMID\nand type its name here. Notarized apps open on any Mac without warnings.");
        ImGui::TextDisabled("codesign: %s", findTool("codesign").empty() ? "not here (sign on a Mac)" : "found");
        ImGui::SeparatorText("Windows (needs a code signing certificate)");
        changed |= ImGui::InputTextWithHint("Certificate", ".pfx file, or a certificate thumbprint", &pub.windowsCertificate);
        ImGui::InputText("Password", &signPassword_, ImGuiInputTextFlags_Password);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The .pfx file's password. Used for this export only; never saved.");
        changed |= ImGui::InputText("Timestamp server", &pub.timestampUrl);
        ImGui::TextDisabled("signtool: %s   osslsigncode: %s", findTool("signtool").empty() ? "not found" : "found",
                            findTool("osslsigncode").empty() ? "not found" : "found");
    }
    if (changed)
        settings_.save(projectDir_);

    ImGui::Spacing();
    bool running = desktopJob_ && !desktopJob_->done;
    std::vector<std::string> chosen;
    for (auto& t : kTargets)
        if (exportTargets_[t.id] && !playerFolder(t.id).empty())
            chosen.push_back(t.id);
    ImGui::BeginDisabled(running || chosen.empty());
    if (ImGui::Button(running ? "Exporting..." : "Export apps", {ui::px(160), ui::px(34)}))
        startDesktopExport(chosen, false);
    ImGui::EndDisabled();
    if (desktopJob_) {
        ImGui::SameLine();
        if (desktopJob_->done && !desktopJob_->results.empty() && ImGui::Button("Open the folder", {0, ui::px(34)}))
            openExternal(desktopJob_->results.front().parent_path().string());
        ImGui::BeginChild("##exportlog", {0, 0}, ImGuiChildFlags_Borders);
        std::lock_guard<std::mutex> lock(desktopJob_->mutex);
        for (auto& line : desktopJob_->log)
            ImGui::TextWrapped("%s", line.c_str());
        if (running)
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
}

} // namespace aven::editor

namespace aven::editor {

// Build & Share > Game details: how the game presents itself, used by every kind of export.
void Editor::drawGameDetails() {
    ProjectSettings& s = settings_;
    PublishSettings& pub = s.publish;
    bool changed = false;
    ImGui::TextWrapped("How your game introduces itself: in its window title, app icon, title screen, web page and the "
                       "\"About\" details of the apps.");
    ImGui::Spacing();
    ImGui::PushItemWidth(-ui::px(120));
    changed |= ImGui::InputText("Name", &s.name);
    changed |= ImGui::InputText("Version", &s.version);
    changed |= ImGui::InputTextMultiline("Description", &s.description, {-ui::px(120), ui::px(60)});
    changed |= ImGui::InputTextWithHint("Made by", "you or your studio", &pub.author);
    changed |= ImGui::InputTextWithHint("Website", "https://...", &pub.website);
    changed |= ImGui::InputTextWithHint("Copyright", "empty: (c) this year, and who made it", &pub.copyright);
    changed |= ImGui::InputTextWithHint("App ID", "empty: made from the names, e.g. com.samsgames.obby", &pub.bundleId);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("A unique id for the macOS app, written backwards like a web address. Keep it the same "
                          "between versions so Macs know it's the same game.");
    ImGui::PopItemWidth();

    ImGui::SeparatorText("Icon");
    std::string icon = pub.icon;
    ImGui::SetNextItemWidth(-ui::px(120));
    if (ui::assetField("Icon picture", icon, projectFiles({".png", ".jpg", ".jpeg"}), "ASSET_PATH")) {
        pub.icon = icon;
        changed = true;
    }
    ImGui::TextDisabled(pub.icon.empty() ? "No picture chosen: the icon is a picture of the start scene."
                                         : "Square pictures, 512 x 512 or bigger, look best.");
    if (!pub.icon.empty()) {
        const TextureAsset& t = assets_.texture(pub.icon);
        if (!t.missing)
            ImGui::Image(static_cast<ImTextureID>(device_.nativeTexture(t.handle)), {ui::px(96), ui::px(96)}, {0, 1}, {1, 0});
    }

    ImGui::SeparatorText("When the game starts");
    changed |= ImGui::Checkbox("\"Made with Aven\" for a moment", &pub.splash);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("About a second, and any key, click or tap skips it. Thanks for the credit!");
    changed |= ImGui::Checkbox("A title screen with Play and Quit", &pub.titleScreen);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Shows the game's name and description over a picture, until the player presses Play.\n"
                          "(Web versions have no Quit: a page can't close its own tab.)");
    if (pub.titleScreen) {
        std::string image = pub.titleImage;
        ImGui::SetNextItemWidth(-ui::px(120));
        if (ui::assetField("Background", image, projectFiles({".png", ".jpg", ".jpeg"}), "ASSET_PATH")) {
            pub.titleImage = image;
            changed = true;
        }
        if (pub.titleImage.empty())
            ImGui::TextDisabled("No picture chosen: the game's thumbnail.png, if it has one.");
    }
    ImGui::TextDisabled("Both show in exported games, not when you press Play here.");
    stdfs::path player = playerFolder(hostTarget());
    ImGui::BeginDisabled(player.empty());
    if (ImGui::Button("Try it as players will see it")) {
        saveScene();
        saveAllScripts();
        // The standalone player, straight from the project, with the splash and title screen.
        stdfs::path program;
        for (auto& t : kTargets)
            if (hostTarget() == std::string(t.id))
                program = player / t.program;
        if (!launchCommand("\"" + program.string() + "\" \"" + projectDir_.string() + "\""))
            notify("Couldn't start the player.", true);
    }
    ImGui::EndDisabled();
    if (changed) {
        settings_.save(projectDir_);
        refreshTitle();
    }
}

} // namespace aven::editor
