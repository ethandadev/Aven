#include "aven/runtime/relay.h"

#include "aven/core/log.h"
#include "aven/runtime/net_socket.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>
#include <random>
#include <unordered_map>
#include <vector>

namespace aven::relay {

std::string frame(Kind kind, uint32_t peer, const std::string& payload) {
    uint32_t len = static_cast<uint32_t>(payload.size() + 5);
    std::string out;
    out.reserve(payload.size() + 9);
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<char>((len >> (8 * i)) & 0xFF));
    out.push_back(static_cast<char>(kind));
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<char>((peer >> (8 * i)) & 0xFF));
    out += payload;
    return out;
}

bool readFrames(std::string& buffer, const std::function<void(Kind, uint32_t, std::string&&)>& onFrame) {
    auto u32 = [&](size_t at) {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= static_cast<uint32_t>(static_cast<uint8_t>(buffer[at + static_cast<size_t>(i)])) << (8 * i);
        return v;
    };
    size_t at = 0;
    bool ok = true;
    while (buffer.size() - at >= 4) {
        uint32_t len = u32(at);
        if (len < 5 || len > kMaxPayload + 5) {
            ok = false;
            break;
        }
        if (buffer.size() - at < 4 + static_cast<size_t>(len))
            break;
        Kind kind = static_cast<Kind>(static_cast<uint8_t>(buffer[at + 4]));
        uint32_t peer = u32(at + 5);
        std::string payload = buffer.substr(at + 9, len - 5);
        at += 4 + len;
        onFrame(kind, peer, std::move(payload));
    }
    buffer.erase(0, at);
    return ok;
}

#if defined(__EMSCRIPTEN__)

struct Server::Impl {};
Server::Server(ServerLimits) {}
Server::~Server() = default;
bool Server::start(int, std::string& error, bool) {
    error = "The relay doesn't run in a browser.";
    return false;
}
int Server::port() const { return 0; }
void Server::poll(int) {}
void Server::stop() {}
int Server::rooms() const { return 0; }
int Server::connections() const { return 0; }

#else

namespace {

double now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Room codes: no 0/O or 1/I, so they're easy to read out.
constexpr char kCodeLetters[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        size_t nl = text.find('\n', start);
        out.push_back(text.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (nl == std::string::npos)
            return out;
        start = nl + 1;
    }
}

} // namespace

struct Server::Impl {
    ServerLimits limits;
    socket_t listener = INVALID_SOCKET;
    int port = 0;
    double lastTick = 0;
    std::mt19937_64 rng{std::random_device{}()};

    struct Conn {
        socket_t s = INVALID_SOCKET;
        std::string in, out, ip;
        enum class Role { New, Host, Player } role = Role::New;
        std::string room;
        uint32_t peer = 0;
        double since = 0;
        double tokens = 0;
        bool closing = false; // close once what's queued has gone (an Error, say)
    };
    struct RoomInfo {
        std::string game;
        int host = 0;
        uint32_t nextPeer = 1;
        std::map<uint32_t, int> players; // peer -> connection
    };
    std::map<int, Conn> conns;
    int nextConn = 1;
    std::unordered_map<std::string, RoomInfo> rooms;
    std::unordered_map<std::string, std::pair<int, double>> wrongCodes; // address -> (count, since)
    std::vector<int> dropping;

    double burst() const { return std::max(limits.bytesPerSecond * 2, static_cast<double>(kMaxPayload) * 1.1); }

    void send(int id, Kind kind, uint32_t peer, const std::string& payload) {
        auto it = conns.find(id);
        if (it == conns.end() || it->second.closing)
            return;
        Conn& c = it->second;
        if (c.out.size() + payload.size() > limits.maxQueued) {
            c.out.clear(); // not keeping up: let it go
            c.closing = true;
            drop(id);
            return;
        }
        c.out += frame(kind, peer, payload);
    }
    void refuse(int id, const std::string& why) {
        send(id, Error, 0, why);
        if (auto it = conns.find(id); it != conns.end())
            it->second.closing = true;
    }
    void drop(int id) {
        if (std::find(dropping.begin(), dropping.end(), id) == dropping.end())
            dropping.push_back(id);
    }

    // Closes a connection, and tells whoever needs to know.
    void close(int id) {
        auto it = conns.find(id);
        if (it == conns.end())
            return;
        Conn c = std::move(it->second);
        conns.erase(it);
        if (c.s != INVALID_SOCKET)
            AVEN_CLOSE(c.s);
        auto room = rooms.find(c.room);
        if (room == rooms.end())
            return;
        if (c.role == Conn::Role::Host && room->second.host == id) {
            for (auto& [peer, player] : room->second.players)
                refuse(player, "The host left the game.");
            rooms.erase(room);
        } else if (c.role == Conn::Role::Player) {
            room->second.players.erase(c.peer);
            send(room->second.host, PeerGone, c.peer, "");
        }
    }

    std::string newCode() {
        for (;;) {
            std::string code;
            for (int i = 0; i < 6; ++i)
                code += kCodeLetters[rng() % (sizeof kCodeLetters - 1)];
            if (!rooms.count(code))
                return code;
        }
    }

    bool tooManyWrongCodes(const std::string& ip, double t) {
        auto& [count, since] = wrongCodes[ip];
        if (t - since > 60) {
            count = 0;
            since = t;
        }
        return count >= limits.wrongCodesPerMinute;
    }

    void handle(int id, Kind kind, uint32_t peer, std::string&& payload) {
        auto it = conns.find(id);
        if (it == conns.end() || it->second.closing)
            return;
        Conn& c = it->second;
        switch (c.role) {
        case Conn::Role::New: {
            auto parts = lines(payload);
            if ((kind != Host && kind != Join) || parts.empty() || parts[0] != kVersion) {
                refuse(id, "This relay speaks " + std::string(kVersion) + "; this game needs a different one.");
                return;
            }
            if (kind == Host) {
                if (static_cast<int>(rooms.size()) >= limits.maxRooms) {
                    refuse(id, "The relay is full right now. Try again in a while.");
                    return;
                }
                std::string code = newCode();
                RoomInfo& r = rooms[code];
                r.game = parts.size() > 1 ? parts[1] : "";
                r.host = id;
                c.role = Conn::Role::Host;
                c.room = code;
                send(id, Room, 0, code);
                return;
            }
            double t = now();
            if (tooManyWrongCodes(c.ip, t)) {
                refuse(id, "Too many wrong codes. Wait a minute, then check the code with the host.");
                return;
            }
            std::string code = parts.size() > 1 ? parts[1] : "";
            std::transform(code.begin(), code.end(), code.begin(), [](char ch) { return static_cast<char>(std::toupper(static_cast<unsigned char>(ch))); });
            auto room = rooms.find(code);
            if (room == rooms.end()) {
                ++wrongCodes[c.ip].first;
                refuse(id, "There's no game with the code " + code + " (is the host still playing?).");
                return;
            }
            std::string game = parts.size() > 2 ? parts[2] : "";
            if (game != room->second.game) {
                refuse(id, "That code is for " + room->second.game + ", a different game.");
                return;
            }
            if (static_cast<int>(room->second.players.size()) >= limits.maxPlayersPerRoom) {
                refuse(id, "That game is full (" + std::to_string(limits.maxPlayersPerRoom) + " players).");
                return;
            }
            c.role = Conn::Role::Player;
            c.room = code;
            c.peer = room->second.nextPeer++;
            room->second.players[c.peer] = id;
            send(id, Joined, c.peer, code);
            send(room->second.host, PeerNew, c.peer, "");
            return;
        }
        case Conn::Role::Host: {
            auto room = rooms.find(c.room);
            if (room == rooms.end())
                return;
            auto player = room->second.players.find(peer);
            if (player == room->second.players.end())
                return; // gone already
            if (kind == Data)
                send(player->second, Data, 0, payload);
            else if (kind == PeerGone) // the host lets them go
                refuse(player->second, payload.empty() ? "The host let you go." : payload);
            return;
        }
        case Conn::Role::Player: {
            auto room = rooms.find(c.room);
            if (room != rooms.end() && kind == Data)
                send(room->second.host, Data, c.peer, payload);
            return;
        }
        }
    }

    void accept(double t) {
        for (;;) {
            sockaddr_storage from{};
            socklen_t len = sizeof from;
            socket_t s = ::accept(listener, reinterpret_cast<sockaddr*>(&from), &len);
            if (s == INVALID_SOCKET)
                return;
            if (static_cast<int>(conns.size()) >= limits.maxConnections) {
                AVEN_CLOSE(s);
                continue;
            }
            net::setNonBlocking(s);
            net::noDelay(s);
            Conn c;
            c.s = s;
            c.since = t;
            c.tokens = burst();
            char ip[INET6_ADDRSTRLEN] = {};
            if (from.ss_family == AF_INET)
                inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(&from)->sin_addr, ip, sizeof ip);
            else if (from.ss_family == AF_INET6)
                inet_ntop(AF_INET6, &reinterpret_cast<sockaddr_in6*>(&from)->sin6_addr, ip, sizeof ip);
            c.ip = ip;
            conns[nextConn++] = std::move(c);
        }
    }

    // Reads what a connection sent, within its allowance; false when it's gone.
    bool read(int id, Conn& c) {
        char buf[16384];
        while (c.tokens > 0) {
            int want = static_cast<int>(std::min<double>(sizeof buf, c.tokens));
            auto n = ::recv(c.s, buf, want, 0);
            if (n == 0)
                return false;
            if (n < 0)
                return net::wouldBlock();
            c.in.append(buf, static_cast<size_t>(n));
            c.tokens -= static_cast<double>(n);
        }
        return true;
    }

    bool write(Conn& c) {
        while (!c.out.empty()) {
            auto n = ::send(c.s, c.out.data(), static_cast<int>(std::min<size_t>(c.out.size(), 1 << 16)), AVEN_NOSIGNAL);
            if (n < 0)
                return net::wouldBlock();
            if (n == 0)
                return true;
            c.out.erase(0, static_cast<size_t>(n));
        }
        return true;
    }
};

Server::Server(ServerLimits limits) : impl_(std::make_unique<Impl>()) { impl_->limits = limits; }

Server::~Server() { stop(); }

bool Server::start(int port, std::string& error, bool loopbackOnly) {
    net::socketsReady();
    stop();
    Impl& n = *impl_;
    int yes = 1, no = 0;
    // Both IPv6 and IPv4 where the system can (one socket for both), else IPv4.
    if (!loopbackOnly) {
        n.listener = ::socket(AF_INET6, SOCK_STREAM, 0);
        if (n.listener != INVALID_SOCKET) {
            setsockopt(n.listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof yes);
            setsockopt(n.listener, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&no), sizeof no);
            sockaddr_in6 addr{};
            addr.sin6_family = AF_INET6;
            addr.sin6_addr = {}; // any address
            addr.sin6_port = htons(static_cast<uint16_t>(port));
            if (::bind(n.listener, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
                AVEN_CLOSE(n.listener);
                n.listener = INVALID_SOCKET;
            }
        }
    }
    if (n.listener == INVALID_SOCKET) {
        n.listener = ::socket(AF_INET, SOCK_STREAM, 0);
        if (n.listener == INVALID_SOCKET) {
            error = "Couldn't open a network connection.";
            return false;
        }
        setsockopt(n.listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof yes);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(loopbackOnly ? INADDR_LOOPBACK : INADDR_ANY);
        addr.sin_port = htons(static_cast<uint16_t>(port));
        if (::bind(n.listener, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
            AVEN_CLOSE(n.listener);
            n.listener = INVALID_SOCKET;
            error = "Port " + std::to_string(port) + " is busy.";
            return false;
        }
    }
    if (::listen(n.listener, 64) != 0) {
        stop();
        error = "Couldn't listen on port " + std::to_string(port) + ".";
        return false;
    }
    net::setNonBlocking(n.listener);
    sockaddr_storage bound{};
    socklen_t len = sizeof bound;
    getsockname(n.listener, reinterpret_cast<sockaddr*>(&bound), &len);
    n.port = ntohs(bound.ss_family == AF_INET6 ? reinterpret_cast<sockaddr_in6*>(&bound)->sin6_port
                                               : reinterpret_cast<sockaddr_in*>(&bound)->sin_port);
    n.lastTick = now();
    return true;
}

int Server::port() const { return impl_->port; }

void Server::poll(int timeoutMs) {
    Impl& n = *impl_;
    if (n.listener == INVALID_SOCKET)
        return;
    std::vector<aven_pollfd> fds;
    std::vector<int> ids;
    fds.push_back({n.listener, POLLIN, 0});
    ids.push_back(0);
    for (auto& [id, c] : n.conns) {
        short events = 0;
        if (c.tokens > 0 && !c.closing)
            events |= POLLIN;
        if (!c.out.empty())
            events |= POLLOUT;
        fds.push_back({c.s, events, 0});
        ids.push_back(id);
    }
#if defined(_WIN32)
    AVEN_POLL(fds.data(), static_cast<ULONG>(fds.size()), timeoutMs);
#else
    AVEN_POLL(fds.data(), static_cast<nfds_t>(fds.size()), timeoutMs);
#endif

    double t = now();
    double refill = (t - n.lastTick) * n.limits.bytesPerSecond;
    n.lastTick = t;
    for (auto& [id, c] : n.conns)
        c.tokens = std::min(n.burst(), c.tokens + refill);

    if (fds[0].revents & POLLIN)
        n.accept(t);
    for (size_t i = 1; i < fds.size(); ++i) {
        int id = ids[i];
        auto it = n.conns.find(id);
        if (it == n.conns.end())
            continue;
        Impl::Conn& c = it->second;
        if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            if (!(fds[i].revents & POLLIN)) {
                n.drop(id);
                continue;
            }
        }
        if ((fds[i].revents & POLLIN) && !c.closing) {
            if (!n.read(id, c)) {
                n.drop(id);
                continue;
            }
            bool ok = readFrames(c.in, [&](Kind kind, uint32_t peer, std::string&& payload) {
                n.handle(id, kind, peer, std::move(payload));
            });
            if (!ok)
                n.drop(id);
        }
    }
    // Send what's queued; say goodbye to connections that are done or never said hello.
    for (auto& [id, c] : n.conns) {
        if (!n.write(c) || (c.closing && c.out.empty()))
            n.drop(id);
        else if (c.role == Impl::Conn::Role::New && t - c.since > n.limits.helloSeconds)
            n.drop(id);
    }
    // (closing one can queue a goodbye for others, and drop more)
    while (!n.dropping.empty()) {
        int id = n.dropping.back();
        n.dropping.pop_back();
        n.close(id);
    }
}

void Server::stop() {
    Impl& n = *impl_;
    for (auto& [id, c] : n.conns)
        if (c.s != INVALID_SOCKET)
            AVEN_CLOSE(c.s);
    n.conns.clear();
    n.rooms.clear();
    n.dropping.clear();
    if (n.listener != INVALID_SOCKET) {
        AVEN_CLOSE(n.listener);
        n.listener = INVALID_SOCKET;
    }
}

int Server::rooms() const { return static_cast<int>(impl_->rooms.size()); }
int Server::connections() const { return static_cast<int>(impl_->conns.size()); }

#endif

} // namespace aven::relay
