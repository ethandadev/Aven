// Game recipes: "I want to make a platformer" -> pick a hero, things to collect, dangers
// and a goal, and Aven cooks a small working game out of behaviors, with a card that
// explains every part and suggests what to try changing.

#include "editor.h"

#include "aven/core/fs.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

namespace aven::editor {

namespace {

// ---------------------------------------------------------------- recipe definitions

enum Danger : unsigned { Spikes = 1, Walkers = 2, Chasers = 4, Falling = 8 };

struct RecipeDef {
    const char* id;
    const char* name;
    const char* pitch;
    uint32_t color;
    const char* icon; // shape drawn on the card
    bool collect;
    unsigned dangers;        // allowed
    unsigned defaultDangers;
    std::vector<int> goals;  // allowed Editor::RecipeGoal values
};

const std::vector<RecipeDef>& recipes() {
    using G = Editor::RecipeGoal;
    static const std::vector<RecipeDef> list = {
        {"platformer", "Platformer", "Run, jump over gaps, grab coins and reach the flag.", 0x38BDF8, "Circle", true,
         Spikes | Walkers | Chasers | Falling, Spikes | Walkers, {G::ReachExit, G::CollectAll, G::Survive, G::ReachScore}},
        {"adventure", "Top-down adventure", "Explore a field, collect gems and dodge slimes to find the portal.", 0x4ADE80,
         "Diamond", true, Spikes | Walkers | Chasers, Walkers | Chasers, {G::CollectAll, G::ReachExit, G::Survive, G::ReachScore}},
        {"shooter", "Arena shooter", "Enemies pour in from the corners. Aim with the mouse, click to shoot.", 0xF87171,
         "Triangle", false, Chasers, Chasers, {G::ReachScore, G::Survive}},
        {"dodge", "Dodge", "Things fall from the sky. Stay alive and catch stars.", 0xFBBF24, "Star", true, Falling, Falling,
         {G::Survive, G::ReachScore}},
        {"clicker", "Clicker", "Click the cookie, buy ovens, and bake a mountain of cookies.", 0xD97706, "Circle", false, 0, 0,
         {G::ReachScore}},
    };
    return list;
}

struct CollectDef {
    const char* name;
    const char* shape;
    uint32_t color;
    const char* tag;
    const char* counter;
    const char* label;
};

const CollectDef kCollects[] = {
    {"Nothing", "", 0, "", "", ""},
    {"Coins", "Circle", 0xFDE047, "coin", "coins", "Coins"},
    {"Gems", "Diamond", 0x67E8F9, "gem", "gems", "Gems"},
    {"Stars", "Star", 0xFACC15, "star", "stars", "Stars"},
};

const char* kHeroShapes[] = {"Circle", "RoundedSquare", "Triangle", "Star", "Heart", "Diamond"};
const char* kDifficulty[] = {"Easy", "Normal", "Hard"};

const char* goalName(int g) {
    switch (g) {
    case Editor::RecipeGoal::CollectAll: return "Collect everything";
    case Editor::RecipeGoal::ReachExit: return "Reach the finish";
    case Editor::RecipeGoal::Survive: return "Survive the timer";
    case Editor::RecipeGoal::ReachScore: return "Reach a score";
    }
    return "";
}

// ---------------------------------------------------------------- JSON helpers

Json arr(std::initializer_list<double> values) {
    Json a = Json::array();
    for (double v : values)
        a.push(Json(v));
    return a;
}

Json rgba(Color c) { return arr({c.r, c.g, c.b, c.a}); }
Json rgba(uint32_t hex, float alpha = 1) { return rgba(Color::fromHex(hex, alpha)); }

Json obj(std::initializer_list<std::pair<const char*, Json>> members) {
    Json o = Json::object();
    for (auto& [k, v] : members)
        o[k] = v;
    return o;
}

Json at(double x, double y, double z = 0) { return obj({{"position", arr({x, y, z})}}); }
Json sprite(const std::string& shape, uint32_t color, double w, double h, int order, float alpha = 1) {
    return obj({{"shape", shape}, {"color", rgba(color, alpha)}, {"size", arr({w, h})}, {"order", order}});
}
Json box(double w, double h, bool trigger = false) {
    Json b = obj({{"size", arr({w, h})}});
    if (trigger)
        b["is_trigger"] = true;
    return b;
}
Json circle(double r, bool trigger = false) {
    Json c = obj({{"radius", r}});
    if (trigger)
        c["is_trigger"] = true;
    return c;
}
Json body(double gravity, bool fixed = true) { return obj({{"gravity_scale", gravity}, {"fixed_rotation", fixed}}); }

// Builds a scene document entity by entity.
class SceneBuilder {
public:
    explicit SceneBuilder(const std::string& name) {
        doc_ = obj({{"aven", "scene"}, {"version", 1}, {"name", name}});
        doc_["entities"] = Json::array();
        std::random_device rd;
        base_ = (static_cast<uint64_t>(rd()) << 32 | rd()) & 0x00FFFFFFFFFF0000ull;
    }
    std::string id() {
        char buf[20];
        std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(base_ + ++next_));
        return buf;
    }
    std::string add(const std::string& name, Json components, const std::string& tag = "", const std::string& parent = "",
                    std::string useId = "") {
        std::string eid = useId.empty() ? id() : useId;
        Json e = obj({{"id", eid}, {"name", name}});
        if (!tag.empty())
            e["tag"] = tag;
        if (!parent.empty())
            e["parent"] = parent;
        e["components"] = std::move(components);
        doc_["entities"].push(std::move(e));
        return eid;
    }
    const Json& doc() const { return doc_; }

private:
    Json doc_;
    uint64_t base_ = 0;
    uint64_t next_ = 0;
};

Json prefabDoc(const std::string& name, const std::string& tag, Json components) {
    Json e = obj({{"id", "00000000000000a1"}, {"name", name}});
    if (!tag.empty())
        e["tag"] = tag;
    e["components"] = std::move(components);
    Json doc = Json::object();
    doc["entities"] = Json::array();
    doc["entities"].push(std::move(e));
    return doc;
}

Json uiText(const char* anchor, double ox, double oy, double w, double h, const std::string& text, int size, const char* align,
            uint32_t color = 0xFFFFFF) {
    return obj({{"UIElement", obj({{"anchor", anchor}, {"offset", arr({ox, oy})}, {"size", arr({w, h})}})},
                {"UIText", obj({{"text", text}, {"font_size", size}, {"align", align}, {"color", rgba(color)}})}});
}

} // namespace

// ---------------------------------------------------------------- cooking

namespace {

struct Cooking {
    Editor& ed;
    const Editor::RecipeChoices& c;
    const RecipeDef& def;
    SceneBuilder scene;
    Editor::RecipeCard card;
    std::vector<std::pair<std::string, std::string>> files; // path -> contents
    std::string slug;
    float hard = 1; // enemy speed scale
    std::string heroId;
    int collectibles = 0;

    Cooking(Editor& e, const Editor::RecipeChoices& choices, const RecipeDef& d, const std::string& s)
        : ed(e), c(choices), def(d), scene(choices.name), slug(s) {
        hard = c.difficulty == 0 ? 0.75f : c.difficulty == 2 ? 1.3f : 1.0f;
    }

    const CollectDef& collect() const { return kCollects[def.collect ? c.collect : 0]; }
    int heroHealth() const { return c.difficulty == 0 ? 5 : c.difficulty == 2 ? 1 : 3; }
    int surviveSeconds() const { return c.difficulty == 0 ? 20 : c.difficulty == 2 ? 45 : 30; }
    bool has(unsigned danger) const { return (c.dangers & def.dangers & danger) != 0; }

    std::string path(const char* folder, const std::string& base, const char* ext) {
        std::string p = std::string(folder) + "/" + slug + "_" + base + ext;
        for (auto& f : files)
            if (f.first == p)
                return p;
        std::error_code ec;
        int n = 2;
        while (stdfs::exists(ed.projectDir() / p, ec))
            p = std::string(folder) + "/" + slug + "_" + base + "_" + std::to_string(n++) + ext;
        return p;
    }

    void part(const std::string& object, const std::string& what, const std::string& tryThis) {
        card.parts.push_back({object, what, tryThis});
    }

    Json heroSprite(double size) const {
        if (!c.heroImage.empty())
            return obj({{"texture", c.heroImage}, {"size", arr({size, size})}, {"order", 5}});
        Color col = c.heroColor;
        return obj({{"shape", c.heroShape}, {"color", rgba(col)}, {"size", arr({size, size})}, {"order", 5}});
    }

    Json health(int hp, const char* whenZero = "RestartScene", const char* counter = "health") const {
        return obj({{"max_health", hp}, {"when_zero", whenZero}, {"counter", counter}});
    }

    void camera(double x, double y, double size, uint32_t background, bool follow) {
        Json comps = obj({{"Transform", at(x, y, 10)}, {"Camera", obj({{"size", size}, {"background", rgba(background)}})}});
        if (follow)
            comps["CameraFollow"] = obj({{"target", heroId}, {"offset", arr({0, 1, 0})}, {"smoothness", 4}, {"follow_z", false}});
        scene.add("Camera", comps);
        if (follow)
            part("Camera", "CameraFollow keeps the hero in the middle of the screen as it moves.",
                 "Change Smoothness: lower numbers make the camera lag behind more.");
    }

    void collectible(double x, double y, int i) {
        const CollectDef& k = collect();
        if (!*k.shape)
            return;
        Json comps = obj({{"Transform", at(x, y)},
                          {"SpriteRenderer", sprite(k.shape, k.color, 0.6, 0.6, 3)},
                          {"CircleCollider2D", circle(0.3, true)},
                          {"Collectible", obj({{"counter", k.counter}, {"amount", 1}})},
                          {"Bob", obj({{"height", 0.12}, {"speed", 0.8 + 0.05 * (i % 5)}})}});
        scene.add(std::string(k.label).substr(0, std::string(k.label).size() - 1), comps, k.tag);
        ++collectibles;
    }

    void hud(bool healthShown) {
        const CollectDef& k = collect();
        if (*k.counter) {
            std::string format = std::string(k.label) + ": {}";
            if (c.goal == Editor::RecipeGoal::CollectAll)
                format += " / " + std::to_string(collectibles);
            Json comps = uiText("TopLeft", 24, -20, 360, 56, std::string(k.label) + ": 0", 40, "Left");
            comps["ScoreDisplay"] = obj({{"counter", k.counter}, {"format", format}});
            scene.add(std::string(k.label) + " Counter", comps);
            part(std::string(k.label) + " Counter",
                 std::string("ScoreDisplay shows game.") + k.counter + " on screen. Collectibles add to it when you pick them up.",
                 "Change its Format, for example to \"Loot: {}\".");
        }
        if (healthShown) {
            Json comps = uiText("TopRight", -24, -20, 300, 56, "Health: 3", 40, "Right", 0xFCA5A5);
            comps["ScoreDisplay"] = obj({{"counter", "health"}, {"format", "Health: {}"}});
            scene.add("Health Counter", comps);
        }
    }

    // The goal: a win message plus whatever checks for the win.
    void goal(double exitX, double exitY, const char* exitShape, uint32_t exitColor) {
        const CollectDef& k = collect();
        std::string check;
        std::string winPath = path("scripts", "win", ".es");
        switch (c.goal) {
        case Editor::RecipeGoal::CollectAll:
            check = "    if not self.visible and count(\"" + std::string(k.tag) + "\") == 0:\n        self.visible = True\n";
            part("You Win", std::string("Its script (") + winPath + ") counts the " + k.label +
                                " still in the level with count(\"" + k.tag + "\"). At zero, the message appears.",
                 "Hide one more collectible somewhere hard to reach.");
            break;
        case Editor::RecipeGoal::ReachScore: {
            int target = def.id == std::string("clicker") ? (c.difficulty == 0 ? 50 : c.difficulty == 2 ? 300 : 120)
                         : def.id == std::string("shooter") ? (c.difficulty == 0 ? 10 : c.difficulty == 2 ? 30 : 20)
                                                             : std::max(3, collectibles - 2);
            std::string counter = def.id == std::string("clicker") ? "cookies" : def.id == std::string("shooter") ? "score" : k.counter;
            if (def.id == std::string("dodge"))
                target = c.difficulty == 0 ? 5 : c.difficulty == 2 ? 15 : 10;
            check = "    if not self.visible and get_game(\"" + counter + "\", 0) >= target:\n        self.visible = True\n";
            check = "target = " + std::to_string(target) + "  # the score that wins\n\n" + check; // moved to the top below
            part("You Win", "Its script (" + winPath + ") watches game." + counter + " and shows the message at " +
                                std::to_string(target) + ".",
                 "Open " + winPath + " and change target.");
            break;
        }
        case Editor::RecipeGoal::Survive: {
            std::string timerPath = path("scripts", "timer", ".es");
            files.push_back({timerPath, "# Counts down. When it reaches zero, you win.\n\n"
                                        "seconds = " + std::to_string(surviveSeconds()) + "  # how long to survive\n\n"
                                        "_left = 0\n_done = False\n\n"
                                        "def on_start():\n    _left = seconds\n\n"
                                        "def on_update(dt):\n    if _done:\n        return\n    _left -= dt\n"
                                        "    self.text = f\"Survive: {ceil(_left)}\"\n"
                                        "    if _left <= 0:\n        _done = True\n        self.text = \"Survived!\"\n"
                                        "        broadcast(\"win\")\n"});
            Json comps = uiText("Top", 0, -20, 400, 56, "Survive: 30", 44, "Center", 0xFDE68A);
            comps["Script"] = obj({{"path", timerPath}});
            scene.add("Timer", comps);
            part("Timer", "A short EasyScript (" + timerPath + ") counts down every frame with on_update(dt). At zero it "
                          "broadcasts \"win\" to every script.",
                 "Change seconds in the Inspector (the Script section) to make it shorter or longer.");
            break;
        }
        case Editor::RecipeGoal::ReachExit: {
            std::string goalPath = path("scripts", "finish", ".es");
            files.push_back({goalPath, "# The finish: touching it wins the game.\n\n"
                                       "def on_trigger(other):\n    if other.tag == \"player\":\n        broadcast(\"win\")\n"});
            Json comps = obj({{"Transform", at(exitX, exitY)},
                              {"SpriteRenderer", sprite(exitShape, exitColor, 1.6, 1.6, 2)},
                              {"CircleCollider2D", circle(0.7, true)},
                              {"Spin", obj({{"speed", arr({0, 0, 60})}})},
                              {"Script", obj({{"path", goalPath}})}});
            scene.add("Finish", comps);
            part("Finish", "Touching it runs " + goalPath + ": on_trigger checks it's the player, then broadcasts \"win\".",
                 "Drag the Finish somewhere harder to reach.");
            break;
        }
        }
        std::string top;
        size_t split = check.find("\n\n");
        if (c.goal == Editor::RecipeGoal::ReachScore && split != std::string::npos) {
            top = check.substr(0, split + 2);
            check = check.substr(split + 2);
        }
        files.push_back({winPath, "# The message that appears when you win. Press R to play again.\n\n" + top +
                                      "def on_start():\n    self.visible = False\n\n"
                                      "def on_message(message, data):\n    if message == \"win\":\n        self.visible = True\n\n"
                                      "def on_update(dt):\n" + check +
                                      "    if self.visible and key_pressed(\"r\"):\n        restart_scene()\n"});
        Json win = uiText("Center", 0, 40, 900, 200, "You win!\nPress R to play again", 72, "Center", 0xFDE047);
        win["Script"] = obj({{"path", winPath}});
        scene.add("You Win", win);
    }

    // ---------------------------------------------------------------- the recipes

    void platformer() {
        heroId = scene.id();
        camera(-6, 0, 6, 0x8FD3FF, true);
        // Scenery.
        for (auto [x, y, r] : {std::tuple{-10.0, -7.0, 12.0}, {8.0, -8.0, 14.0}, {26.0, -7.0, 12.0}, {44.0, -8.0, 14.0}})
            scene.add("Hill", obj({{"Transform", at(x, y)}, {"SpriteRenderer", sprite("Circle", 0x6CC56C, r, r, -20)}}));
        for (auto [x, y] : {std::pair{-4.0, 5.0}, {12.0, 6.0}, {30.0, 5.5}, {46.0, 6.0}})
            scene.add("Cloud", obj({{"Transform", at(x, y)}, {"SpriteRenderer", sprite("Circle", 0xFFFFFF, 2.2, 1.4, -15, 0.9f)}}));
        // Ground with gaps, and platforms.
        for (auto [x, w] : {std::pair{-2.0, 22.0}, {15.0, 10.0}, {29.0, 12.0}, {43.0, 10.0}}) {
            std::string g = scene.add("Ground", obj({{"Transform", at(x, -4)}, {"SpriteRenderer", sprite("Square", 0x9A6B3F, w, 2, 0)},
                                                     {"BoxCollider2D", obj({{"size", arr({w, 2})}, {"friction", 0.6}})}}));
            scene.add("Grass", obj({{"Transform", at(0, 0.9)}, {"SpriteRenderer", sprite("Square", 0x5BBF5B, w, 0.3, 1)}}), "", g);
        }
        for (auto [x, y, w] : {std::tuple{-5.0, -0.5, 3.0}, {1.0, 1.0, 3.0}, {6.0, -0.5, 2.5}, {15.0, 1.0, 3.0}, {21.5, 0.5, 2.5},
                               {27.0, 1.5, 3.0}, {33.0, 3.0, 3.0}, {41.0, 1.0, 3.0}})
            scene.add("Platform", obj({{"Transform", at(x, y)}, {"SpriteRenderer", sprite("RoundedSquare", 0xB07A4A, w, 0.5, 0)},
                                       {"BoxCollider2D", obj({{"size", arr({w, 0.5})}, {"friction", 0.6}})}}));
        part("Ground and Platform", "Plain sprites with a BoxCollider2D, so things can stand on them.",
             "Select a Platform, press Ctrl+D to copy it, and drag the copy somewhere new.");

        // The hero.
        Json hero = obj({{"Transform", at(-10, -2)},
                         {"SpriteRenderer", heroSprite(0.9)},
                         {"RigidBody2D", body(2.5)},
                         {"BoxCollider2D", obj({{"size", arr({0.75, 0.85})}, {"friction", 0}})},
                         {"PlatformerController", obj({{"speed", 6}, {"jump_power", c.difficulty == 0 ? 13 : 12}})},
                         {"Health", health(heroHealth())}});
        scene.add("Hero", hero, "player", "", heroId);
        part("Hero", "PlatformerController runs with the arrow keys or A/D and jumps with Space. RigidBody2D adds gravity, and "
                     "Health gives " + std::to_string(heroHealth()) + " hits before the level restarts.",
             "Set Jump Power to 16 for a floaty jump, or Extra Jumps to 1 for a double jump.");

        int i = 0;
        for (auto [x, y] : {std::pair{-7.0, -2.3}, {-5.0, 0.6}, {1.0, 2.1}, {3.0, -2.3}, {6.0, 0.6}, {12.0, -2.3}, {15.0, 2.1},
                            {21.5, 1.6}, {25.0, -2.3}, {27.0, 2.6}, {33.0, 4.1}, {41.0, 2.1}})
            collectible(x, y, i++);
        if (collectibles)
            part(std::string(collect().label).substr(0, std::strlen(collect().label) - 1),
                 std::string("Collectible adds 1 to game.") + collect().counter + " when the hero touches it, then disappears. Bob "
                     "makes it float up and down.",
                 "Set Amount to 5 on one of them to make a super " + std::string(collect().tag) + ".");

        int count = c.difficulty == 0 ? 2 : c.difficulty == 2 ? 4 : 3;
        if (has(Spikes)) {
            const double xs[] = {-1.0, 12.8, 30.5, 45.0};
            for (int k = 0; k < count; ++k)
                scene.add("Spikes", obj({{"Transform", at(xs[k], -2.7)}, {"SpriteRenderer", sprite("Triangle", 0x9CA3AF, 1, 0.7, 2)},
                                         {"BoxCollider2D", box(0.8, 0.5, true)}, {"Hazard", obj({{"damage", 1}})}}),
                          "hazard");
            part("Spikes", "Hazard takes 1 health from whatever tagged 'player' touches it, and knocks it back.",
                 "Raise Knockback to 12 for a big bounce.");
        }
        if (has(Walkers)) {
            const double xs[] = {4.0, 17.0, 31.0, 44.0};
            for (int k = 0; k < count; ++k)
                scene.add("Walker", obj({{"Transform", at(xs[k], -2.4)}, {"SpriteRenderer", sprite("RoundedSquare", 0xA855F7, 0.9, 0.8, 4)},
                                         {"RigidBody2D", body(2)}, {"BoxCollider2D", box(0.85, 0.75)},
                                         {"Patrol", obj({{"distance", 2}, {"speed", 2 * hard}})}, {"Hazard", obj({{"damage", 1}})}}),
                          "enemy");
            part("Walker", "Patrol walks back and forth, and Hazard hurts the hero on contact.",
                 "Make one faster: set Patrol Speed to 4.");
        }
        if (has(Chasers)) {
            const double xs[] = {26.0, 40.0};
            for (int k = 0; k < (c.difficulty == 2 ? 2 : 1); ++k)
                scene.add("Chaser", obj({{"Transform", at(xs[k], -2.4)}, {"SpriteRenderer", sprite("Circle", 0xEF4444, 0.8, 0.8, 4)},
                                         {"RigidBody2D", body(2)}, {"CircleCollider2D", circle(0.4)},
                                         {"Chase", obj({{"speed", 2.5 * hard}, {"sight", 6}})}, {"Hazard", obj({{"damage", 1}})}}),
                          "enemy");
            part("Chaser", "Chase moves toward anything tagged 'player' that comes within Sight.", "Try Run Away: it will flee instead.");
        }
        if (has(Falling))
            rocks(18, 10, 22);
        goal(46, -1.8, "Star", 0xFDE047);
        hud(true);
        card.summary = "A side-on platformer. Run with the arrow keys, jump with Space.";
    }

    void rocks(double x, double y, double range) {
        std::string rockPath = path("prefabs", "rock", ".prefab");
        Json rock = obj({{"Transform", at(0, 0)},
                         {"SpriteRenderer", sprite("Circle", 0x78716C, 0.8, 0.8, 6)},
                         {"RigidBody2D", obj({{"gravity_scale", 0.6 * hard}})},
                         {"CircleCollider2D", circle(0.35, true)},
                         {"Hazard", obj({{"damage", 1}, {"knockback", 3}})},
                         {"Lifetime", obj({{"seconds", 5}, {"fade_out", true}})}});
        files.push_back({rockPath, prefabDoc("Rock", "hazard", rock).dump(2)});
        scene.add("Rock Maker", obj({{"Transform", at(x, y)},
                                    {"Spawner", obj({{"prefab", rockPath},
                                                     {"interval", c.difficulty == 0 ? 1.8 : c.difficulty == 2 ? 0.7 : 1.1},
                                                     {"max_alive", 14},
                                                     {"random_range", arr({range, 0})}})}}));
        part("Rock Maker", "An invisible Spawner that drops copies of " + rockPath + " from the sky. Each rock has Hazard, "
                           "and Lifetime removes it after a few seconds.",
             "Lower Interval for a rock storm, or open the rock prefab and make rocks bigger.");
    }

    void arenaWalls(double w, double h, uint32_t color) {
        struct W {
            double x, y, sw, sh;
        };
        for (W wall : {W{0, h / 2 + 0.5, w + 2, 1}, W{0, -h / 2 - 0.5, w + 2, 1}, W{-w / 2 - 0.5, 0, 1, h}, W{w / 2 + 0.5, 0, 1, h}})
            scene.add("Wall", obj({{"Transform", at(wall.x, wall.y)}, {"SpriteRenderer", sprite("Square", color, wall.sw, wall.sh, 0)},
                                   {"BoxCollider2D", box(wall.sw, wall.sh)}}));
    }

    void adventure() {
        heroId = scene.id();
        camera(-12, -6, 7, 0x14532D, true);
        scene.add("Grass", obj({{"Transform", at(0, 0)}, {"SpriteRenderer", sprite("Square", 0x4ADE80, 32, 20, -20)}}));
        arenaWalls(32, 20, 0x166534);
        for (auto [x, y] : {std::pair{-6.0, 4.0}, {-9.0, -2.0}, {2.0, -5.0}, {5.0, 5.0}, {10.0, -1.0}, {-2.0, 1.0}, {12.0, -7.0}})
            scene.add("Tree", obj({{"Transform", at(x, y)}, {"SpriteRenderer", sprite("Circle", 0x15803D, 2.2, 2.2, 1)},
                                   {"CircleCollider2D", circle(0.9)}}));
        part("Tree and Wall", "Colliders with no RigidBody2D never move, so they block the way.",
             "Copy a Tree with Ctrl+D to make a maze.");
        Json hero = obj({{"Transform", at(-13, -7)},
                         {"SpriteRenderer", heroSprite(0.9)},
                         {"RigidBody2D", body(0)},
                         {"CircleCollider2D", circle(0.4)},
                         {"TopDownController", obj({{"speed", 5}})},
                         {"Health", health(heroHealth())}});
        scene.add("Hero", hero, "player", "", heroId);
        part("Hero", "TopDownController moves in four directions with the arrow keys or WASD. Gravity Scale is 0 because this "
                     "game is seen from above.",
             "Set Speed to 8, or turn on Face Movement so the hero turns where it walks.");
        int i = 0;
        for (auto [x, y] : {std::pair{-10.0, 6.0}, {-4.0, -7.0}, {-1.0, 6.0}, {3.0, -1.0}, {7.0, 7.5}, {8.0, -6.0}, {13.0, 2.0},
                            {-13.0, 2.0}, {0.0, -8.0}, {-7.0, 0.0}, {10.0, 4.0}, {14.0, -4.0}})
            collectible(x, y, i++);
        if (collectibles)
            part(std::string(collect().label).substr(0, std::strlen(collect().label) - 1),
                 std::string("Collectible adds 1 to game.") + collect().counter + " when the hero touches it.",
                 "Change the Counter name on one of them to start a second kind of treasure.");
        int count = c.difficulty == 0 ? 2 : c.difficulty == 2 ? 5 : 3;
        if (has(Spikes)) {
            const double pos[][2] = {{-3, -3}, {6, 1}, {11, 6}, {-11, -5}, {1, 8}};
            for (int k = 0; k < count; ++k)
                scene.add("Trap", obj({{"Transform", at(pos[k][0], pos[k][1])}, {"SpriteRenderer", sprite("Diamond", 0x6B7280, 0.9, 0.9, 1)},
                                       {"BoxCollider2D", box(0.7, 0.7, true)}, {"Hazard", obj({{"damage", 1}, {"knockback", 8}})}}),
                          "hazard");
            part("Trap", "Hazard: stepping on it costs 1 health and pushes the hero away.", "Set Damage to 2 on one trap.");
        }
        if (has(Walkers)) {
            const double pos[][3] = {{-6, -4, 0}, {4, 3, 1}, {9, -3, 0}, {-12, 4, 1}, {12, 5, 1}};
            for (int k = 0; k < count; ++k)
                scene.add("Slime", obj({{"Transform", at(pos[k][0], pos[k][1])}, {"SpriteRenderer", sprite("RoundedSquare", 0xA855F7, 0.9, 0.8, 4)},
                                        {"RigidBody2D", body(0)}, {"CircleCollider2D", circle(0.42)},
                                        {"Patrol", obj({{"axis", pos[k][2] > 0 ? "Y" : "X"}, {"distance", 2.5}, {"speed", 2 * hard}})},
                                        {"Hazard", obj({{"damage", 1}})}}),
                          "enemy");
            part("Slime", "Patrol walks back and forth along one axis (some go sideways, some up and down). Hazard hurts.",
                 "Change a slime's Axis from X to Y.");
        }
        if (has(Chasers)) {
            const double pos[][2] = {{6, -8}, {-2, 7}, {14, 7}};
            for (int k = 0; k < (c.difficulty == 0 ? 1 : c.difficulty == 2 ? 3 : 2); ++k)
                scene.add("Hunter", obj({{"Transform", at(pos[k][0], pos[k][1])}, {"SpriteRenderer", sprite("Circle", 0xEF4444, 0.8, 0.8, 4)},
                                         {"RigidBody2D", body(0)}, {"CircleCollider2D", circle(0.4)},
                                         {"Chase", obj({{"speed", 2.4 * hard}, {"sight", 6}})}, {"Hazard", obj({{"damage", 1}})}}),
                          "enemy");
            part("Hunter", "Chase heads for the hero once it's within Sight (6 units).", "Lower Sight to 3 to make hunters sleepier.");
        }
        goal(13, 7, "Circle", 0xA855F7);
        hud(true);
        card.summary = "A top-down adventure seen from above. Move with the arrow keys or WASD.";
    }

    void shooter() {
        heroId = scene.id();
        camera(0, 0, 9.5, 0x0F172A, false);
        scene.add("Floor", obj({{"Transform", at(0, 0)}, {"SpriteRenderer", sprite("Square", 0x1E293B, 30, 18, -20)}}));
        arenaWalls(30, 18, 0x475569);
        std::string bulletPath = path("prefabs", "bullet", ".prefab");
        files.push_back({bulletPath, prefabDoc("Bullet", "bullet",
                                               obj({{"Transform", at(0, 0)},
                                                    {"SpriteRenderer", sprite("Circle", 0xFDE047, 0.25, 0.25, 6)},
                                                    {"RigidBody2D", obj({{"gravity_scale", 0}, {"fixed_rotation", true}, {"continuous", true}})},
                                                    {"CircleCollider2D", circle(0.12, true)},
                                                    {"Hazard", obj({{"victim_tag", "enemy"}, {"damage", 1}, {"knockback", 3}, {"vanish_on_hit", true}})},
                                                    {"Lifetime", obj({{"seconds", 2}, {"fade_out", false}})}}))
                                         .dump(2)});
        std::string enemyScript = path("scripts", "enemy", ".es");
        files.push_back({enemyScript, "# Runs when this enemy is destroyed: one more point.\n\n"
                                      "def on_destroy():\n    game.score = get_game(\"score\", 0) + 1\n"});
        std::string enemyPath = path("prefabs", "enemy", ".prefab");
        files.push_back({enemyPath, prefabDoc("Enemy", "enemy",
                                              obj({{"Transform", at(0, 0)},
                                                   {"SpriteRenderer", sprite("Circle", 0xEF4444, 0.8, 0.8, 4)},
                                                   {"RigidBody2D", body(0)},
                                                   {"CircleCollider2D", circle(0.38)},
                                                   {"Chase", obj({{"speed", 2.2 * hard}, {"sight", 40}})},
                                                   {"Hazard", obj({{"damage", 1}, {"knockback", 5}})},
                                                   {"Health", health(c.difficulty == 2 ? 2 : 1, "Destroy", "")},
                                                   {"Script", obj({{"path", enemyScript}})}}))
                                        .dump(2)});
        Json hero = obj({{"Transform", at(0, 0)},
                         {"SpriteRenderer", heroSprite(0.9)},
                         {"RigidBody2D", body(0)},
                         {"CircleCollider2D", circle(0.4)},
                         {"TopDownController", obj({{"speed", 6}})},
                         {"Shooter", obj({{"prefab", bulletPath}, {"action", "mouse_left"}, {"bullet_speed", 16}, {"cooldown", 0.18}, {"aim", "Mouse"},
                                          {"offset", arr({0, 0})}})},
                         {"Health", health(heroHealth())}});
        scene.add("Hero", hero, "player", "", heroId);
        part("Hero", "TopDownController moves with WASD. Shooter fires " + bulletPath + " toward the mouse while the left "
                     "button is held.",
             "Lower Cooldown to 0.08 for a machine gun, or raise Bullet Speed.");
        part("Bullet (prefab)", "Each bullet has Hazard with Victim Tag 'enemy' and Vanish On Hit, so it hurts enemies and "
                                "disappears. Lifetime removes bullets that miss.",
             "Open the prefab (double-click it in Assets) and make bullets bigger.");
        for (auto [x, y] : {std::pair{-13.0, 7.0}, {13.0, 7.0}, {-13.0, -7.0}, {13.0, -7.0}})
            scene.add("Enemy Door", obj({{"Transform", at(x, y)}, {"SpriteRenderer", sprite("Circle", 0x7F1D1D, 1.4, 1.4, -5)},
                                         {"Spawner", obj({{"prefab", enemyPath}, {"interval", c.difficulty == 0 ? 3.5 : c.difficulty == 2 ? 1.6 : 2.5},
                                                          {"max_alive", c.difficulty == 2 ? 5 : 3}, {"random_range", arr({0.5, 0.5})}})}}));
        part("Enemy Door", "A Spawner that keeps making copies of " + enemyPath + ". Enemies Chase the hero and have Health; "
                           "their script adds a point when they're destroyed.",
             "Set Max Alive to 8 on one door for chaos.");
        Json score = uiText("TopLeft", 24, -20, 360, 56, "Score: 0", 40, "Left");
        score["ScoreDisplay"] = obj({{"counter", "score"}, {"format", "Score: {}"}});
        scene.add("Score Counter", score);
        Json hp = uiText("TopRight", -24, -20, 300, 56, "Health: 3", 40, "Right", 0xFCA5A5);
        hp["ScoreDisplay"] = obj({{"counter", "health"}, {"format", "Health: {}"}});
        scene.add("Health Counter", hp);
        goal(0, 0, "Circle", 0);
        card.summary = "An arena shooter. Move with WASD, aim with the mouse and hold the left button to fire.";
    }

    void dodge() {
        heroId = scene.id();
        camera(0, 0, 6, 0x1E1B4B, false);
        for (auto [x, y, s] : {std::tuple{-9.0, 4.0, 0.12}, {-3.0, 5.0, 0.08}, {4.0, 4.5, 0.1}, {8.0, 2.5, 0.08}, {-6.0, 1.5, 0.07}, {2.0, 2.0, 0.09}})
            scene.add("Twinkle", obj({{"Transform", at(x, y)}, {"SpriteRenderer", sprite("Star", 0xE0E7FF, s * 3, s * 3, -10)}}));
        scene.add("Ground", obj({{"Transform", at(0, -4.5)}, {"SpriteRenderer", sprite("Square", 0x4338CA, 24, 2, 0)},
                                 {"BoxCollider2D", box(24, 2)}}));
        for (double x : {-11.5, 11.5})
            scene.add("Wall", obj({{"Transform", at(x, 0)}, {"SpriteRenderer", sprite("Square", 0x312E81, 1, 12, 0)}, {"BoxCollider2D", box(1, 12)}}));
        Json hero = obj({{"Transform", at(0, -3)},
                         {"SpriteRenderer", heroSprite(0.9)},
                         {"RigidBody2D", body(2.5)},
                         {"BoxCollider2D", obj({{"size", arr({0.75, 0.85})}, {"friction", 0}})},
                         {"PlatformerController", obj({{"speed", 7}, {"jump_power", 10}})},
                         {"Health", health(heroHealth())}});
        scene.add("Hero", hero, "player", "", heroId);
        part("Hero", "PlatformerController: run with the arrow keys, jump with Space. Health decides how many hits you can take.",
             "Give the hero Extra Jumps = 1 to dodge in mid-air.");
        rocks(0, 8, 10);
        if (*collect().shape) {
            std::string starPath = path("prefabs", collect().tag, ".prefab");
            files.push_back({starPath, prefabDoc(std::string(collect().label).substr(0, std::strlen(collect().label) - 1), collect().tag,
                                                 obj({{"Transform", at(0, 0)},
                                                      {"SpriteRenderer", sprite(collect().shape, collect().color, 0.6, 0.6, 5)},
                                                      {"RigidBody2D", obj({{"gravity_scale", 0.3}})},
                                                      {"CircleCollider2D", circle(0.3, true)},
                                                      {"Collectible", obj({{"counter", collect().counter}, {"amount", 1}})},
                                                      {"Lifetime", obj({{"seconds", 6}, {"fade_out", true}})}}))
                                           .dump(2)});
            scene.add(std::string(collect().label) + " Maker",
                      obj({{"Transform", at(0, 8)}, {"Spawner", obj({{"prefab", starPath}, {"interval", 2.2}, {"max_alive", 4}, {"random_range", arr({10, 0})}})}}));
            part(std::string(collect().label) + " Maker", "Another Spawner, dropping " + starPath + " to catch for points.",
                 "Set Amount to 3 in the prefab's Collectible to make them worth more.");
            collectibles = 1;
        }
        goal(0, 0, "Circle", 0);
        hud(true);
        card.summary = "Dodge falling rocks. Move with the arrow keys, jump with Space.";
    }

    void clicker() {
        camera(0, 0, 6, 0xFDE68A, false);
        std::string cookie = scene.add("Cookie", obj({{"Transform", at(-4, -0.3)},
                                                      {"SpriteRenderer", sprite("Circle", 0xD97706, 4.2, 4.2, 2)},
                                                      {"Clickable", obj({{"counter", "cookies"}, {"amount", 1}, {"bounce", true}})}}));
        for (auto [x, y] : {std::pair{-0.9, 0.8}, {0.7, 1.1}, {0.1, -0.4}, {-1.1, -1.0}, {1.2, -0.9}, {-0.2, 1.6}})
            scene.add("Chip", obj({{"Transform", at(x, y)}, {"SpriteRenderer", sprite("Circle", 0x78350F, 0.45, 0.45, 3)}}), "", cookie);
        part("Cookie", "Clickable adds 1 to game.cookies every time you click it, and bounces.",
             "Set Amount to 10 for giant clicks (just to test!).");
        std::string upgrade = path("scripts", "upgrade", ".es");
        files.push_back({upgrade, "# An upgrade you buy with cookies. Each one bakes more cookies every second.\n\n"
                                  "label = \"Oven\"\ncost = 15  # cookies it costs\nper_second = 1  # extra cookies each second\n\n"
                                  "def on_click():\n    if get_game(\"cookies\", 0) >= cost:\n        game.cookies -= cost\n"
                                  "        game.per_second = get_game(\"per_second\", 0) + per_second\n"
                                  "        cost = round(cost * 1.5)  # each one costs more\n\n"
                                  "def on_update(dt):\n    self.text = f\"{label}: +{per_second}/s\\ncost {cost}\"\n"});
        struct Up {
            const char* label;
            int cost, perSecond;
            double y;
            uint32_t color;
        };
        for (Up u : {Up{"Oven", 15, 1, 2.5, 0xFDE68A}, Up{"Bakery", 100, 5, 0, 0xFCD34D}, Up{"Factory", 600, 25, -2.5, 0xFBBF24}}) {
            Json comps = obj({{"Transform", at(4, u.y)},
                              {"SpriteRenderer", sprite("RoundedSquare", u.color, 5, 1.9, 1)},
                              {"TextRenderer", obj({{"text", u.label}, {"font_size", 0.42}, {"color", rgba(0x422006)}, {"order", 4}})},
                              {"Script", obj({{"path", upgrade}, {"overrides", obj({{"label", u.label}, {"cost", u.cost}, {"per_second", u.perSecond}})}})}});
            scene.add(u.label, comps);
        }
        part("Oven, Bakery and Factory", "The same script (" + upgrade + ") with different settings: label, cost and "
                                          "per_second are variables you can change in the Inspector for each one.",
             "Add a fourth upgrade: copy the Factory with Ctrl+D and set a bigger cost and per_second.");
        std::string bake = path("scripts", "baking", ".es");
        files.push_back({bake, "# Adds the cookies your upgrades bake, once every second.\n\n_timer = 0\n\n"
                               "def on_update(dt):\n    _timer += dt\n    if _timer >= 1:\n        _timer -= 1\n"
                               "        game.cookies = get_game(\"cookies\", 0) + get_game(\"per_second\", 0)\n"});
        scene.add("Baking", obj({{"Transform", at(0, 0)}, {"Script", obj({{"path", bake}})}}));
        part("Baking", "An invisible object whose script adds game.per_second cookies every second.",
             "Make it faster: change _timer >= 1 to _timer >= 0.5.");
        Json count = uiText("Top", 0, -20, 700, 70, "0 cookies", 56, "Center", 0x422006);
        count["ScoreDisplay"] = obj({{"counter", "cookies"}, {"format", "{} cookies"}});
        scene.add("Cookie Counter", count);
        Json rate = uiText("Top", 0, -90, 600, 44, "0 per second", 32, "Center", 0x92400E);
        rate["ScoreDisplay"] = obj({{"counter", "per_second"}, {"format", "{} per second"}});
        scene.add("Rate Counter", rate);
        part("Cookie Counter", "ScoreDisplay shows game.cookies, and another shows game.per_second.",
             "Change the Format to \"Cookies: {}\".");
        goal(0, 0, "Circle", 0);
        card.summary = "A clicker. Click the cookie, then buy upgrades on the right.";
    }

    void cook() {
        std::string id = def.id;
        if (id == "platformer")
            platformer();
        else if (id == "adventure")
            adventure();
        else if (id == "shooter")
            shooter();
        else if (id == "dodge")
            dodge();
        else
            clicker();
    }
};

void drawShape(ImDrawList* dl, const std::string& shape, ImVec2 c, float r, ImU32 color) {
    if (shape == "Circle") {
        dl->AddCircleFilled(c, r, color, 24);
    } else if (shape == "RoundedSquare" || shape == "Square") {
        dl->AddRectFilled({c.x - r, c.y - r}, {c.x + r, c.y + r}, color, shape == "Square" ? 0 : r * 0.35f);
    } else if (shape == "Triangle") {
        dl->AddTriangleFilled({c.x, c.y - r}, {c.x + r, c.y + r * 0.8f}, {c.x - r, c.y + r * 0.8f}, color);
    } else if (shape == "Diamond") {
        dl->AddQuadFilled({c.x, c.y - r}, {c.x + r, c.y}, {c.x, c.y + r}, {c.x - r, c.y}, color);
    } else if (shape == "Star") {
        for (int i = 0; i < 5; ++i) {
            float a0 = -1.5708f + static_cast<float>(i) * 1.2566f;
            float a1 = a0 + 0.6283f, am = a0 - 0.6283f;
            ImVec2 p0{c.x + std::cos(a0) * r, c.y + std::sin(a0) * r};
            ImVec2 p1{c.x + std::cos(a1) * r * 0.45f, c.y + std::sin(a1) * r * 0.45f};
            ImVec2 pm{c.x + std::cos(am) * r * 0.45f, c.y + std::sin(am) * r * 0.45f};
            dl->AddTriangleFilled(pm, p0, p1, color);
            dl->AddTriangleFilled(c, pm, p1, color);
        }
    } else if (shape == "Heart") {
        dl->AddCircleFilled({c.x - r * 0.45f, c.y - r * 0.25f}, r * 0.5f, color, 16);
        dl->AddCircleFilled({c.x + r * 0.45f, c.y - r * 0.25f}, r * 0.5f, color, 16);
        dl->AddTriangleFilled({c.x - r * 0.93f, c.y - r * 0.05f}, {c.x + r * 0.93f, c.y - r * 0.05f}, {c.x, c.y + r * 0.9f}, color);
    }
}

bool chip(const char* label, bool selected) {
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    }
    bool pressed = ImGui::Button(label);
    if (selected)
        ImGui::PopStyleColor(2);
    return pressed;
}

} // namespace

// ---------------------------------------------------------------- Editor API

std::string Editor::cookRecipe(const RecipeChoices& choices) {
    const auto& list = recipes();
    const RecipeDef& def = list[static_cast<size_t>(std::clamp(choices.recipe, 0, static_cast<int>(list.size()) - 1))];
    RecipeChoices c = choices;
    // Keep the choices valid for this recipe.
    if (std::find(def.goals.begin(), def.goals.end(), c.goal) == def.goals.end())
        c.goal = def.goals.front();
    if (!def.collect)
        c.collect = 0;
    if ((c.goal == RecipeGoal::CollectAll || c.goal == RecipeGoal::ReachScore) && def.collect && c.collect == 0)
        c.collect = 1;
    if (def.dangers && !(c.dangers & def.dangers))
        c.dangers = def.defaultDangers;
    if (c.name.empty())
        c.name = def.name;

    std::string slug;
    for (char ch : c.name)
        slug += std::isalnum(static_cast<unsigned char>(ch)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(ch))) : '_';
    while (!slug.empty() && slug.back() == '_')
        slug.pop_back();
    if (slug.empty())
        slug = def.id;

    Cooking cooking(*this, c, def, slug);
    cooking.cook();
    std::string scenePath = uniqueName("scenes", slug, ".scene");
    std::error_code ec;
    for (auto& [path, text] : cooking.files) {
        stdfs::create_directories((projectDir_ / path).parent_path(), ec);
        fs::writeText(projectDir_ / path, text);
    }
    stdfs::create_directories(projectDir_ / "scenes", ec);
    fs::writeText(projectDir_ / scenePath, cooking.scene.doc().dump(2));

    RecipeCard& card = cooking.card;
    card.title = c.name;
    card.scene = scenePath;
    card.summary += " " + std::string(goalName(c.goal)) + " to win.";
    std::string cardPath = "recipes/" + stdfs::path(scenePath).stem().string() + ".json";
    Json cardJson = obj({{"title", card.title}, {"summary", card.summary}, {"scene", scenePath}});
    cardJson["parts"] = Json::array();
    for (auto& p : card.parts)
        cardJson["parts"].push(obj({{"object", p.object}, {"what", p.what}, {"try", p.tryThis}}));
    stdfs::create_directories(projectDir_ / "recipes", ec);
    fs::writeText(projectDir_ / cardPath, cardJson.dump(2));

    if (c.makeStartScene) {
        settings_.startScene = scenePath;
        settings_.save(projectDir_);
    }
    if (playing_)
        stop();
    openScene(scenePath);
    scanAssets();
    recipeCard_ = card;
    showRecipeCard_ = focusRecipeCard_ = true;
    milestone("recipes");
    notify("Cooked \"" + c.name + "\"! Press Play to try it, and read the Recipe Card to see how it works.");
    return scenePath;
}

void Editor::loadRecipeCard(const std::string& scenePath) {
    recipeCard_ = {};
    auto text = fs::readText(projectDir_ / ("recipes/" + stdfs::path(scenePath).stem().string() + ".json"));
    if (!text)
        return;
    Json j = Json::parse(*text);
    recipeCard_.title = j["title"].asString("");
    recipeCard_.summary = j["summary"].asString("");
    recipeCard_.scene = j["scene"].asString("");
    for (auto& p : j["parts"].elements())
        recipeCard_.parts.push_back({p["object"].asString(""), p["what"].asString(""), p["try"].asString("")});
}

void Editor::drawRecipes() {
    ImGui::SetNextWindowSize({980, 660}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    if (focusRecipes_) {
        ImGui::SetNextWindowFocus();
        focusRecipes_ = false;
    }
    if (!ImGui::Begin("Game Recipes###Recipes", &showRecipes_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    RecipeChoices& c = recipeChoices_;
    const auto& list = recipes();
    c.recipe = std::clamp(c.recipe, 0, static_cast<int>(list.size()) - 1);
    const RecipeDef& def = list[static_cast<size_t>(c.recipe)];

    ImGui::PushFont(fonts.big);
    ImGui::TextUnformatted("What do you want to make?");
    ImGui::PopFont();
    ImGui::TextDisabled("Pick a recipe and the ingredients. Aven builds a small working game and explains every part.");
    ImGui::Spacing();

    // Recipe cards.
    float cardW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 4) / 5.0f;
    float cardH = 150;
    for (size_t i = 0; i < list.size(); ++i) {
        const RecipeDef& r = list[i];
        if (i)
            ImGui::SameLine();
        ImGui::PushID(static_cast<int>(i));
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool selected = c.recipe == static_cast<int>(i);
        if (ImGui::InvisibleButton("##card", {cardW, cardH})) {
            c.recipe = static_cast<int>(i);
            c.dangers = r.defaultDangers;
            c.goal = r.goals.front();
            c.name = r.name;
            if (r.collect && c.collect == 0)
                c.collect = 1;
        }
        bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        Color col = Color::fromHex(r.color);
        ImU32 accent = ImGui::ColorConvertFloat4ToU32({col.r, col.g, col.b, 1});
        dl->AddRectFilled(p, {p.x + cardW, p.y + cardH}, ImGui::GetColorU32(hovered || selected ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), 10);
        dl->AddRectFilled(p, {p.x + cardW, p.y + 64}, ImGui::ColorConvertFloat4ToU32({col.r, col.g, col.b, 0.35f}), 10,
                          ImDrawFlags_RoundCornersTop);
        drawShape(dl, r.icon, {p.x + cardW * 0.5f, p.y + 32}, 20, accent);
        if (selected)
            dl->AddRect(p, {p.x + cardW, p.y + cardH}, ImGui::GetColorU32(ImGuiCol_SliderGrab), 10, 0, 3);
        ImGui::PushFont(fonts.bold);
        dl->AddText({p.x + 10, p.y + 72}, ImGui::GetColorU32(ImGuiCol_Text), r.name);
        ImGui::PopFont();
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.9f, {p.x + 10, p.y + 96}, ImGui::GetColorU32(ImGuiCol_TextDisabled), r.pitch,
                    nullptr, cardW - 20);
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::Separator();

    // Ingredients.
    ImGui::BeginChild("##ingredients", {ImGui::GetContentRegionAvail().x * 0.58f, 0});
    ui::sectionHeader("Hero");
    for (int i = 0; i < IM_ARRAYSIZE(kHeroShapes); ++i) {
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool sel = c.heroImage.empty() && c.heroShape == kHeroShapes[i];
        if (ImGui::InvisibleButton("##shape", {40, 40})) {
            c.heroShape = kHeroShapes[i];
            c.heroImage.clear();
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, {p.x + 40, p.y + 40}, ImGui::GetColorU32(ImGuiCol_FrameBg), 6);
        if (sel)
            dl->AddRect(p, {p.x + 40, p.y + 40}, ImGui::GetColorU32(ImGuiCol_SliderGrab), 6, 0, 3);
        drawShape(dl, kHeroShapes[i], {p.x + 20, p.y + 20}, 13, ImGui::ColorConvertFloat4ToU32({c.heroColor.r, c.heroColor.g, c.heroColor.b, 1}));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", kHeroShapes[i]);
        ImGui::SameLine();
        ImGui::PopID();
    }
    ImGui::ColorEdit3("##herocolor", &c.heroColor.r, ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine();
    auto images = projectFiles({".png", ".jpg", ".jpeg"});
    if (!images.empty()) {
        ImGui::SetNextItemWidth(170);
        if (ImGui::BeginCombo("##heroimage", c.heroImage.empty() ? "or an image..." : c.heroImage.c_str())) {
            if (ImGui::Selectable("(use the shape)", c.heroImage.empty()))
                c.heroImage.clear();
            for (auto& img : images)
                if (ImGui::Selectable(img.c_str(), c.heroImage == img))
                    c.heroImage = img;
            ImGui::EndCombo();
        }
    }

    if (def.collect) {
        ui::sectionHeader("Things to collect");
        for (int i = 0; i < 4; ++i) {
            if (i)
                ImGui::SameLine();
            ImGui::PushID(i);
            if (chip(kCollects[i].name, c.collect == i))
                c.collect = i;
            ImGui::PopID();
        }
    }
    if (def.dangers) {
        ui::sectionHeader("Dangers");
        struct D {
            Danger d;
            const char* name;
            const char* tip;
        };
        bool first = true;
        for (D d : {D{Spikes, "Spikes", "Sharp traps that stay put"}, D{Walkers, "Walkers", "Enemies that walk back and forth"},
                    D{Chasers, "Chasers", "Enemies that come after you"}, D{Falling, "Falling rocks", "Rocks that drop from the sky"}}) {
            if (!(def.dangers & d.d))
                continue;
            if (!first)
                ImGui::SameLine();
            first = false;
            bool on = (c.dangers & d.d) != 0;
            if (ImGui::Checkbox(d.name, &on))
                c.dangers = on ? (c.dangers | d.d) : (c.dangers & ~static_cast<unsigned>(d.d));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", d.tip);
        }
    }
    ui::sectionHeader("How to win");
    for (size_t i = 0; i < def.goals.size(); ++i) {
        int g = def.goals[i];
        bool needsCollect = (g == RecipeGoal::CollectAll) && !def.collect;
        if (needsCollect)
            continue;
        if (i)
            ImGui::SameLine();
        ImGui::PushID(g);
        if (chip(goalName(g), c.goal == g))
            c.goal = g;
        ImGui::PopID();
    }
    ui::sectionHeader("Difficulty");
    for (int i = 0; i < 3; ++i) {
        if (i)
            ImGui::SameLine();
        ImGui::PushID(100 + i);
        if (chip(kDifficulty[i], c.difficulty == i))
            c.difficulty = i;
        ImGui::PopID();
    }
    ui::sectionHeader("Name");
    ImGui::SetNextItemWidth(260);
    ImGui::InputText("##recipename", &c.name);
    ImGui::Checkbox("Start the game with this scene", &c.makeStartScene);
    ImGui::EndChild();

    // Summary and cook button.
    ImGui::SameLine();
    ImGui::BeginChild("##summary", {0, 0}, ImGuiChildFlags_Border);
    ImGui::PushFont(fonts.bold);
    ImGui::TextUnformatted("Your game");
    ImGui::PopFont();
    std::string hero = c.heroImage.empty() ? std::string(c.heroShape == "RoundedSquare" ? "square" : c.heroShape) : "picture";
    for (auto& ch : hero)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    std::string kind = def.name;
    kind[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(kind[0])));
    std::string adjective = c.difficulty == 0 ? "gentle " : c.difficulty == 2 ? "tough " : "";
    std::string first = adjective.empty() ? kind : adjective;
    bool vowel = std::string("aeiou").find(first[0]) != std::string::npos;
    std::string summary = std::string(vowel ? "An " : "A ") + adjective + kind + " where you play " + (hero == "picture" ? "your own picture" : std::string(std::string("aeiou").find(hero[0]) != std::string::npos ? "an " : "a ") + hero) + ".";
    if (def.collect && c.collect)
        summary += std::string(" Collect ") + kCollects[c.collect].tag + "s.";
    std::vector<std::string> ds;
    if (c.dangers & def.dangers & Spikes)
        ds.push_back("spikes");
    if (c.dangers & def.dangers & Walkers)
        ds.push_back("walkers");
    if (c.dangers & def.dangers & Chasers)
        ds.push_back("chasers");
    if (c.dangers & def.dangers & Falling)
        ds.push_back("falling rocks");
    if (!ds.empty()) {
        summary += " Watch out for ";
        for (size_t i = 0; i < ds.size(); ++i)
            summary += (i ? (i + 1 == ds.size() ? " and " : ", ") : "") + ds[i];
        summary += ".";
    }
    summary += std::string(" Win: ") + goalName(c.goal) + ".";
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(summary.c_str());
    ImGui::Spacing();
    ImGui::TextDisabled("Everything is made with behaviors (no code needed), plus a few tiny scripts you can read. A Recipe "
                        "Card explains each part.");
    ImGui::PopTextWrapPos();
    ImGui::Dummy({0, 16});
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(34, 160, 90, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(44, 185, 105, 255));
    ImGui::PushFont(fonts.bold);
    bool cook = ImGui::Button("Cook it!", {-1, 52});
    ImGui::PopFont();
    ImGui::PopStyleColor(2);
    ImGui::EndChild();
    ImGui::End();
    if (cook) {
        showRecipes_ = false;
        cookRecipe(c);
    }
}

void Editor::drawRecipeCard() {
    ui::placeWindow({440, 620}, {0.8f, 0.45f});
    if (focusRecipeCard_) {
        ImGui::SetNextWindowFocus();
        focusRecipeCard_ = false;
    }
    if (!ImGui::Begin("Recipe Card###RecipeCard", &showRecipeCard_)) {
        ImGui::End();
        return;
    }
    if (recipeCard_.parts.empty()) {
        ImGui::TextWrapped("Cook a game from a recipe (Create > Game from a Recipe) and its card will explain every part here.");
        ImGui::End();
        return;
    }
    ImGui::PushFont(fonts.big);
    ImGui::TextUnformatted(recipeCard_.title.c_str());
    ImGui::PopFont();
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(recipeCard_.summary.c_str());
    ImGui::PopTextWrapPos();
    if (!playing_ && ImGui::Button("Play it"))
        play();
    ImGui::Separator();
    ImGui::BeginChild("##parts");
    for (size_t i = 0; i < recipeCard_.parts.size(); ++i) {
        auto& p = recipeCard_.parts[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::PushFont(fonts.bold);
        ImGui::TextUnformatted(p.object.c_str());
        ImGui::PopFont();
        std::string name = p.object.substr(0, p.object.find(" ("));
        name = name.substr(0, name.find(" and "));
        if (Entity e = editScene().findByName(name)) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Select"))
                select(e);
        }
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(p.what.c_str());
        if (!p.tryThis.empty()) {
            ImGui::TextColored({0.45f, 0.85f, 0.55f, 1}, "Try:");
            ImGui::SameLine();
            ImGui::TextUnformatted(p.tryThis.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace aven::editor
