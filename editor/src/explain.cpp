// Explain: select anything and read what it does, and how it connects to the rest of the game.
// Works on projects made by other people too, which is where it helps most.

#include "editor.h"

#include "aven/runtime/native.h"

#include "aven/blocks/blocks.h"
#include "aven/core/fs.h"
#include "script_facts.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace aven::editor {

namespace {

// "a" or "an" for the word that follows.
std::string article(const std::string& word) {
    return !word.empty() && std::string("aeiouAEIOU").find(word[0]) != std::string::npos ? "an" : "a";
}

std::string colorName(Color c) {
    struct Named {
        const char* name;
        float r, g, b;
    };
    static const Named names[] = {{"red", 0.94f, 0.27f, 0.27f}, {"orange", 0.98f, 0.45f, 0.09f}, {"yellow", 0.98f, 0.8f, 0.08f},
                                  {"green", 0.13f, 0.77f, 0.37f}, {"teal", 0.08f, 0.72f, 0.65f},  {"blue", 0.23f, 0.51f, 0.96f},
                                  {"purple", 0.66f, 0.33f, 0.97f}, {"pink", 0.93f, 0.28f, 0.6f},  {"brown", 0.57f, 0.25f, 0.05f},
                                  {"white", 1, 1, 1},              {"black", 0.07f, 0.07f, 0.07f}, {"grey", 0.61f, 0.64f, 0.69f},
                                  {"light blue", 0.49f, 0.83f, 0.99f}, {"dark green", 0.1f, 0.4f, 0.2f}};
    const Named* best = &names[0];
    float bestD = 1e9f;
    for (auto& n : names) {
        float d = (c.r - n.r) * (c.r - n.r) + (c.g - n.g) * (c.g - n.g) + (c.b - n.b) * (c.b - n.b);
        if (d < bestD) {
            bestD = d;
            best = &n;
        }
    }
    return best->name;
}

std::string shapeName(Shape2D s) {
    switch (s) {
    case Shape2D::Square: return "square";
    case Shape2D::Circle: return "circle";
    case Shape2D::Triangle: return "triangle";
    case Shape2D::RoundedSquare: return "rounded square";
    case Shape2D::Diamond: return "diamond";
    case Shape2D::Star: return "star";
    case Shape2D::Heart: return "heart";
    }
    return "shape";
}

std::string meshName(MeshShape m) {
    switch (m) {
    case MeshShape::Cube: return "cube";
    case MeshShape::Sphere: return "sphere";
    case MeshShape::Plane: return "flat plane";
    case MeshShape::Cylinder: return "cylinder";
    case MeshShape::Capsule: return "capsule";
    case MeshShape::Cone: return "cone";
    case MeshShape::Torus: return "ring (torus)";
    case MeshShape::Model: return "3D model";
    }
    return "shape";
}

std::string fmt(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, std::abs(v - std::round(v)) < 1e-4f ? "%.0f" : "%.2g", static_cast<double>(v));
    return buf;
}

// One sentence about a behavior's settings.
std::string describeBehavior(Registry& reg, Entity e, const std::string& name) {
    if (name == "Patrol") {
        auto& p = reg.get<Patrol>(e);
        const char* dir = p.axis == Axis3::X ? "left and right" : p.axis == Axis3::Y ? "up and down" : "forward and back";
        return std::string("walks ") + dir + ", " + fmt(p.distance) + " units each way, at speed " + fmt(p.speed);
    }
    if (name == "Chase") {
        auto& c = reg.get<Chase>(e);
        return std::string(c.runAway ? "runs away from" : "chases") + " anything tagged '" + c.targetTag + "' that comes within " +
               fmt(c.sight) + " units";
    }
    if (name == "Spin") return "keeps spinning";
    if (name == "Bob") return "floats gently up and down";
    if (name == "Collectible") {
        auto& c = reg.get<Collectible>(e);
        return "can be picked up by '" + c.collectorTag + "': adds " + std::to_string(c.amount) + " to game." + c.counter;
    }
    if (name == "Hazard") {
        auto& h = reg.get<Hazard>(e);
        return "hurts '" + h.victimTag + "' by " + std::to_string(h.damage) + " when touched";
    }
    if (name == "Health") {
        auto& h = reg.get<Health>(e);
        const char* z = h.whenZero == WhenHealthRunsOut::RestartScene ? "the level restarts"
                        : h.whenZero == WhenHealthRunsOut::Respawn    ? "it goes back to where it started"
                        : h.whenZero == WhenHealthRunsOut::Destroy    ? "it disappears"
                                                                      : "nothing happens";
        return "has " + std::to_string(h.maxHealth) + " health; when it runs out, " + z;
    }
    if (name == "Lifetime") return "disappears after " + fmt(reg.get<Lifetime>(e).seconds) + " seconds";
    if (name == "WrapAround") return "comes back on the other side of the screen";
    if (name == "Shooter") {
        auto& s = reg.get<Shooter>(e);
        return "fires " + (s.prefab.empty() ? std::string("(no prefab picked yet)") : s.prefab) + " when you press '" + s.action + "'";
    }
    if (name == "PlatformerController") {
        auto& p = reg.get<PlatformerController>(e);
        return "is controlled with the arrow keys and jumps with Space (speed " + fmt(p.speed) + ", jump " + fmt(p.jumpPower) +
               (p.extraJumps > 0 ? ", double jump" : "") + ")";
    }
    if (name == "TopDownController") return "walks in every direction with the arrow keys or WASD";
    if (name == "FollowMouse") return "follows the mouse pointer";
    if (name == "Draggable") return "can be dragged with the mouse";
    if (name == "Spawner") {
        auto& s = reg.get<Spawner>(e);
        return "creates a copy of " + (s.prefab.empty() ? std::string("(nothing yet)") : s.prefab) + " every " + fmt(s.interval) + " seconds";
    }
    if (name == "Clickable") {
        auto& c = reg.get<Clickable>(e);
        return "adds " + std::to_string(c.amount) + " to game." + c.counter + " each time it's clicked";
    }
    if (name == "ScoreDisplay") return "shows game." + reg.get<ScoreDisplay>(e).counter + " as text";
    if (name == "SceneLink") {
        auto& l = reg.get<SceneLink>(e);
        const char* how = l.when == LinkTrigger::Touch ? "when touched" : l.when == LinkTrigger::Click ? "when clicked" : "after a while";
        return std::string("goes to ") + (l.scene.empty() ? "(no scene picked)" : l.scene) + " " + how;
    }
    return "";
}

} // namespace

ScriptFacts& Editor::factsFor(const std::string& path) {
    auto it = factsCache_.find(path);
    std::error_code ec;
    auto stamp = stdfs::last_write_time(projectDir_ / path, ec);
    if (it != factsCache_.end() && it->second.first == stamp)
        return it->second.second;
    ScriptFacts facts;
    if (auto text = fs::readText(projectDir_ / path)) {
        if (fs::extension(path) == ".blocks")
            facts = analyzeScript(blocks::compileFile(*text), *text);
        else
            facts = analyzeScript(*text);
    }
    auto& slot = factsCache_[path];
    slot = {stamp, std::move(facts)};
    return slot.second;
}

std::vector<Editor::ExplainSection> Editor::explainEntity(Entity e) {
    std::vector<ExplainSection> out;
    Scene& s = scene();
    auto& reg = s.registry();
    EntityInfo& info = s.info(e);

    // --- What it is
    ExplainSection what{"What it is", {}};
    if (auto* sr = reg.tryGet<SpriteRenderer>(e))
        what.lines.push_back(sr->texture.empty() ? "It looks like " + article(colorName(sr->color)) + " " + colorName(sr->color) + " " +
                                                       shapeName(sr->shape) + "."
                                                 : "It's a picture: " + sr->texture + ".");
    if (auto* mr = reg.tryGet<MeshRenderer>(e))
        what.lines.push_back(mr->mesh == MeshShape::Model ? "It's a 3D model: " + mr->model + "."
                                                          : "It's " + article(colorName(mr->color)) + " " + colorName(mr->color) +
                                                                " 3D " + meshName(mr->mesh) + ".");
    if (auto* t = reg.tryGet<TextRenderer>(e))
        what.lines.push_back("It shows the text \"" + t->text + "\".");
    if (auto* tm = reg.tryGet<Tilemap>(e)) {
        static const char* kCollision[] = {"things pass through it", "it's solid", "it's a trigger"};
        what.lines.push_back("It's a tilemap with " + std::to_string(tm->tiles.size()) + " tiles" +
                             (tm->tileset.empty() ? " (colored blocks)" : " from " + tm->tileset) + "; " +
                             kCollision[std::clamp(static_cast<int>(tm->collision), 0, 2)] + ".");
    }
    if (reg.has<UIButton>(e))
        what.lines.push_back("It's a button on the screen labeled \"" + reg.get<UIButton>(e).text + "\".");
    else if (auto* ut = reg.tryGet<UIText>(e))
        what.lines.push_back("It's text pinned to the screen: \"" + ut->text + "\".");
    if (reg.has<UIImage>(e))
        what.lines.push_back("It's a panel or picture pinned to the screen.");
    if (auto* cam = reg.tryGet<Camera>(e))
        what.lines.push_back(std::string("It's a camera: it decides what the player sees") +
                             (cam->projection == Projection::Perspective ? " in 3D." : " (2D)."));
    if (auto* l = reg.tryGet<Light>(e))
        what.lines.push_back(l->type == LightType::Directional ? "It's the sun: light from far away, casting shadows."
                             : l->type == LightType::Point     ? "It's a light bulb glowing in every direction."
                                                               : "It's a spotlight pointing one way.");
    if (reg.has<Environment>(e))
        what.lines.push_back("It sets the sky, ambient light and fog for the scene.");
    if (reg.has<ParticleEmitter>(e))
        what.lines.push_back("It sprays particles (like sparks, smoke or fire).");
    if (reg.has<AudioSource>(e))
        what.lines.push_back("It plays the sound " + reg.get<AudioSource>(e).clip + ".");
    if (what.lines.empty())
        what.lines.push_back("It's an empty object: it holds a position, and maybe children or a script.");
    if (!info.tag.empty())
        what.lines.push_back("Its tag is '" + info.tag + "', so other things can find it by that tag.");
    if (Entity p = s.parent(e))
        what.lines.push_back("It's inside " + s.info(p).name + ", so it moves along with it.");
    if (!s.children(e).empty())
        what.lines.push_back("It has " + std::to_string(s.children(e).size()) + " object(s) inside it.");
    if (!info.active)
        what.lines.push_back("It's turned off right now (the eye in the Hierarchy).");
    out.push_back(std::move(what));

    // --- Physics
    ExplainSection physics{"How it moves and collides", {}};
    if (auto* rb = reg.tryGet<RigidBody2D>(e))
        physics.lines.push_back(rb->type == BodyType::Dynamic ? (rb->gravityScale == 0 ? "It's pushed around by physics, without gravity."
                                                                                       : "Gravity pulls it down and it bumps into things.")
                                : rb->type == BodyType::Kinematic ? "Physics doesn't push it; its scripts or behaviors move it."
                                                                  : "It never moves; things bump into it.");
    if (auto* rb = reg.tryGet<RigidBody>(e))
        physics.lines.push_back(rb->type == BodyType::Dynamic ? "Gravity pulls it down and it bumps into things (3D)."
                                                              : "Physics doesn't push it (3D).");
    if (reg.has<CharacterController>(e))
        physics.lines.push_back(reg.get<CharacterController>(e).useInput ? "It's a character you control with WASD and Space."
                                                                         : "It's a character moved by its script.");
    auto colliderLine = [&](bool trigger) {
        physics.lines.push_back(trigger ? "It's a trigger: things pass through it, but it notices when they touch."
                                        : "It's solid: other things bump into it.");
    };
    if (auto* c = reg.tryGet<BoxCollider2D>(e))
        colliderLine(c->isTrigger);
    else if (auto* c2 = reg.tryGet<CircleCollider2D>(e))
        colliderLine(c2->isTrigger);
    else if (auto* c3 = reg.tryGet<BoxCollider>(e))
        colliderLine(c3->isTrigger);
    else if (auto* c4 = reg.tryGet<SphereCollider>(e))
        colliderLine(c4->isTrigger);
    if (!physics.lines.empty())
        out.push_back(std::move(physics));

    // --- Behaviors
    ExplainSection behaviors{"Ready-made behaviors", {}};
    for (auto& ci : ComponentRegistry::all())
        if (ci.category == "Behaviors" && ci.get(reg, e)) {
            std::string line = describeBehavior(reg, e, ci.name);
            if (!line.empty())
                behaviors.lines.push_back(ci.name + ": it " + line + ".");
        }
    if (!behaviors.lines.empty())
        out.push_back(std::move(behaviors));

    // --- Script
    if (auto* sc = reg.tryGet<Script>(e); sc && !sc->path.empty()) {
        ScriptFacts& f = factsFor(sc->path);
        ExplainSection script{"Its script: " + sc->path, {}};
        if (!f.error.empty())
            script.lines.push_back("The script has an error on line " + std::to_string(f.errorLine) + ": " + f.error);
        for (auto& [when, lines] : f.handlers) {
            if (lines.empty())
                continue;
            std::string all = when + ": ";
            for (size_t i = 0; i < lines.size(); ++i) {
                size_t first = lines[i].find_first_not_of(' ');
                all += (i ? "; " : "") + (first == std::string::npos ? lines[i] : lines[i].substr(first));
            }
            script.lines.push_back(all + ".");
        }
        if (f.handlers.empty() && f.error.empty())
            script.lines.push_back("The script doesn't do anything yet.");
        out.push_back(std::move(script));
    }

    // --- Native code
    if (auto* ns = reg.tryGet<NativeScript>(e); ns && !ns->className.empty()) {
        ExplainSection native{"Its native code (C/C++): " + ns->className, {}};
        if (const NativeBehaviorInfo* b = NativeModules::get().find(ns->className)) {
            native.lines.push_back("The behavior " + b->name + " comes from " + b->module + ", built from the code in native/src.");
            std::string props;
            for (auto& p : b->properties) {
                const Json& o = ns->overrides[p.name];
                double v = o.isNumber() ? o.asNumber() : o.isBool() ? (o.asBool() ? 1 : 0) : p.defaultValue;
                char text[64];
                std::snprintf(text, sizeof text, "%g", v);
                props += (props.empty() ? "" : ", ") + p.name + " = " + text;
            }
            if (!props.empty())
                native.lines.push_back("Its settings: " + props + ".");
            const AvenBehavior& cb = b->callbacks;
            std::string when;
            auto add = [&](bool has, const char* what) {
                if (has)
                    when += (when.empty() ? "" : ", ") + std::string(what);
            };
            add(cb.on_start, "when the game starts");
            add(cb.on_update || cb.on_fixed_update, "every frame");
            add(cb.on_collide || cb.on_trigger, "when it touches something");
            add(cb.on_click, "when it's clicked");
            add(cb.on_key_pressed, "when a key is pressed");
            add(cb.on_message, "when it gets a message");
            add(cb.on_destroy, "when it's destroyed");
            if (!when.empty())
                native.lines.push_back("Its C code runs " + when + ".");
        } else {
            native.lines.push_back("No behavior called " + ns->className + " is built yet, so it won't do anything. Build the "
                                   "native module in Tools > Native Code.");
        }
        out.push_back(std::move(native));
    }

    // --- Connections to other objects
    ExplainSection links{"How it connects to the rest of the game", {}};
    std::set<std::string> mine; // messages this object listens for
    if (auto* sc = reg.tryGet<Script>(e); sc && !sc->path.empty()) {
        ScriptFacts& f = factsFor(sc->path);
        mine = f.receives;
        for (auto& m : f.sends)
            links.lines.push_back("It sends the message \"" + m + "\".");
        for (auto& v : f.gameVarsWritten)
            links.lines.push_back("It changes game." + v + " (a value every script can see).");
        for (auto& n : f.findsNames)
            if (n != info.name)
                links.lines.push_back("It looks for the object called " + n + ".");
        for (auto& k : f.keys)
            links.lines.push_back("It listens to the key: " + k + ".");
    }
    // Who else refers to this object?
    std::vector<std::string> watchers;
    s.walk([&](Entity o, int) {
        if (o == e)
            return true;
        auto* osc = reg.tryGet<Script>(o);
        std::string oname = s.info(o).name;
        if (osc && !osc->path.empty()) {
            ScriptFacts& f = factsFor(osc->path);
            if (f.findsNames.count(info.name))
                watchers.push_back(oname + "'s script looks for it by name");
            else if (!info.tag.empty() && f.tags.count(info.tag))
                watchers.push_back(oname + "'s script reacts to things tagged '" + info.tag + "'");
            for (auto& m : mine)
                if (f.sends.count(m))
                    links.lines.push_back(oname + " sends \"" + m + "\", which this object listens for.");
        }
        auto tagHit = [&](const std::string& tag) { return !info.tag.empty() && (tag == info.tag || tag == info.name); };
        if (auto* c = reg.tryGet<Chase>(o); c && tagHit(c->targetTag))
            watchers.push_back(oname + " " + (c->runAway ? "runs away from" : "chases") + " it");
        if (auto* h = reg.tryGet<Hazard>(o); h && tagHit(h->victimTag))
            watchers.push_back(oname + " can hurt it");
        if (auto* col = reg.tryGet<Collectible>(o); col && tagHit(col->collectorTag))
            watchers.push_back("it can collect " + oname);
        if (auto* cf = reg.tryGet<CameraFollow>(o); cf && cf->target == info.uuid)
            watchers.push_back("the camera (" + oname + ") follows it");
        return true;
    });
    std::sort(watchers.begin(), watchers.end());
    watchers.erase(std::unique(watchers.begin(), watchers.end()), watchers.end());
    for (auto& w : watchers)
        links.lines.push_back(w + ".");
    // The same sentence from several copies of an object ("Coin", "Coin"...) is said once.
    std::vector<std::string> unique;
    for (auto& l : links.lines)
        if (std::find(unique.begin(), unique.end(), l) == unique.end())
            unique.push_back(l);
    links.lines = std::move(unique);
    if (!links.lines.empty())
        out.push_back(std::move(links));
    return out;
}

std::vector<Editor::ExplainSection> Editor::explainGame() {
    std::vector<ExplainSection> out;
    Scene& s = scene();
    auto& reg = s.registry();
    int objects = 0;
    s.walk([&](Entity, int) {
        ++objects;
        return true;
    });
    ExplainSection overview{"Overview", {}};
    overview.lines.push_back("\"" + settings_.name + "\" has " + std::to_string(projectFiles({".scene"}).size()) + " scene(s). This one (" +
                             (scenePath_.empty() ? "unsaved" : scenePath_) + ") has " + std::to_string(objects) + " objects.");
    bool threeD = reg.count<MeshRenderer>() > 0;
    overview.lines.push_back(threeD ? "It's a 3D game." : "It's a 2D game.");
    out.push_back(std::move(overview));

    // Who's the player, and how is it controlled?
    ExplainSection player{"The player and controls", {}};
    std::set<std::string> keys;
    s.walk([&](Entity e, int) {
        std::string n = s.info(e).name;
        if (reg.has<PlatformerController>(e))
            player.lines.push_back(n + " runs with the arrow keys or A/D and jumps with Space.");
        if (reg.has<TopDownController>(e))
            player.lines.push_back(n + " walks around with the arrow keys or WASD.");
        if (auto* cc = reg.tryGet<CharacterController>(e); cc && cc->useInput)
            player.lines.push_back(n + " walks with WASD and jumps with Space" + std::string(cc->firstPerson ? ", in first person." : "."));
        if (auto* sc = reg.tryGet<Script>(e); sc && !sc->path.empty()) {
            ScriptFacts& f = factsFor(sc->path);
            if (f.readsInput)
                player.lines.push_back(n + " is controlled by its script (" + sc->path + ").");
            keys.insert(f.keys.begin(), f.keys.end());
        }
        return true;
    });
    if (!keys.empty()) {
        std::string list;
        for (auto& k : keys)
            list += (list.empty() ? "" : ", ") + k;
        player.lines.push_back("Keys used: " + list + ".");
    }
    if (player.lines.empty())
        player.lines.push_back("Nothing reads the keyboard yet, so there's no player to control.");
    out.push_back(std::move(player));

    // Goals and dangers.
    ExplainSection goals{"Goals and dangers", {}};
    std::set<std::string> vars;
    int collectibles = 0, hazards = 0;
    s.walk([&](Entity e, int) {
        std::string n = s.info(e).name;
        if (reg.has<Collectible>(e))
            ++collectibles;
        if (reg.has<Hazard>(e))
            ++hazards;
        if (auto* l = reg.tryGet<SceneLink>(e))
            goals.lines.push_back(n + " leads to " + (l->scene.empty() ? "another scene" : l->scene) + ".");
        if (auto* sc = reg.tryGet<Script>(e); sc && !sc->path.empty()) {
            ScriptFacts& f = factsFor(sc->path);
            for (auto& sc2 : f.scenes)
                goals.lines.push_back(n + "'s script can go to " + sc2 + ".");
            if (f.restartsScene)
                goals.lines.push_back(n + "'s script can restart the level.");
            if (f.sends.count("win"))
                goals.lines.push_back(n + " announces the win (message \"win\").");
            vars.insert(f.gameVarsWritten.begin(), f.gameVarsWritten.end());
        }
        return true;
    });
    if (collectibles)
        goals.lines.push_back(std::to_string(collectibles) + " things can be collected.");
    if (hazards)
        goals.lines.push_back(std::to_string(hazards) + " things are dangerous.");
    if (!vars.empty()) {
        std::string list;
        for (auto& v : vars)
            list += (list.empty() ? "game." : ", game.") + v;
        goals.lines.push_back("It keeps track of: " + list + ".");
    }
    if (!goals.lines.empty())
        out.push_back(std::move(goals));

    // Scripted objects, one line each.
    ExplainSection scripted{"Objects with scripts or behaviors", {}};
    s.walk([&](Entity e, int) {
        std::string n = s.info(e).name;
        std::vector<std::string> parts;
        for (auto& ci : ComponentRegistry::all())
            if (ci.category == "Behaviors" && ci.get(reg, e))
                parts.push_back(describeBehavior(reg, e, ci.name));
        if (auto* sc = reg.tryGet<Script>(e); sc && !sc->path.empty()) {
            ScriptFacts& f = factsFor(sc->path);
            if (!f.handlers.empty())
                parts.push_back(f.handlers[0].first + ", it " + (f.handlers[0].second.empty() ? "acts" : f.handlers[0].second[0]));
        }
        if (!parts.empty()) {
            std::string line = n + ": ";
            for (size_t i = 0; i < parts.size() && i < 3; ++i)
                line += (i ? "; " : "") + parts[i];
            scripted.lines.push_back(line + ".");
        }
        return scripted.lines.size() < 40;
    });
    // Collapse repeated objects ("Coin" x10).
    std::map<std::string, int> counts;
    std::vector<std::string> unique;
    for (auto& l : scripted.lines)
        if (counts[l]++ == 0)
            unique.push_back(l);
    scripted.lines.clear();
    for (auto& l : unique)
        scripted.lines.push_back(counts[l] > 1 ? l + " (" + std::to_string(counts[l]) + " of these)" : l);
    if (!scripted.lines.empty())
        out.push_back(std::move(scripted));

    // Messages between objects.
    ExplainSection messages{"Messages between objects", {}};
    std::map<std::string, std::pair<std::set<std::string>, std::set<std::string>>> byMessage;
    s.walk([&](Entity e, int) {
        if (auto* sc = reg.tryGet<Script>(e); sc && !sc->path.empty()) {
            ScriptFacts& f = factsFor(sc->path);
            for (auto& m : f.sends)
                byMessage[m].first.insert(s.info(e).name);
            for (auto& m : f.receives)
                byMessage[m].second.insert(s.info(e).name);
        }
        return true;
    });
    for (auto& [m, who] : byMessage) {
        auto join = [](const std::set<std::string>& names) {
            std::string out;
            for (auto& n : names)
                out += (out.empty() ? "" : ", ") + n;
            return out.empty() ? std::string("nobody") : out;
        };
        messages.lines.push_back("\"" + m + "\": sent by " + join(who.first) + ", heard by " + join(who.second) + ".");
    }
    if (!messages.lines.empty())
        out.push_back(std::move(messages));
    return out;
}

void Editor::drawExplain() {
    ui::panelClass();
    if (focusExplain_) {
        ImGui::SetNextWindowFocus();
        focusExplain_ = false;
    }
    if (!ImGui::Begin("Explain###Explain", &showExplain_)) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("##explaintabs")) {
        auto drawSections = [&](const std::vector<ExplainSection>& sections) {
            for (auto& sec : sections) {
                ImGui::PushFont(fonts.bold);
                ImGui::TextUnformatted(sec.title.c_str());
                ImGui::PopFont();
                ImGui::PushTextWrapPos(0);
                for (auto& l : sec.lines) {
                    ImGui::Bullet();
                    ImGui::TextUnformatted(l.c_str());
                }
                ImGui::PopTextWrapPos();
                ImGui::Spacing();
            }
        };
        if (ImGui::BeginTabItem("This object")) {
            Entity e = selected();
            if (!e) {
                ImGui::TextWrapped("Select an object in the Scene or Hierarchy to see what it does.");
            } else {
                ImGui::PushFont(fonts.big);
                ImGui::TextUnformatted(scene().info(e).name.c_str());
                ImGui::PopFont();
                drawSections(explainEntity(e));
                if (auto* sc = scene().registry().tryGet<Script>(e); sc && !sc->path.empty() && ImGui::Button("Open its script"))
                    openScript(sc->path);
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Whole game")) {
            drawSections(explainGame());
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

} // namespace aven::editor
