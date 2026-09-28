#pragma once

// Multiplayer on the local network (the same Wi-Fi or cable): one game hosts, others join.
//
// The host keeps a TCP connection to every player and passes messages on, so everyone hears
// everyone. Games are found with a UDP broadcast. Scripts:
//   host_game(), join_game(address), find_games(), send(message, data), leave_game()
//   on_player_joined(player), on_player_left(player), on_receive(message, data, player),
//   on_connected(), on_disconnected()
// Objects with a NetworkSync component keep their position (and look) the same for everyone:
// scene objects belong to the host, spawn_networked() objects to whoever made them. The other
// players' copies don't read this computer's keyboard; they follow what their owner sends.
//
// Online (over the internet): the same, through a relay server that both sides connect out to
// (relay.h), with a room code instead of an address: host_online(), join_online(code), room_code(),
// on_room_ready(code).
//
// Not available in web builds: browsers can't open network connections like this.

#include "aven/core/json.h"
#include "aven/ecs/registry.h"
#include "aven/math/math.h"

#include <memory>
#include <string>
#include <vector>

namespace aven {

class Game;

class Network {
public:
    static constexpr int kDefaultPort = 4242; // TCP; discovery answers on the next port (UDP)

    explicit Network(Game& game);
    ~Network();

    bool host(int port, std::string& error);
    bool join(const std::string& address, int port, std::string& error);
    // Online, through a relay server (relay.h): hosting gets a room code to share; joining uses it.
    // `relay` is "host:port" (empty: the project's setting).
    bool hostOnline(const std::string& relay, std::string& error);
    bool joinOnline(const std::string& relay, const std::string& code, std::string& error);
    std::string roomCode() const; // hosting online: the code, once the relay has given one
    void leave();
    bool online() const;    // hosting, or connected to a host
    bool isHost() const;
    bool connecting() const; // joined, waiting for the host to say hello
    int playerId() const;   // 0 = the host; everyone else counts up; -1 = not online
    std::vector<int> players() const; // everyone, including this one

    // A message for everyone else (the host passes it on).
    void send(const std::string& message, const Json& data);

    // Looks for games on the local network for a couple of seconds.
    void findGames();
    struct FoundGame {
        std::string name, address;
        int port = kDefaultPort;
    };
    const std::vector<FoundGame>& gamesFound() const { return found_; }

    // A prefab copied to every player, belonging to this one.
    Entity spawnNetworked(const std::string& prefab, Vec3 position);
    void onDestroy(Entity e); // a networked object this player owns was destroyed: everyone's copy goes

    void update(float dt); // call every frame: messages in, then positions out
    void sceneStarted();   // a new scene: whose objects are whose again
    void stop();           // the game stopped

    static std::string localAddress(); // this computer's address on the local network

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<FoundGame> found_;
};

// Whether this copy of an object is another player's (it follows the network, not the keyboard).
bool isRemote(const Registry& reg, Entity e);

} // namespace aven
