#pragma once

// Online multiplayer through a relay: a small server (aven-relay) that games on different networks
// both connect out to, so nobody has to open ports on their router.
//
// The host asks the relay for a room and gets a short code ("KX7P2M") to tell friends; players join
// with the code. The relay only passes bytes along: the host's game is still in charge of everything
// (see network.cpp), exactly as on a local network. It never looks inside the game's messages.
//
// Frames, both ways: a 4-byte length (little-endian, of what follows), a kind (1 byte), a peer
// number (4 bytes) and a payload. Peer numbers are the relay's names for a room's players (1, 2...),
// which the host also uses as their player ids.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace aven::relay {

constexpr int kDefaultPort = 4243;
constexpr uint32_t kMaxPayload = 1u << 20;
constexpr const char* kVersion = "aven-relay-1";

enum Kind : uint8_t {
    Host = 1,     // game -> relay: "1\n<game name>" makes a room
    Room = 2,     // relay -> host: the room's code
    Join = 3,     // game -> relay: "<version>\n<code>\n<game name>"
    Joined = 4,   // relay -> player: in the room (then Data, to and from the host)
    PeerNew = 5,  // relay -> host: a player joined, as `peer`
    Data = 6,     // host -> relay: to `peer`; player -> relay: to the host; relay -> host: from `peer`
    PeerGone = 7, // relay -> host: `peer` left; host -> relay: let `peer` go
    Error = 9,    // relay -> game: why not (then it closes the connection)
};

std::string frame(Kind kind, uint32_t peer, const std::string& payload);
// Takes whole frames off the front of `buffer`. False if the data isn't frames (too big, say).
bool readFrames(std::string& buffer, const std::function<void(Kind, uint32_t, std::string&&)>& onFrame);

struct ServerLimits {
    int maxRooms = 1000;
    int maxPlayersPerRoom = 16;
    int maxConnections = 4000;
    size_t maxQueued = 8u << 20;        // bytes waiting for one slow connection before it's dropped
    double bytesPerSecond = 512 * 1024; // what one connection may send, on average
    double helloSeconds = 10;           // to say Host or Join after connecting
    int wrongCodesPerMinute = 20;       // per address, against guessing codes
};

class Server {
public:
    explicit Server(ServerLimits limits = {});
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool start(int port, std::string& error, bool loopbackOnly = false);
    int port() const; // the one it listens on (useful after start(0))
    // Waits up to `timeoutMs` for something to do, then does it.
    void poll(int timeoutMs);
    void stop();
    int rooms() const;
    int connections() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace aven::relay
