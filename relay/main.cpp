// rynax-relay: lets Rynax games on different networks play together. Hosts get a room code from it,
// players join with the code, and it passes their messages along. See docs/online-multiplayer.md.
//
//   rynax-relay [--port 4243] [--max-rooms 1000] [--max-players 16] [--rate 512]

#include "rynax/runtime/relay.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

std::atomic<bool> running{true};

void onSignal(int) { running = false; }

int usage() {
    std::printf("rynax-relay: the server for Rynax's online multiplayer.\n\n"
                "  --port N          where to listen (default %d, TCP)\n"
                "  --max-rooms N     games at once (default 1000)\n"
                "  --max-players N   players in one game, besides the host (default 16)\n"
                "  --rate KB         what one player may send each second, on average (default 512)\n",
                rynax::relay::kDefaultPort);
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    int port = rynax::relay::kDefaultPort;
    rynax::relay::ServerLimits limits;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        // A whole number from `low` to `high`, or a clear message and the usage.
        auto value = [&](long low, long high) -> int {
            char* end = nullptr;
            long v = i + 1 < argc ? std::strtol(argv[i + 1], &end, 10) : 0;
            if (i + 1 >= argc || end == argv[i + 1] || *end || v < low || v > high) {
                std::fprintf(stderr, "%s needs a number from %ld to %ld after it.\n", a.c_str(), low, high);
                std::exit(usage());
            }
            ++i;
            return static_cast<int>(v);
        };
        if (a == "--port")
            port = value(1, 65535);
        else if (a == "--max-rooms")
            limits.maxRooms = value(1, 1000000);
        else if (a == "--max-players")
            limits.maxPlayersPerRoom = value(1, 1000);
        else if (a == "--rate")
            limits.bytesPerSecond = value(1, 1000000) * 1024.0;
        else
            return usage();
    }
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN); // a player gone mid-send is an error to handle, not a reason to stop
#endif

    rynax::relay::Server server(limits);
    std::string error;
    if (!server.start(port, error)) {
        std::fprintf(stderr, "rynax-relay: %s\n", error.c_str());
        return 1;
    }
    std::printf("rynax-relay: listening on port %d\n", server.port());
    std::fflush(stdout);
    auto lastReport = std::chrono::steady_clock::now();
    int lastRooms = -1, lastConnections = -1;
    while (running) {
        server.poll(200);
        auto now = std::chrono::steady_clock::now();
        if (now - lastReport > std::chrono::minutes(1)) {
            lastReport = now;
            if (server.rooms() != lastRooms || server.connections() != lastConnections) {
                lastRooms = server.rooms();
                lastConnections = server.connections();
                std::printf("rynax-relay: %d games, %d connections\n", lastRooms, lastConnections);
                std::fflush(stdout);
            }
        }
    }
    server.stop();
    std::printf("rynax-relay: stopped\n");
    return 0;
}
