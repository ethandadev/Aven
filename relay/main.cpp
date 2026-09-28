// aven-relay: lets Aven games on different networks play together. Hosts get a room code from it,
// players join with the code, and it passes their messages along. See docs/online-multiplayer.md.
//
//   aven-relay [--port 4243] [--max-rooms 1000] [--max-players 16] [--rate 512]

#include "aven/runtime/relay.h"

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
    std::printf("aven-relay: the server for Aven's online multiplayer.\n\n"
                "  --port N          where to listen (default %d, TCP)\n"
                "  --max-rooms N     games at once (default 1000)\n"
                "  --max-players N   players in one game, besides the host (default 16)\n"
                "  --rate KB         what one player may send each second, on average (default 512)\n",
                aven::relay::kDefaultPort);
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    int port = aven::relay::kDefaultPort;
    aven::relay::ServerLimits limits;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&]() -> int {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a number after it.\n", a.c_str());
                std::exit(usage());
            }
            return std::atoi(argv[++i]);
        };
        if (a == "--port")
            port = value();
        else if (a == "--max-rooms")
            limits.maxRooms = value();
        else if (a == "--max-players")
            limits.maxPlayersPerRoom = value();
        else if (a == "--rate")
            limits.bytesPerSecond = value() * 1024.0;
        else
            return usage();
    }
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    aven::relay::Server server(limits);
    std::string error;
    if (!server.start(port, error)) {
        std::fprintf(stderr, "aven-relay: %s\n", error.c_str());
        return 1;
    }
    std::printf("aven-relay: listening on port %d\n", server.port());
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
                std::printf("aven-relay: %d games, %d connections\n", lastRooms, lastConnections);
                std::fflush(stdout);
            }
        }
    }
    server.stop();
    std::printf("aven-relay: stopped\n");
    return 0;
}
