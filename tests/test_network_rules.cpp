// The multiplayer host is in charge: a player (here, a raw connection sending whatever it likes)
// can't move or delete what isn't theirs, can't claim others' objects, can't announce fake players,
// and a player from a different game is turned away.

#include "test_framework.h"

#include "rynax/assets/assets.h"
#include "rynax/core/fs.h"
#include "rynax/core/json.h"
#include "rynax/platform/input.h"
#include "rynax/runtime/game.h"
#include "rynax/runtime/network.h"
#include "rynax/runtime/script_system.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using raw_socket = SOCKET;
static void closeRaw(raw_socket s) { closesocket(s); }
static void nonBlocking(raw_socket s) {
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
}
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using raw_socket = int;
static void closeRaw(raw_socket s) { close(s); }
static void nonBlocking(raw_socket s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
#endif

using namespace rynax;

namespace {

// A connection that speaks Rynax's framing (4-byte little-endian length, then JSON) and nothing else.
struct RawPlayer {
    raw_socket s;
    std::string in;
    bool closed = false;

    explicit RawPlayer(int port) {
        s = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        ::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
        nonBlocking(s);
    }
    ~RawPlayer() { closeRaw(s); }
    void send(const std::string& json) {
        uint32_t n = static_cast<uint32_t>(json.size());
        char head[4] = {static_cast<char>(n & 0xFF), static_cast<char>((n >> 8) & 0xFF), static_cast<char>((n >> 16) & 0xFF),
                        static_cast<char>(n >> 24)};
        std::string all = std::string(head, 4) + json;
        ::send(s, all.data(), static_cast<int>(all.size()), 0);
    }
    // Messages that have arrived.
    std::vector<Json> poll() {
        char buf[4096];
        for (;;) {
            auto got = ::recv(s, buf, static_cast<int>(sizeof buf), 0);
            if (got > 0) {
                in.append(buf, static_cast<size_t>(got));
                continue;
            }
            if (got == 0)
                closed = true;
            break;
        }
        std::vector<Json> out;
        while (in.size() >= 4) {
            uint32_t len = static_cast<uint8_t>(in[0]) | (static_cast<uint8_t>(in[1]) << 8) | (static_cast<uint8_t>(in[2]) << 16) |
                           (static_cast<uint32_t>(static_cast<uint8_t>(in[3])) << 24);
            if (in.size() < 4 + static_cast<size_t>(len))
                break;
            out.push_back(Json::parse(in.substr(4, len)));
            in.erase(0, 4 + static_cast<size_t>(len));
        }
        return out;
    }
};

template <class Step, class Done>
bool pumpFor(Step&& step, Done&& done, double seconds) {
    auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (!done()) {
        if (std::chrono::steady_clock::now() > until)
            return false;
        step();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

} // namespace

RYNAX_TEST(multiplayer_host_only_lets_players_change_their_own_things) {
    namespace stdfs = std::filesystem;
    stdfs::path dir = stdfs::temp_directory_path() / "rynax_net_rules_test";
    std::error_code ec;
    stdfs::remove_all(dir, ec);
    stdfs::create_directories(dir / "prefabs", ec);
    fs::writeText(dir / "prefabs/avatar.prefab",
                  R"({"entities": [{"id": "00000000000000a1", "name": "Avatar", "components": {"Transform": {}, "NetworkSync": {}}}]})");
    Assets assets;
    assets.setRoot(dir);
    Input input;
    Game host(assets, input);
    host.settings().name = "Rules Test";
    auto scene = std::make_unique<Scene>();
    Entity box = scene->create("Box");
    scene->setUUID(box, rynax::UUID{0x1234}); // rynax:: (Windows has a UUID of its own)
    scene->registry().emplace<NetworkSync>(box);
    host.start(std::move(scene), "test.scene");
    std::string error;
    const int port = 45131;
    if (!host.network().host(port, error)) {
        std::printf("  (skipped: %s)\n", error.c_str());
        return;
    }
    Entity hostAvatar = host.network().spawnNetworked("prefabs/avatar.prefab", {1, 0, 0});
    CHECK(static_cast<bool>(hostAvatar));
    uint64_t hostAvatarId = host.scene().registry().get<NetworkSync>(hostAvatar).netId;
    auto frame = [&] { host.update(1.0f / 60.0f); };
    auto avatars = [&] {
        int n = 0;
        host.scene().walk([&](Entity e, int) {
            n += host.scene().info(e).name == "Avatar";
            return true;
        });
        return n;
    };

    // A player from another game: refused, and let go.
    {
        RawPlayer stranger(port);
        stranger.send(R"({"t":"hello","game":"Some Other Game"})");
        bool refused = false;
        pumpFor(frame, [&] {
            for (auto& m : stranger.poll())
                refused |= m["t"].asString("") == "refused";
            return refused && stranger.closed;
        }, 5.0);
        CHECK(refused);
        CHECK(stranger.closed);
        CHECK_EQ(host.network().players().size(), size_t(1)); // just the host (player 0)
    }

    // A player of this game who then tries everything that isn't theirs to do.
    RawPlayer p(port);
    p.send(R"({"t":"hello","game":"Rules Test"})");
    int me = -1;
    pumpFor(frame, [&] {
        for (auto& m : p.poll())
            if (m["t"].asString("") == "welcome")
                me = m["id"].asInt(-1);
        return me > 0;
    }, 5.0);
    CHECK(me > 0);
    if (me <= 0) {
        host.stop();
        return;
    }
    auto netId = [](int owner, int n) { return std::to_string((1ull << 63) | (static_cast<uint64_t>(owner + 1) << 40) | static_cast<uint64_t>(n)); };
    p.send(R"({"t":"hello","game":"Rules Test"})");                     // again: not a second player
    p.send(R"({"t":"joined","id":99})");                                // only the host announces players
    p.send(R"({"t":"left","id":0})");
    p.send(R"({"t":"state","id":"4660","p":[9,9,9]})");                  // the host's scene object (uuid 0x1234)
    p.send(R"({"t":"state","id":")" + netId(0, 1) + R"(","p":[7,7,7]})"); // the host's avatar
    p.send(R"({"t":"destroy","id":")" + std::to_string(hostAvatarId) + R"("})");
    p.send(R"({"t":"spawn","id":")" + netId(0, 77) + R"(","owner":0,"prefab":"prefabs/avatar.prefab","p":[0,0,0]})"); // in the host's range
    p.send(R"({"t":"msg","m":"hi","d":1,"from":0})");                    // pretending to be the host
    for (int i = 0; i < 60; ++i) {
        frame();
        p.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    auto players = host.network().players(); // the host (0) and this player, once each
    CHECK_EQ(players.size(), size_t(2));
    CHECK(players.size() == 2 && players[0] == 0 && players[1] == me);
    CHECK(length(host.scene().transform(host.scene().findByName("Box")).position) < 0.001f);
    CHECK(host.scene().valid(hostAvatar));
    CHECK(length(host.scene().transform(hostAvatar).position - Vec3{1, 0, 0}) < 0.001f);
    CHECK_EQ(avatars(), 1);

    // What is theirs works: spawning in their own range, then moving it.
    p.send(R"({"t":"spawn","id":")" + netId(me, 1) + R"(","owner":0,"prefab":"prefabs/avatar.prefab","p":[0,0,0]})");
    p.send(R"({"t":"state","id":")" + netId(me, 1) + R"(","p":[3,0,0]})");
    pumpFor([&] { frame(); p.poll(); }, [&] { return avatars() == 2; }, 5.0);
    CHECK_EQ(avatars(), 2);
    bool ownedByThem = false;
    host.scene().walk([&](Entity e, int) {
        if (auto* ns = host.scene().registry().tryGet<NetworkSync>(e); ns && host.scene().info(e).name == "Avatar" && e != hostAvatar)
            ownedByThem = ns->owner == me && ns->remote; // the owner is who sent it, whatever the message said
        return true;
    });
    CHECK(ownedByThem);
    host.stop();
}

// A player who stops reading isn't queued for forever: once 8 MB waits for them, they're let go.
RYNAX_TEST(multiplayer_host_lets_go_of_a_player_who_stops_reading) {
    Assets assets;
    Input input;
    Game host(assets, input);
    host.settings().name = "Queue Test";
    host.start(std::make_unique<Scene>(), "test.scene");
    std::string error;
    const int port = 45132;
    if (!host.network().host(port, error)) {
        std::printf("  (skipped: %s)\n", error.c_str());
        return;
    }
    auto frame = [&] { host.update(1.0f / 60.0f); };
    RawPlayer p(port);
    p.send(R"({"t":"hello","game":"Queue Test"})");
    pumpFor(frame, [&] { return host.network().players().size() == 2; }, 5.0);
    CHECK_EQ(host.network().players().size(), size_t(2));
    // From now on the player reads nothing, while the host sends it 256 KB a frame.
    Json big(std::string(256 * 1024, 'x'));
    bool letGo = pumpFor([&] { host.network().send("big", big); frame(); },
                         [&] { return host.network().players().size() == 1; }, 20.0);
    CHECK(letGo);
    host.stop();
}
