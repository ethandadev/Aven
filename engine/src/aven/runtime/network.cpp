#include "aven/runtime/network.h"

#include "aven/core/log.h"
#include "aven/runtime/game.h"
#include "aven/runtime/relay.h"
#include "aven/runtime/script_system.h"
#include "aven/runtime/systems.h"
#include "aven/scene/scene.h"
#include "aven/script/vm.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <unordered_map>

#include "aven/runtime/net_socket.h"

namespace aven {

bool isRemote(const Registry& reg, Entity e) {
    const NetworkSync* ns = reg.tryGet<NetworkSync>(e);
    return ns && ns->remote;
}

#if defined(__EMSCRIPTEN__)

// Browsers can't open these connections: everything says so.
struct Network::Impl {};
Network::Network(Game&) : impl_(std::make_unique<Impl>()) {}
Network::~Network() = default;
bool Network::host(int, std::string& error) {
    error = "Multiplayer on the local network works in the desktop version of a game, not in web builds.";
    return false;
}
bool Network::join(const std::string&, int, std::string& error) { return host(0, error); }
bool Network::hostOnline(const std::string&, std::string& error) { return host(0, error); }
bool Network::joinOnline(const std::string&, const std::string&, std::string& error) { return host(0, error); }
std::string Network::roomCode() const { return ""; }
void Network::leave() {}
bool Network::online() const { return false; }
bool Network::isHost() const { return false; }
bool Network::connecting() const { return false; }
int Network::playerId() const { return -1; }
std::vector<int> Network::players() const { return {}; }
void Network::send(const std::string&, const Json&) {}
void Network::findGames() {}
Entity Network::spawnNetworked(const std::string&, Vec3) { return {}; }
void Network::onDestroy(Entity) {}
void Network::update(float) {}
void Network::sceneStarted() {}
void Network::stop() {}
std::string Network::localAddress() { return "127.0.0.1"; }

#else

namespace {

using net::noDelay;
using net::setNonBlocking;
using net::socketsReady;
using net::wouldBlock;

Json vec3Json(Vec3 v) {
    Json a = Json::array();
    a.push(v.x);
    a.push(v.y);
    a.push(v.z);
    return a;
}

Vec3 jsonVec3(const Json& j, Vec3 fallback = {}) {
    if (j.size() < 3)
        return fallback;
    return {j[0].asFloat(0), j[1].asFloat(0), j[2].asFloat(0)};
}

} // namespace

struct Network::Impl {
    Game& game;
    explicit Impl(Game& g) : game(g) {}

    struct Peer {
        socket_t s = INVALID_SOCKET;
        std::string in, out;
        int id = -1;
        bool welcomed = false;
        bool drop = false; // close after sending what's queued (refused, or not keeping up)
        bool gone = false; // through the relay: it said this player left
        bool virtualPeer() const { return s == INVALID_SOCKET; }
    };
    // Online: everything goes through one connection to the relay (relay.h). Players are then
    // peers without a socket of their own; their bytes travel in the relay's Data frames.
    struct RelayLink {
        socket_t s = INVALID_SOCKET;
        std::string in, out, code, address;
    } relay;
    socket_t listener = INVALID_SOCKET; // host: new players connect here
    socket_t discovery = INVALID_SOCKET; // host: answers "anyone hosting?"
    socket_t finder = INVALID_SOCKET;    // looking for games
    float findTime = 0;
    std::vector<Peer> peers;             // host: every player; player: just the host
    bool hosting = false, connected = false, pending = false;
    float connectTime = 0;
    int myId = -1, nextId = 1, port = kDefaultPort;
    std::vector<int> playerList;
    uint64_t spawnCounter = 0;
    std::unordered_map<uint64_t, float> sendTimers;

    // --- messages: a 4-byte length, then JSON
    // What may wait to be sent to one player: one who stops reading is let go, rather than
    // queueing up memory for them forever.
    static constexpr size_t kMaxQueued = 8u << 20;
    void queue(Peer& p, const Json& msg) {
        if (p.drop)
            return;
        std::string body = msg.dump();
        if (p.out.size() + body.size() > kMaxQueued) {
            Log::warn("Multiplayer: player ", p.id, " isn't keeping up; letting them go.");
            p.drop = true;
            return;
        }
        uint32_t n = static_cast<uint32_t>(body.size());
        char head[4] = {static_cast<char>(n & 0xFF), static_cast<char>((n >> 8) & 0xFF), static_cast<char>((n >> 16) & 0xFF),
                        static_cast<char>((n >> 24) & 0xFF)};
        p.out.append(head, 4);
        p.out += body;
    }
    // To everyone this player talks to, except `skip` (a player id).
    void broadcast(const Json& msg, int skip = -2) {
        for (auto& p : peers)
            if (p.id != skip && (p.welcomed || !hosting))
                queue(p, msg);
    }
    void flush(Peer& p) {
        if (p.virtualPeer()) {
            for (size_t at = 0; at < p.out.size(); at += relay::kMaxPayload)
                relay.out += relay::frame(relay::Data, hosting ? static_cast<uint32_t>(p.id) : 0, p.out.substr(at, relay::kMaxPayload));
            p.out.clear();
            return;
        }
        while (!p.out.empty()) {
            auto n = ::send(p.s, p.out.data(), static_cast<int>(std::min<size_t>(p.out.size(), 1 << 16)), AVEN_NOSIGNAL);
            if (n <= 0)
                return; // full for now (or gone: the read notices)
            p.out.erase(0, static_cast<size_t>(n));
        }
    }
    // Reads what's arrived; false when the connection is gone.
    bool receive(Peer& p, std::vector<Json>& messages) {
        char buf[16384];
        for (;;) {
            if (p.virtualPeer())
                break; // (the relay link filled p.in)
            auto n = ::recv(p.s, buf, static_cast<int>(sizeof buf), 0);
            if (n > 0) {
                p.in.append(buf, static_cast<size_t>(n));
                continue;
            }
            if (n == 0)
                return false;
            if (!wouldBlock())
                return false;
            break;
        }
        while (p.in.size() >= 4) {
            uint32_t len = static_cast<uint8_t>(p.in[0]) | (static_cast<uint8_t>(p.in[1]) << 8) |
                           (static_cast<uint8_t>(p.in[2]) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(p.in[3])) << 24);
            if (len > (16u << 20))
                return false; // not an Aven game
            if (p.in.size() < 4 + static_cast<size_t>(len))
                break;
            messages.push_back(Json::parse(p.in.substr(4, len)));
            p.in.erase(0, 4 + static_cast<size_t>(len));
        }
        return !p.gone;
    }

    // The relay connection: what it says, and sending what's waiting. False when it's gone.
    bool pumpRelay() {
        char buf[16384];
        bool open = true;
        for (;;) {
            auto n = ::recv(relay.s, buf, static_cast<int>(sizeof buf), 0);
            if (n > 0) {
                relay.in.append(buf, static_cast<size_t>(n));
                continue;
            }
            if (n == 0 || !wouldBlock())
                open = false; // closed: but first, what it said before closing (why, say)
            break;
        }
        bool refused = false;
        bool framed = relay::readFrames(relay.in, [&](relay::Kind kind, uint32_t peer, std::string&& payload) {
            if (refused)
                return;
            int id = static_cast<int>(peer);
            switch (kind) {
            case relay::Room:
                relay.code = payload;
                Log::info("Online game ready. Room code: ", payload);
                event("on_room_ready", {script::Value(payload)});
                break;
            case relay::PeerNew:
                if (hosting && id > 0 && !peerById(id)) {
                    Peer p;
                    p.id = id;
                    peers.push_back(std::move(p));
                }
                break;
            case relay::PeerGone:
                if (Peer* p = peerById(id))
                    p->gone = true;
                break;
            case relay::Data:
                if (hosting) {
                    if (Peer* p = peerById(id); p && !p->drop)
                        p->in += payload;
                } else if (!peers.empty()) {
                    peers[0].in += payload;
                }
                break;
            case relay::Error:
                Log::warn("Online: ", payload);
                refused = true;
                break;
            default:
                break;
            }
        });
        return open && framed && !refused;
    }
    bool flushRelay() {
        while (!relay.out.empty()) {
            auto n = ::send(relay.s, relay.out.data(), static_cast<int>(std::min<size_t>(relay.out.size(), 1 << 16)), AVEN_NOSIGNAL);
            if (n <= 0)
                return n == 0 || wouldBlock();
            relay.out.erase(0, static_cast<size_t>(n));
        }
        return true;
    }

    void closeAll() {
        for (auto& p : peers)
            if (p.s != INVALID_SOCKET)
                AVEN_CLOSE(p.s);
        peers.clear();
        if (relay.s != INVALID_SOCKET) {
            flushRelay(); // (a goodbye, if there's room)
            AVEN_CLOSE(relay.s);
        }
        relay = {};
        for (socket_t* s : {&listener, &discovery})
            if (*s != INVALID_SOCKET) {
                AVEN_CLOSE(*s);
                *s = INVALID_SOCKET;
            }
        hosting = connected = pending = false;
        myId = -1;
        playerList.clear();
        sendTimers.clear();
    }

    // --- script events
    void event(const char* name, std::vector<script::Value> args = {}) { game.scripts().callEvent(name, std::move(args)); }

    // --- networked objects
    std::unordered_map<uint64_t, Entity> objects() {
        std::unordered_map<uint64_t, Entity> map;
        Scene& scene = game.scene();
        auto& reg = scene.registry();
        for (Entity e : reg.entitiesWith<NetworkSync>()) {
            auto& ns = reg.get<NetworkSync>(e);
            if (!ns.netId) // scene objects: the same on every computer, since everyone loads the same scene
                ns.netId = scene.info(e).uuid.value;
            map[ns.netId] = e;
        }
        return map;
    }

    // Another player's object: it follows the network, not this keyboard.
    void makeRemote(Entity e) {
        auto& reg = game.scene().registry();
        auto& ns = reg.get<NetworkSync>(e);
        if (ns.remote)
            return;
        ns.remote = true;
        reg.remove<PlatformerController>(e);
        reg.remove<TopDownController>(e);
        reg.remove<FollowMouse>(e);
        reg.remove<Draggable>(e);
        reg.remove<Shooter>(e);
        if (auto* rb = reg.tryGet<RigidBody2D>(e); rb && rb->type == BodyType::Dynamic) {
            rb->type = BodyType::Kinematic;
            game.physics2D().refresh(e);
        }
        if (auto* rb = reg.tryGet<RigidBody>(e); rb && rb->type == BodyType::Dynamic) {
            rb->type = BodyType::Kinematic;
            game.physics3D().refresh(e);
        }
    }

    // Which objects are whose, once this player knows its id.
    void claimSceneObjects() {
        auto& reg = game.scene().registry();
        for (auto& [id, e] : objects()) {
            auto& ns = reg.get<NetworkSync>(e);
            if (ns.prefab.empty() && ns.owner == 0 && myId != 0)
                makeRemote(e);
        }
    }

    Json stateOf(Entity e) {
        Scene& scene = game.scene();
        auto& reg = scene.registry();
        auto& ns = reg.get<NetworkSync>(e);
        Json m = Json::object();
        m["t"] = "state";
        m["id"] = std::to_string(ns.netId);
        Transform& t = scene.transform(e);
        if (ns.position)
            m["p"] = vec3Json(t.position);
        if (ns.rotation)
            m["r"] = vec3Json(t.rotation);
        if (ns.scale)
            m["s"] = vec3Json(t.scale);
        if (ns.look) {
            if (auto* sr = reg.tryGet<SpriteRenderer>(e)) {
                m["f"] = sr->frame;
                m["x"] = sr->flipX;
            }
            m["v"] = !reg.has<Hidden>(e);
        }
        return m;
    }

    Json spawnOf(Entity e) {
        auto& ns = game.scene().registry().get<NetworkSync>(e);
        Json m = Json::object();
        m["t"] = "spawn";
        m["id"] = std::to_string(ns.netId);
        m["owner"] = ns.owner;
        m["prefab"] = ns.prefab;
        m["p"] = vec3Json(game.scene().transform(e).position);
        return m;
    }

    void applyState(const Json& m, std::unordered_map<uint64_t, Entity>& objs) {
        uint64_t id = std::strtoull(m["id"].asString("0").c_str(), nullptr, 10);
        auto it = objs.find(id);
        if (it == objs.end())
            return;
        Entity e = it->second;
        auto& reg = game.scene().registry();
        auto& ns = reg.get<NetworkSync>(e);
        if (!ns.remote)
            return; // ours: we say where it is
        Transform& t = game.scene().transform(e);
        ns.target = jsonVec3(m["p"], t.position);
        ns.targetRotation = jsonVec3(m["r"], t.rotation);
        ns.targetScale = jsonVec3(m["s"], t.scale);
        if (!ns.hasTarget) { // first update: jump straight there
            t.position = ns.target;
            t.rotation = ns.targetRotation;
            t.scale = ns.targetScale;
        }
        ns.hasTarget = true;
        if (auto* sr = reg.tryGet<SpriteRenderer>(e)) {
            if (m.contains("f"))
                sr->frame = m["f"].asInt(sr->frame);
            if (m.contains("x"))
                sr->flipX = m["x"].asBool(sr->flipX);
        }
        if (m.contains("v")) {
            bool visible = m["v"].asBool(true);
            if (visible && reg.has<Hidden>(e))
                reg.remove<Hidden>(e);
            else if (!visible && !reg.has<Hidden>(e))
                reg.emplace<Hidden>(e);
        }
    }

    Entity spawnLocal(const Json& m) {
        uint64_t id = std::strtoull(m["id"].asString("0").c_str(), nullptr, 10);
        if (objects().count(id))
            return {};
        std::string prefab = m["prefab"].asString("");
        Entity e = game.spawnPrefab(prefab, jsonVec3(m["p"]));
        if (!e)
            return {};
        auto& ns = game.scene().registry().getOrEmplace<NetworkSync>(e);
        ns.netId = id;
        ns.owner = m["owner"].asInt(0);
        ns.prefab = prefab;
        if (ns.owner != myId)
            makeRemote(e);
        return e;
    }

    void destroyLocal(uint64_t id) {
        auto objs = objects();
        auto it = objs.find(id);
        if (it != objs.end()) {
            game.scene().registry().get<NetworkSync>(it->second).netId = 0; // no echo
            game.destroyEntity(it->second);
        }
    }

    // The host is in charge: players only get to change what's theirs, and only the host says who
    // joined or left. A player trusts the host, whose messages are all it hears.
    Peer* peerById(int id) {
        for (auto& p : peers)
            if (p.id == id)
                return &p;
        return nullptr;
    }
    // The player a networked spawn's id says made it (see spawnNetworked), or -1 for scene objects.
    static int idOwner(uint64_t netId) {
        if (!(netId >> 63))
            return -1;
        return static_cast<int>((netId >> 40) & ((1u << 23) - 1)) - 1;
    }
    void addPlayer(int id) {
        if (std::find(playerList.begin(), playerList.end(), id) == playerList.end())
            playerList.push_back(id);
    }

    void handle(const Json& m, int from, std::unordered_map<uint64_t, Entity>& objs) {
        std::string t = m["t"].asString("");
        if (hosting)
            hostHandle(t, m, from, objs);
        else
            playerHandle(t, m, objs);
    }

    void hostHandle(const std::string& t, const Json& m, int from, std::unordered_map<uint64_t, Entity>& objs) {
        Peer* peer = peerById(from);
        if (!peer)
            return;
        if (t == "hello") {
            if (peer->welcomed)
                return; // said hello already
            if (m["game"].asString("") != game.settings().name) {
                Json no = Json::object();
                no["t"] = "refused";
                no["why"] = "that's " + game.settings().name + ", a different game";
                queue(*peer, no);
                peer->drop = true;
                return;
            }
            // A new player: tell them who they are, who's here and what's been made.
            Json w = Json::object();
            w["t"] = "welcome";
            w["id"] = from;
            Json list = Json::array();
            for (int id : playerList)
                list.push(id);
            list.push(from);
            w["players"] = list;
            queue(*peer, w);
            for (auto& [id, e] : objs) {
                auto& ns = game.scene().registry().get<NetworkSync>(e);
                if (!ns.prefab.empty())
                    queue(*peer, spawnOf(e));
                queue(*peer, stateOf(e));
            }
            Json joined = Json::object();
            joined["t"] = "joined";
            joined["id"] = from;
            broadcast(joined, from);
            peer->welcomed = true;
            addPlayer(from);
            event("on_player_joined", {script::Value(from)});
            return;
        }
        if (!peer->welcomed)
            return; // nothing before hello
        uint64_t id = std::strtoull(m["id"].asString("0").c_str(), nullptr, 10);
        auto owned = [&] { // an object this player made
            auto it = objs.find(id);
            return it != objs.end() && game.scene().registry().get<NetworkSync>(it->second).owner == from;
        };
        Json relay = m;
        relay["from"] = from; // whatever the message says
        if (t == "msg") {
            broadcast(relay, from);
            event("on_receive", {script::Value(relay["m"].asString("")), script::VM::fromJson(relay["d"]), script::Value(from)});
        } else if (t == "spawn") {
            if (idOwner(id) != from || objs.count(id))
                return; // only new objects, with ids from this player's own range
            relay["owner"] = from;
            if (Entity e = spawnLocal(relay)) {
                objs[id] = e;
                broadcast(relay, from);
            }
        } else if (t == "state") {
            if (!owned())
                return; // not theirs to move (scene objects are the host's)
            applyState(relay, objs);
            broadcast(relay, from);
        } else if (t == "destroy") {
            if (!owned() || idOwner(id) != from)
                return; // only what they spawned
            destroyLocal(id);
            objs = objects();
            broadcast(relay, from);
        }
        // "welcome", "joined" and "left" come from the host only: from a player, they're ignored.
    }

    void playerHandle(const std::string& t, const Json& m, std::unordered_map<uint64_t, Entity>& objs) {
        if (t == "welcome") {
            if (connected)
                return;
            myId = m["id"].asInt(-1);
            playerList.clear();
            for (auto& id : m["players"].elements())
                addPlayer(id.asInt(0));
            connected = true;
            pending = false;
            claimSceneObjects();
            event("on_connected");
        } else if (t == "refused") {
            Log::warn("Couldn't join the game: ", m["why"].asString("the host said no"), ".");
            if (!peers.empty())
                peers[0].drop = true;
        } else if (t == "joined") {
            int id = m["id"].asInt(0);
            if (std::find(playerList.begin(), playerList.end(), id) != playerList.end())
                return;
            addPlayer(id);
            event("on_player_joined", {script::Value(id)});
        } else if (t == "left") {
            playerLeft(m["id"].asInt(0));
        } else if (t == "msg") {
            event("on_receive", {script::Value(m["m"].asString("")), script::VM::fromJson(m["d"]), script::Value(m["from"].asInt(0))});
        } else if (t == "spawn") {
            if (Entity e = spawnLocal(m))
                objs[game.scene().registry().get<NetworkSync>(e).netId] = e;
        } else if (t == "state") {
            applyState(m, objs);
        } else if (t == "destroy") {
            destroyLocal(std::strtoull(m["id"].asString("0").c_str(), nullptr, 10));
            objs = objects();
        }
    }

    void playerLeft(int id) {
        std::erase(playerList, id);
        // What they made goes with them.
        auto& reg = game.scene().registry();
        for (auto& [nid, e] : objects()) {
            auto& ns = reg.get<NetworkSync>(e);
            if (!ns.prefab.empty() && ns.owner == id) {
                ns.netId = 0;
                game.destroyEntity(e);
            }
        }
        event("on_player_left", {script::Value(id)});
    }
};

Network::Network(Game& game) : impl_(std::make_unique<Impl>(game)) {}

Network::~Network() { impl_->closeAll(); }

bool Network::host(int port, std::string& error) {
    socketsReady();
    leave();
    Impl& n = *impl_;
    n.listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (n.listener == INVALID_SOCKET) {
        error = "Couldn't open a network connection.";
        return false;
    }
    int yes = 1;
    setsockopt(n.listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof yes);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (::bind(n.listener, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || ::listen(n.listener, 16) != 0) {
        AVEN_CLOSE(n.listener);
        n.listener = INVALID_SOCKET;
        error = "Port " + std::to_string(port) + " is busy (is another game already hosting on this computer?).";
        return false;
    }
    setNonBlocking(n.listener);
    // Answer games looking for a host.
    n.discovery = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (n.discovery != INVALID_SOCKET) {
        setsockopt(n.discovery, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof yes);
        sockaddr_in d{};
        d.sin_family = AF_INET;
        d.sin_addr.s_addr = htonl(INADDR_ANY);
        d.sin_port = htons(static_cast<uint16_t>(port + 1));
        if (::bind(n.discovery, reinterpret_cast<sockaddr*>(&d), sizeof d) != 0) {
            AVEN_CLOSE(n.discovery);
            n.discovery = INVALID_SOCKET;
        } else {
            setNonBlocking(n.discovery);
        }
    }
    n.port = port;
    n.hosting = true;
    n.connected = true;
    n.myId = 0;
    n.nextId = 1;
    n.playerList = {0};
    Log::info("Hosting a network game on ", localAddress(), " (port ", port, ")");
    return true;
}

bool Network::join(const std::string& address, int port, std::string& error) {
    socketsReady();
    leave();
    Impl& n = *impl_;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, address.c_str(), &addr.sin_addr) != 1) {
        error = "\"" + address + "\" isn't an address. It looks like 192.168.1.20 (the host shows it).";
        return false;
    }
    socket_t s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) {
        error = "Couldn't open a network connection.";
        return false;
    }
    setNonBlocking(s);
    noDelay(s);
    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 && !wouldBlock()) {
        AVEN_CLOSE(s);
        error = "Couldn't reach " + address + ".";
        return false;
    }
    Impl::Peer host;
    host.s = s;
    host.id = 0;
    host.welcomed = true;
    Json hello = Json::object();
    hello["t"] = "hello";
    hello["game"] = n.game.settings().name;
    n.queue(host, hello);
    n.peers.push_back(std::move(host));
    n.pending = true;
    n.connectTime = 0;
    n.port = port;
    return true;
}

bool Network::hostOnline(const std::string& relayAddress, std::string& error) {
    socketsReady();
    leave();
    Impl& n = *impl_;
    std::string address = relayAddress.empty() ? n.game.settings().relay : relayAddress;
    std::string host;
    int port = relay::kDefaultPort;
    net::splitAddress(address, host, port, relay::kDefaultPort);
    if (host.empty()) {
        error = "There's no relay to host through. Set one in Project Settings > Game > Online relay, or pass "
                "its address: host_online(\"relay.example.com\").";
        return false;
    }
    socket_t s = net::connectTo(host, port, error);
    if (s == INVALID_SOCKET)
        return false;
    n.relay.s = s;
    n.relay.address = address;
    n.relay.out = relay::frame(relay::Host, 0, std::string(relay::kVersion) + "\n" + n.game.settings().name);
    n.hosting = true;
    n.connected = true;
    n.myId = 0;
    n.playerList = {0};
    Log::info("Asking the relay (", address, ") for a room...");
    return true;
}

bool Network::joinOnline(const std::string& relayAddress, const std::string& code, std::string& error) {
    socketsReady();
    leave();
    Impl& n = *impl_;
    std::string address = relayAddress.empty() ? n.game.settings().relay : relayAddress;
    std::string host;
    int port = relay::kDefaultPort;
    net::splitAddress(address, host, port, relay::kDefaultPort);
    if (host.empty()) {
        error = "There's no relay to join through. Set one in Project Settings > Game > Online relay, or pass "
                "its address: join_online(code, relay=\"relay.example.com\").";
        return false;
    }
    std::string trimmed;
    for (char c : code)
        if (!std::isspace(static_cast<unsigned char>(c)) && c != '-')
            trimmed += c;
    if (trimmed.empty()) {
        error = "join_online() needs the room code the host sees.";
        return false;
    }
    socket_t s = net::connectTo(host, port, error);
    if (s == INVALID_SOCKET)
        return false;
    n.relay.s = s;
    n.relay.address = address;
    n.relay.out = relay::frame(relay::Join, 0, std::string(relay::kVersion) + "\n" + trimmed + "\n" + n.game.settings().name);
    Impl::Peer hostPeer; // no socket: through the relay
    hostPeer.id = 0;
    hostPeer.welcomed = true;
    Json hello = Json::object();
    hello["t"] = "hello";
    hello["game"] = n.game.settings().name;
    n.queue(hostPeer, hello);
    n.peers.push_back(std::move(hostPeer));
    n.pending = true;
    n.connectTime = 0;
    return true;
}

std::string Network::roomCode() const { return impl_->relay.code; }

void Network::leave() {
    Impl& n = *impl_;
    bool was = n.connected;
    n.closeAll();
    // Other players' objects go; ours stay ours.
    auto& reg = n.game.scene().registry();
    auto list = reg.entitiesWith<NetworkSync>();
    for (Entity e : list) {
        auto& ns = reg.get<NetworkSync>(e);
        if (ns.remote && !ns.prefab.empty()) {
            ns.netId = 0;
            n.game.destroyEntity(e);
        }
    }
    if (was)
        n.event("on_disconnected");
}

bool Network::online() const { return impl_->connected; }
bool Network::isHost() const { return impl_->hosting; }
bool Network::connecting() const { return impl_->pending; }
int Network::playerId() const { return impl_->connected ? impl_->myId : -1; }
std::vector<int> Network::players() const { return impl_->playerList; }

void Network::send(const std::string& message, const Json& data) {
    Impl& n = *impl_;
    if (!n.connected)
        return;
    Json m = Json::object();
    m["t"] = "msg";
    m["m"] = message;
    m["d"] = data;
    m["from"] = n.myId;
    n.broadcast(m);
}

void Network::findGames() {
    socketsReady();
    Impl& n = *impl_;
    found_.clear();
    if (n.finder == INVALID_SOCKET) {
        n.finder = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (n.finder == INVALID_SOCKET)
            return;
        int yes = 1;
        setsockopt(n.finder, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof yes);
        setNonBlocking(n.finder);
    }
    const char ask[] = "AVEN?";
    for (const char* to : {"255.255.255.255", "127.0.0.1"}) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(kDefaultPort + 1));
        inet_pton(AF_INET, to, &addr.sin_addr);
        ::sendto(n.finder, ask, static_cast<int>(sizeof ask - 1), 0, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    }
    n.findTime = 3.0f;
}

Entity Network::spawnNetworked(const std::string& prefab, Vec3 position) {
    Impl& n = *impl_;
    Entity e = n.game.spawnPrefab(prefab, position);
    if (!e)
        return {};
    auto& ns = n.game.scene().registry().getOrEmplace<NetworkSync>(e);
    ns.owner = n.connected ? n.myId : 0;
    ns.prefab = prefab;
    // Unique for everyone: who made it, and how many they've made.
    ns.netId = (static_cast<uint64_t>(ns.owner + 1) << 40) | (++n.spawnCounter) | (1ull << 63);
    if (n.connected)
        n.broadcast(n.spawnOf(e));
    return e;
}

void Network::onDestroy(Entity e) {
    Impl& n = *impl_;
    auto* ns = n.game.scene().registry().tryGet<NetworkSync>(e);
    if (!n.connected || !ns || !ns->netId || ns->remote)
        return;
    Json m = Json::object();
    m["t"] = "destroy";
    m["id"] = std::to_string(ns->netId);
    n.broadcast(m);
}

void Network::update(float dt) {
    Impl& n = *impl_;
    // Games answering "anyone hosting?".
    if (n.finder != INVALID_SOCKET) {
        char buf[512];
        sockaddr_in from{};
        socklen_t len = sizeof from;
        for (;;) {
            auto got = ::recvfrom(n.finder, buf, static_cast<int>(sizeof buf - 1), 0, reinterpret_cast<sockaddr*>(&from), &len);
            if (got <= 0)
                break;
            buf[got] = 0;
            std::string reply(buf);
            if (reply.rfind("AVEN!", 0) != 0)
                continue;
            size_t bar = reply.find('|');
            char ip[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
            FoundGame g;
            g.address = std::string(ip) == "127.0.0.1" ? localAddress() : ip;
            g.port = std::atoi(reply.substr(5, bar == std::string::npos ? std::string::npos : bar - 5).c_str());
            g.name = bar == std::string::npos ? "Game" : reply.substr(bar + 1);
            if (std::none_of(found_.begin(), found_.end(), [&](const FoundGame& f) { return f.address == g.address && f.port == g.port; }))
                found_.push_back(g);
        }
        if ((n.findTime -= dt) <= 0) {
            AVEN_CLOSE(n.finder);
            n.finder = INVALID_SOCKET;
        }
    }
    if (n.discovery != INVALID_SOCKET) {
        char buf[64];
        sockaddr_in from{};
        socklen_t len = sizeof from;
        for (;;) {
            auto got = ::recvfrom(n.discovery, buf, static_cast<int>(sizeof buf), 0, reinterpret_cast<sockaddr*>(&from), &len);
            if (got <= 0)
                break;
            if (std::string(buf, static_cast<size_t>(got)) != "AVEN?")
                continue;
            std::string reply = "AVEN!" + std::to_string(n.port) + "|" + n.game.settings().name;
            ::sendto(n.discovery, reply.data(), static_cast<int>(reply.size()), 0, reinterpret_cast<sockaddr*>(&from), len);
        }
    }
    if (n.relay.s != INVALID_SOCKET && (!n.pumpRelay() || n.relay.out.size() > (32u << 20))) {
        bool wasHost = n.hosting, wasPending = n.pending;
        std::string where = n.relay.address;
        leave();
        if (wasHost)
            Log::warn("Lost the connection to the relay (", where, "), so the online game ended.");
        else if (wasPending)
            Log::warn("Couldn't join the online game through ", where, ".");
        else
            Log::info("The online game ended.");
        return;
    }
    if (!n.hosting && n.peers.empty())
        return;

    // New players.
    if (n.listener != INVALID_SOCKET)
        for (;;) {
            socket_t s = ::accept(n.listener, nullptr, nullptr);
            if (s == INVALID_SOCKET)
                break;
            setNonBlocking(s);
            noDelay(s);
            Impl::Peer p;
            p.s = s;
            p.id = n.nextId++;
            n.peers.push_back(std::move(p));
        }

    // Messages in.
    auto objs = n.objects();
    std::vector<int> gone;
    for (size_t i = 0; i < n.peers.size(); ++i) {
        std::vector<Json> messages;
        bool alive = n.receive(n.peers[i], messages);
        int id = n.peers[i].id;
        for (auto& m : messages)
            n.handle(m, id, objs);
        if (!alive)
            gone.push_back(id);
    }
    for (auto& p : n.peers)
        if (p.drop && std::find(gone.begin(), gone.end(), p.id) == gone.end()) {
            n.flush(p); // e.g. why they were refused
            gone.push_back(p.id);
        }
    if (n.pending && (n.connectTime += dt) > (n.relay.s != INVALID_SOCKET ? 15.0f : 6.0f))
        gone.push_back(0);
    for (int id : gone) {
        auto it = std::find_if(n.peers.begin(), n.peers.end(), [&](const Impl::Peer& p) { return p.id == id; });
        if (it == n.peers.end())
            continue;
        bool welcomed = it->welcomed;
        if (!it->virtualPeer())
            AVEN_CLOSE(it->s);
        else if (n.hosting && !it->gone)
            n.relay.out += relay::frame(relay::PeerGone, static_cast<uint32_t>(id), ""); // the relay lets them go
        n.peers.erase(it);
        if (n.hosting) {
            if (welcomed) {
                Json left = Json::object();
                left["t"] = "left";
                left["id"] = id;
                n.broadcast(left);
                n.playerLeft(id);
            }
        } else {
            // The host went away (or never answered).
            bool wasPending = n.pending;
            leave();
            if (wasPending)
                Log::warn("Couldn't join the game: nobody answered. Check the address, and that both are on the same network.");
            else
                Log::info("The host left the game.");
            return;
        }
    }
    objs = n.objects();

    // Other players' objects glide to where they were last seen.
    auto& reg = n.game.scene().registry();
    float k = std::min(1.0f, dt * 15.0f);
    for (auto& [id, e] : objs) {
        auto& ns = reg.get<NetworkSync>(e);
        if (!ns.remote || !ns.hasTarget)
            continue;
        Transform& t = n.game.scene().transform(e);
        t.position += (ns.target - t.position) * k;
        t.rotation += (ns.targetRotation - t.rotation) * k;
        t.scale += (ns.targetScale - t.scale) * k;
    }

    // Where our objects are, a few times a second.
    if (n.connected)
        for (auto& [id, e] : objs) {
            auto& ns = reg.get<NetworkSync>(e);
            if (ns.remote || !n.game.scene().isActive(e))
                continue;
            float& timer = n.sendTimers[id];
            timer -= dt;
            if (timer > 0)
                continue;
            timer = 1.0f / std::clamp(ns.sendRate, 1.0f, 60.0f);
            n.broadcast(n.stateOf(e));
        }
    for (auto& p : n.peers)
        n.flush(p);
    if (n.relay.s != INVALID_SOCKET)
        n.flushRelay();
}

void Network::sceneStarted() {
    impl_->sendTimers.clear();
    if (impl_->connected)
        impl_->claimSceneObjects();
}

void Network::stop() {
    impl_->closeAll();
    if (impl_->finder != INVALID_SOCKET) {
        AVEN_CLOSE(impl_->finder);
        impl_->finder = INVALID_SOCKET;
    }
    found_.clear();
}

std::string Network::localAddress() {
    socketsReady();
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
    AVEN_CLOSE(s);
    return ip;
}

#endif

} // namespace aven
