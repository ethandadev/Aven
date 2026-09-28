#include "test_framework.h"

#include "aven/assets/assets.h"
#include "aven/core/fs.h"
#include "aven/core/log.h"
#include "aven/platform/input.h"
#include "aven/runtime/game.h"
#include "aven/runtime/network.h"
#include "aven/runtime/net_socket.h"
#include "aven/runtime/relay.h"
#include "aven/runtime/script_system.h"
#include "aven/runtime/systems.h"

#include <cctype>
#include <chrono>
#include <filesystem>
#include <thread>

using namespace aven;

namespace {

struct Messages {
    std::vector<std::string> all;
    int sink;
    Messages() {
        sink = Log::addSink([this](const LogMessage& m) { all.push_back(m.text); });
    }
    ~Messages() { Log::removeSink(sink); }
    bool has(const std::string& text) const {
        for (auto& m : all)
            if (m.find(text) != std::string::npos)
                return true;
        return false;
    }
};

template <typename Step, typename Done>
bool pumpUntil(Step&& step, Done&& done, double seconds = 10.0) {
    auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (!done()) {
        if (std::chrono::steady_clock::now() > until)
            return false;
        step();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

int avatars(Game& g) {
    int n = 0;
    g.scene().walk([&](Entity e, int) {
        n += g.scene().info(e).name == "Avatar";
        return true;
    });
    return n;
}

} // namespace

AVEN_TEST(relay_frames) {
    std::string buffer = relay::frame(relay::Data, 7, "hello") + relay::frame(relay::Room, 0, "KX7P2M");
    buffer += relay::frame(relay::Data, 1, "partial").substr(0, 6); // the rest hasn't arrived
    std::vector<std::string> got;
    CHECK(relay::readFrames(buffer, [&](relay::Kind kind, uint32_t peer, std::string&& payload) {
        got.push_back(std::to_string(kind) + ":" + std::to_string(peer) + ":" + payload);
    }));
    CHECK_EQ(got.size(), size_t(2));
    CHECK_EQ(got[0], std::string("6:7:hello"));
    CHECK_EQ(got[1], std::string("2:0:KX7P2M"));
    CHECK_EQ(buffer.size(), size_t(6)); // kept for later
    std::string huge = "\xff\xff\xff\x7f";
    CHECK(!relay::readFrames(huge, [](relay::Kind, uint32_t, std::string&&) {}));
}

// Two games that can't see each other directly play through a relay: the host gets a room code,
// the player joins with it, and everything works as on a local network.
AVEN_TEST(online_two_games_through_a_relay) {
    relay::Server server;
    std::string error;
    if (!server.start(0, error, true)) {
        std::printf("  (skipped: %s)\n", error.c_str());
        return;
    }
    std::string address = "127.0.0.1:" + std::to_string(server.port());
    namespace stdfs = std::filesystem;
    stdfs::path dir = stdfs::temp_directory_path() / "aven_relay_test";
    std::error_code ec;
    stdfs::remove_all(dir, ec);
    stdfs::create_directories(dir / "scripts", ec);
    stdfs::create_directories(dir / "prefabs", ec);
    fs::writeText(dir / "prefabs/avatar.prefab",
                  R"({"entities": [{"id": "00000000000000a1", "name": "Avatar", "components": {"Transform": {}, "NetworkSync": {}}}]})");
    fs::writeText(dir / "scripts/host.es", "def on_start():\n    game.ok = host_online(\"" + address + "\")\n"
                                           "    spawn_networked(\"prefabs/avatar.prefab\", 1, 0)\n"
                                           "def on_room_ready(code):\n    game.code = code\n"
                                           "def on_player_joined(p):\n    game.joined = p\n    send(\"hi\", 42)\n"
                                           "def on_player_left(p):\n    game.left = p\n");
    Messages log;
    auto makeScene = [](const char* script) {
        auto scene = std::make_unique<Scene>();
        auto& reg = scene->registry();
        reg.emplace<Script>(scene->create("Brain")).path = script;
        Entity box = scene->create("Box");
        scene->setUUID(box, aven::UUID{0x1234});
        reg.emplace<NetworkSync>(box);
        return scene;
    };
    Assets hostAssets, clientAssets;
    hostAssets.setRoot(dir);
    clientAssets.setRoot(dir);
    Input hostInput, clientInput;
    Game host(hostAssets, hostInput), client(clientAssets, clientInput);
    host.start(makeScene("scripts/host.es"), "test.scene");
    host.update(1.0f / 60.0f);
    CHECK(host.scripts().gameValue("ok").truthy());
    bool room = pumpUntil([&] {
        server.poll(0);
        host.update(1.0f / 60.0f);
    }, [&] { return !host.network().roomCode().empty(); });
    CHECK(room);
    if (!room) {
        host.stop();
        return;
    }
    std::string code = host.network().roomCode();
    CHECK_EQ(code.size(), size_t(6));
    CHECK_EQ(host.scripts().gameValue("code").toString(), code);
    CHECK_EQ(server.rooms(), 1);

    // The player types the code (in lower case, with a dash: both fine).
    std::string typed = code.substr(0, 3) + "-" + code.substr(3);
    for (char& c : typed)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    fs::writeText(dir / "scripts/client.es", "def on_start():\n    join_online(\"" + typed + "\", relay=\"" + address + "\")\n"
                                             "def on_connected():\n    game.me = player_id()\n    spawn_networked(\"prefabs/avatar.prefab\", -1, 0)\n"
                                             "def on_receive(m, d, p):\n    game.got = d\n    game.from = p\n"
                                             "def on_disconnected():\n    game.gone = 1\n");
    client.start(makeScene("scripts/client.es"), "test.scene");
    auto frame = [&] {
        server.poll(0);
        host.update(1.0f / 60.0f);
        client.update(1.0f / 60.0f);
    };
    bool connected = pumpUntil(frame, [&] {
        return client.scripts().gameNumber("got") == 42.0 && avatars(host) == 2 && avatars(client) == 2;
    });
    CHECK(connected);
    if (!connected) {
        for (auto& m : log.all)
            std::printf("    %s\n", m.c_str());
        host.stop();
        client.stop();
        return;
    }
    CHECK_EQ(host.scripts().gameNumber("joined"), 1.0);
    CHECK_EQ(client.scripts().gameNumber("me"), 1.0);
    CHECK_EQ(client.scripts().gameNumber("from"), 0.0);
    // The host's Box moves for the player; the player's avatar moves for the host.
    host.scene().transform(host.scene().findByName("Box")).position = {5, 2, 0};
    Entity mine;
    client.scene().walk([&](Entity e, int) {
        if (client.scene().info(e).name == "Avatar" && !isRemote(client.scene().registry(), e))
            mine = e;
        return true;
    });
    CHECK(static_cast<bool>(mine));
    if (mine)
        client.scene().transform(mine).position = {-3, 4, 0};
    auto synced = [&] {
        bool avatarThere = false;
        host.scene().walk([&](Entity e, int) {
            if (host.scene().info(e).name == "Avatar" && isRemote(host.scene().registry(), e))
                avatarThere = length(host.scene().transform(e).position - Vec3{-3, 4, 0}) < 0.05f;
            return true;
        });
        return avatarThere && length(client.scene().transform(client.scene().findByName("Box")).position - Vec3{5, 2, 0}) < 0.05f;
    };
    CHECK(pumpUntil(frame, synced));

    // The player leaves: the host hears it through the relay, and their avatar goes.
    client.stop();
    CHECK(pumpUntil([&] {
        server.poll(0);
        host.update(1.0f / 60.0f);
    }, [&] { return host.scripts().gameNumber("left") == 1.0 && avatars(host) == 1; }));

    // Someone joins again, then the host leaves: the player is told, and the room is gone.
    Game again(clientAssets, clientInput);
    again.start(makeScene("scripts/client.es"), "test.scene");
    CHECK(pumpUntil([&] {
        server.poll(0);
        host.update(1.0f / 60.0f);
        again.update(1.0f / 60.0f);
    }, [&] { return again.network().online(); }));
    CHECK_EQ(again.network().playerId(), 2); // the relay counts on
    host.stop();
    CHECK(pumpUntil([&] {
        server.poll(0);
        again.update(1.0f / 60.0f);
    }, [&] { return again.scripts().gameNumber("gone") == 1.0; }));
    CHECK(log.has("The host left the game."));
    CHECK_EQ(server.rooms(), 0);
    again.stop();
    server.poll(0);
}

// The relay says no, clearly: a wrong code, a different game, guessing codes, and connections
// that never say what they want.
AVEN_TEST(online_relay_refusals) {
    relay::ServerLimits limits;
    limits.wrongCodesPerMinute = 2;
    limits.helloSeconds = 0.3;
    relay::Server server(limits);
    std::string error;
    if (!server.start(0, error, true)) {
        std::printf("  (skipped: %s)\n", error.c_str());
        return;
    }
    std::string address = "127.0.0.1:" + std::to_string(server.port());
    Assets a1, a2;
    Input i1, i2;
    Game host(a1, i1), player(a2, i2);
    host.settings().name = "Space Race";
    host.start(std::make_unique<Scene>(), "test.scene");
    player.start(std::make_unique<Scene>(), "test.scene");
    CHECK(host.network().hostOnline(address, error));
    auto frame = [&] {
        server.poll(0);
        host.update(1.0f / 60.0f);
        player.update(1.0f / 60.0f);
    };
    CHECK(pumpUntil(frame, [&] { return !host.network().roomCode().empty(); }));
    auto tryJoin = [&](const std::string& code, const char* expect) {
        Messages log;
        CHECK(player.network().joinOnline(address, code, error));
        bool refused = pumpUntil(frame, [&] { return log.has(expect); }, 5.0);
        if (!refused)
            for (auto& m : log.all)
                std::printf("    %s\n", m.c_str());
        CHECK(refused);
        pumpUntil(frame, [&] { return !player.network().connecting(); }, 2.0);
        CHECK(!player.network().online());
    };
    player.settings().name = "Space Race";
    tryJoin("ZZZZZZ", "There's no game with the code ZZZZZZ");
    player.settings().name = "Other Game";
    tryJoin(host.network().roomCode(), "a different game");
    player.settings().name = "Space Race";
    tryJoin("YYYYYY", "There's no game with the code YYYYYY");
    tryJoin(host.network().roomCode(), "Too many wrong codes"); // even the right one, for a minute
    CHECK_EQ(host.network().players().size(), size_t(1));

    // No relay set: a clear message, not a hang.
    std::string noRelay;
    CHECK(!player.network().joinOnline("", "ABCDEF", noRelay));
    CHECK(noRelay.find("Project Settings") != std::string::npos);

    // A connection that never says Host or Join is let go.
    std::string connectError;
    socket_t idle = net::connectTo("127.0.0.1", server.port(), connectError);
    int before = -1;
    pumpUntil([&] { server.poll(0); }, [&] { return (before = server.connections()) >= 2; }, 2.0);
    CHECK(before >= 2);
    CHECK(pumpUntil([&] { server.poll(5); }, [&] { return server.connections() == 1; }, 3.0)); // only the host
    if (idle != INVALID_SOCKET)
        AVEN_CLOSE(idle);
    host.stop();
    player.stop();
}
