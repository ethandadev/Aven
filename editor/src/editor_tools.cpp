// Editor tools: EasyScript that works on the scene being edited, like Unity's editor scripts.
// Each .es file in editor_tools/ is a command (Tools > Editor tools, and the command palette);
// running it calls its run() function. Everything a script can do to objects in a game works,
// plus selection(), select(), create() and notify(). One run is one undo step.

#include "editor.h"

#include "aven/core/fs.h"
#include "aven/core/log.h"
#include "aven/runtime/game.h"
#include "aven/runtime/script_system.h"
#include "aven/script/vm.h"

#include <imgui.h>

namespace aven::editor {

namespace {

const char* kToolTemplate = R"(# Lines up the selected objects in a row, a gap apart.
# Editor tools work on the scene you're editing: run this from Tools > Editor tools.
# Everything a script can do to objects works here, plus selection(), select(),
# create() and notify(). One run is one undo step, so Ctrl+Z puts things back.

gap = 2

def run():
    objects = selection()
    if len(objects) < 2:
        notify("Select two or more objects first.")
        return
    x = objects[0].x
    for obj in objects:
        obj.x = x
        obj.y = objects[0].y
        x += gap
    notify("Lined up " + str(len(objects)) + " objects.")
)";

} // namespace

std::vector<Editor::EditorTool> Editor::editorTools() const {
    std::vector<EditorTool> tools;
    for (auto& f : assetFiles_) {
        if (f.rfind("editor_tools/", 0) != 0 || fs::extension(f) != ".es")
            continue;
        EditorTool t;
        t.path = f;
        std::string stem = stdfs::path(f).stem().string();
        for (char& c : stem)
            if (c == '_' || c == '-')
                c = ' ';
        if (!stem.empty())
            stem[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(stem[0])));
        t.name = stem;
        // The first comment line says what it does.
        if (auto text = fs::readText(projectDir_ / f)) {
            size_t start = text->find_first_not_of(" \t\r\n");
            if (start != std::string::npos && (*text)[start] == '#') {
                size_t end = text->find('\n', start);
                t.about = text->substr(start + 1, end == std::string::npos ? std::string::npos : end - start - 1);
                while (!t.about.empty() && (t.about.front() == ' ' || t.about.front() == '#'))
                    t.about.erase(0, 1);
            }
        }
        tools.push_back(std::move(t));
    }
    return tools;
}

void Editor::newEditorTool() {
    std::string path = uniqueName("editor_tools", "line_up", ".es");
    fs::writeText(projectDir_ / path, kToolTemplate);
    scanAssets();
    openScript(path);
    notify("Made " + path + ". Save it, then run it from Tools > Editor tools.");
}

bool Editor::runEditorTool(const std::string& path) {
    if (playing_) {
        notify("Stop the game to run editor tools.", true);
        return false;
    }
    if (!hasProject() || !fs::exists(projectDir_ / path)) {
        notify("There's no editor tool " + path + ".", true);
        return false;
    }
    saveAllScripts(); // run what's in the code editor, not the last saved copy
    std::string name = stdfs::path(path).stem().string();
    recordUndo("Tool: " + name);
    runningTool_ = true;

    // A game that borrows the scene being edited, without starting it.
    auto game = makeGame();
    game->adoptScene(std::move(scene_));
    ScriptSystem& scripts = game->scripts();
    script::VM& vm = scripts.vm();
    std::vector<UUID> newSelection;
    bool selectionChanged = false;
    std::string message;

    vm.defineFunction("selection", "selection()", 0, 0, [&](script::CallArgs&) {
        std::vector<script::Value> list;
        for (UUID id : selection_)
            if (Entity e = game->scene().findByUUID(id))
                list.push_back(scripts.entityValue(e));
        return script::Value::list(std::move(list));
    });
    vm.defineFunction("select", "select(objects)", 1, 1, [&](script::CallArgs& a) {
        newSelection.clear();
        selectionChanged = true;
        auto add = [&](const script::Value& v) {
            if (Entity e = scripts.entityFromValue(v))
                newSelection.push_back(game->scene().info(e).uuid);
        };
        if (a[0].isList())
            for (auto& v : a[0].listObj().items)
                add(v);
        else
            add(a[0]);
        return script::Value();
    });
    vm.defineFunction("notify", "notify(\"Done!\")", 1, 1, [&](script::CallArgs& a) {
        message = a[0].toString();
        return script::Value();
    });
    // create("Cube"), create("Sprite", x, y): anything from the Create menu.
    vm.defineFunction("create", "create(\"Cube\", x, y, z)", 1, 4, [&](script::CallArgs& a) {
        std::string kind = a.string(0, "kind");
        scene_ = game->releaseScene();
        Entity e = createEntity(kind);
        if (a.has(1)) {
            Vec3& p = scene_->transform(e).position;
            p.x = static_cast<float>(a.number(1, "x"));
            p.y = static_cast<float>(a.numberOr(2, "y", p.y));
            p.z = static_cast<float>(a.numberOr(3, "z", p.z));
        }
        game->adoptScene(std::move(scene_));
        return scripts.entityValue(e);
    });

    bool ok = false;
    if (auto module = scripts.module(path)) {
        if (auto instance = vm.createInstance(module, script::Value(), name)) {
            static const script::Symbol run = script::intern("run");
            if (!instance->find(run)) {
                Log::write(LogLevel::Error, "An editor tool needs a def run(): that does the work.", path, 1);
            } else {
                ok = vm.callFunction(instance, run, {}, false).ok;
            }
        }
    }
    scene_ = game->releaseScene();
    game.reset();
    scene_->flushDestroyed();
    scene_->updateTransforms();
    runningTool_ = false;
    if (selectionChanged)
        selection_ = newSelection;
    std::erase_if(selection_, [&](UUID id) { return !scene_->findByUUID(id); });
    snapshotValid_ = false;
    dirty_ = true;
    refreshTitle();
    if (!ok)
        notify(name + " stopped with an error (see the Console). Ctrl+Z puts things back.", true);
    else if (!message.empty())
        notify(message);
    else
        notify("Ran " + name + ".");
    return ok;
}

void Editor::drawEditorToolsMenu() {
    auto tools = editorTools();
    for (auto& t : tools) {
        if (ImGui::MenuItem(t.name.c_str(), nullptr, false, !playing_))
            runEditorTool(t.path);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s%s(%s)", t.about.c_str(), t.about.empty() ? "" : "\n", t.path.c_str());
    }
    if (tools.empty())
        ImGui::TextDisabled("No tools yet: they're EasyScript in editor_tools/.");
    ImGui::Separator();
    if (ImGui::MenuItem("New editor tool..."))
        newEditorTool();
    if (!tools.empty() && ImGui::BeginMenu("Edit a tool")) {
        for (auto& t : tools)
            if (ImGui::MenuItem(t.name.c_str()))
                openScript(t.path);
        ImGui::EndMenu();
    }
}

} // namespace aven::editor
