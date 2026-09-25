// Native code: C and C++ behaviors for the advanced tier. Creates a module from the template,
// builds it with CMake in the background (compiler errors land in the Console, clickable), reloads
// it, and lets objects pick a native behavior and set its properties in the Inspector.

#include "editor.h"

#include "aven/core/fs.h"
#include "aven/runtime/native.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
#define AVEN_POPEN _popen
#define AVEN_PCLOSE _pclose
#else
#define AVEN_POPEN popen
#define AVEN_PCLOSE pclose
#endif

namespace aven::editor {

namespace {

// "src/player.c:12:5: error: expected ';'" (GCC, Clang) or "src\player.c(12): error C2143: ..." (MSVC).
bool parseCompilerLine(const std::string& line, std::string& file, int& lineNo, std::string& message, bool& isError) {
    for (const char* kind : {": error", ": fatal error", ": warning"}) {
        size_t at = line.find(kind);
        if (at == std::string::npos)
            continue;
        isError = std::string(kind) != ": warning";
        std::string where = line.substr(0, at);
        size_t colon = line.find(':', at + 2);
        message = colon == std::string::npos ? line.substr(at + 2) : line.substr(colon + 1);
        while (!message.empty() && message[0] == ' ')
            message.erase(0, 1);
        lineNo = 0;
        if (size_t paren = where.rfind('('); paren != std::string::npos && where.back() == ')') {
            lineNo = std::atoi(where.c_str() + paren + 1); // MSVC
            file = where.substr(0, paren);
        } else {
            // file:line:col (the file itself may contain a drive letter's colon)
            size_t c2 = where.rfind(':');
            size_t c1 = c2 == std::string::npos ? std::string::npos : where.rfind(':', c2 - 1);
            if (c1 != std::string::npos && c1 > 1) {
                lineNo = std::atoi(where.c_str() + c1 + 1);
                file = where.substr(0, c1);
            } else if (c2 != std::string::npos && c2 > 1) {
                lineNo = std::atoi(where.c_str() + c2 + 1);
                file = where.substr(0, c2);
            } else {
                file = where;
            }
        }
        return lineNo > 0 && !file.empty();
    }
    return false;
}

void copyIfMissing(const stdfs::path& from, const stdfs::path& to) {
    std::error_code ec;
    if (!stdfs::exists(to, ec))
        stdfs::copy_file(from, to, ec);
}

} // namespace

bool Editor::isNativeSource(const std::string& path) const {
    std::string ext = fs::extension(path);
    return ext == ".c" || ext == ".cpp" || ext == ".cc" || ext == ".h" || ext == ".hpp";
}

void Editor::openNativeCode() { showNativeCode_ = focusNativeCode_ = true; }

void Editor::createNativeModule() {
    stdfs::path sdk = sdkDir(), native = projectDir_ / "native";
    std::error_code ec;
    if (!stdfs::exists(sdk / "include" / "aven.h", ec)) {
        notify("Aven's C header (sdk/include/aven.h) is missing from this installation.", true);
        return;
    }
    for (const char* dir : {"src", "include", "bin"})
        stdfs::create_directories(native / dir, ec);
    // The header always matches this version of Aven.
    stdfs::copy_file(sdk / "include" / "aven.h", native / "include" / "aven.h", stdfs::copy_options::overwrite_existing, ec);
    copyIfMissing(sdk / "template" / "CMakeLists.txt", native / "CMakeLists.txt");
    copyIfMissing(sdk / "template" / "README.md", native / "README.md");
    bool hasSources = false;
    for (auto& f : stdfs::directory_iterator(native / "src", ec))
        hasSources |= isNativeSource(f.path().string());
    if (!hasSources)
        copyIfMissing(sdk / "template" / "src" / "behaviors.c", native / "src" / "behaviors.c");
    if (!stdfs::exists(native / ".gitignore", ec))
        fs::writeText(native / ".gitignore", "build/\n");
    scanAssets();
    if (stdfs::exists(native / "src" / "behaviors.c", ec))
        openScript("native/src/behaviors.c");
    notify("Made native/ with a CMake project and example behaviors. Press Build to compile it.");
}

void Editor::buildNativeModule() {
    if (nativeBuild_)
        return; // already building
    if (!stdfs::exists(projectDir_ / "native" / "CMakeLists.txt"))
        createNativeModule();
    if (!stdfs::exists(projectDir_ / "native" / "CMakeLists.txt"))
        return;
    saveAllScripts();
    std::error_code ec;
    stdfs::copy_file(sdkDir() / "include" / "aven.h", projectDir_ / "native" / "include" / "aven.h",
                     stdfs::copy_options::overwrite_existing, ec);
    if (nativeThread_.joinable())
        nativeThread_.join();
    auto build = std::make_shared<NativeBuild>();
    nativeBuild_ = build;
    nativeOutput_.clear();
    nativeErrors_ = 0;
    std::string src = (projectDir_ / "native").string(), out = (projectDir_ / "native" / "build").string();
    nativeThread_ = std::thread([build, src, out] {
        auto say = [&](const std::string& line) {
            std::lock_guard lock(build->mutex);
            build->lines.push_back(line);
        };
        auto run = [&](const std::string& command) {
            say("> " + command);
            FILE* pipe = AVEN_POPEN((command + " 2>&1").c_str(), "r");
            if (!pipe) {
                say("Couldn't start: " + command);
                return false;
            }
            char buffer[2048];
            while (std::fgets(buffer, sizeof buffer, pipe)) {
                std::string line = buffer;
                while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                    line.pop_back();
                say(line);
            }
            return AVEN_PCLOSE(pipe) == 0;
        };
        bool ok = run("cmake -S \"" + src + "\" -B \"" + out + "\" -DCMAKE_BUILD_TYPE=Release") &&
                  run("cmake --build \"" + out + "\" --config Release");
        build->ok = ok;
        build->done = true;
    });
    showNativeCode_ = true;
}

void Editor::updateNativeCode(float dt) {
    if (auto build = nativeBuild_) {
        std::vector<std::string> fresh;
        {
            std::lock_guard lock(build->mutex);
            fresh.swap(build->lines);
        }
        for (auto& line : fresh) {
            nativeOutput_.push_back(line);
            std::string file, message;
            int lineNo = 0;
            bool isError = false;
            if (parseCompilerLine(line, file, lineNo, message, isError)) {
                // Paths inside the project become clickable project paths.
                std::error_code ec;
                stdfs::path p = stdfs::weakly_canonical(stdfs::path(file).is_absolute() ? stdfs::path(file)
                                                                                        : projectDir_ / "native" / "build" / file, ec);
                stdfs::path rel = p.lexically_relative(stdfs::weakly_canonical(projectDir_, ec));
                std::string shown = !rel.empty() && rel.native()[0] != '.' ? rel.generic_string() : file;
                Log::write(isError ? LogLevel::Error : LogLevel::Warning, message, shown, lineNo);
                nativeErrors_ += isError;
            }
        }
        if (nativeOutput_.size() > 2000)
            nativeOutput_.erase(nativeOutput_.begin(), nativeOutput_.end() - 2000);
        if (build->done.load() && fresh.empty()) {
            nativeBuildOk_ = build->ok;
            nativeBuiltOnce_ = true;
            nativeBuild_.reset();
            if (nativeThread_.joinable())
                nativeThread_.join();
            if (nativeBuildOk_) {
                if (!playing_)
                    NativeModules::get().refresh(projectDir_);
                int n = static_cast<int>(NativeModules::get().behaviors().size());
                notify("Native code built" + std::string(playing_ ? " (it loads when the game stops)" : "") + ": " +
                       std::to_string(n) + (n == 1 ? " behavior" : " behaviors"));
                milestone("native_builds");
            } else {
                bool noCmake = false;
                for (auto& l : nativeOutput_)
                    noCmake |= l.find("cmake: not found") != std::string::npos || l.find("cmake: command not found") != std::string::npos ||
                               l.find("'cmake' is not recognized") != std::string::npos;
                notify(noCmake ? "CMake isn't installed (or isn't on the PATH). See native/README.md for what to install."
                               : "The build failed with " + std::to_string(nativeErrors_) + " error" + (nativeErrors_ == 1 ? "" : "s") +
                                     ". Click one in the Console to jump to it.",
                       true);
            }
        }
    }
    // Pick up libraries built outside the editor too (only while editing: running code can't be swapped).
    nativeRefreshTimer_ += dt;
    if (!playing_ && nativeRefreshTimer_ > 1.0f && !projectDir_.empty()) {
        nativeRefreshTimer_ = 0;
        NativeModules::get().refresh(projectDir_);
    }
}

void Editor::drawNativeCode() {
    ui::placeWindow({640, 620}, {0.5f, 0.45f});
    if (focusNativeCode_) {
        ImGui::SetNextWindowFocus();
        focusNativeCode_ = false;
    }
    if (!ImGui::Begin("Native Code (C/C++)###NativeCode", &showNativeCode_)) {
        ImGui::End();
        return;
    }
    std::error_code ec;
    const bool hasModule = stdfs::exists(projectDir_ / "native" / "CMakeLists.txt", ec);
    NativeModules& modules = NativeModules::get();
    if (!hasModule) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("Write parts of your game in C or C++, the languages most engines (and Aven itself) are "
                               "written in. Native code runs at full speed, and it uses the same names as EasyScript:");
        ImGui::PopTextWrapPos();
        ImGui::PushFont(fonts.code);
        ImGui::TextDisabled("  self.angle += speed * dt");
        ImGui::TextDisabled("  aven_set(self, \"angle\", aven_get(self, \"angle\") + s->speed * dt);");
        ImGui::PopFont();
        ImGui::Spacing();
        if (ImGui::Button("Create a native module", {-1, 36}))
            createNativeModule();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("You'll need CMake and a C compiler: Visual Studio (Community) on Windows, the Xcode command line "
                           "tools on macOS, or gcc/clang on Linux. Native code doesn't run in web builds.");
        ImGui::PopStyleColor();
        ImGui::End();
        return;
    }

    // Build bar.
    const bool building = nativeBuild_ != nullptr;
    ImGui::BeginDisabled(building);
    if (ImGui::Button(building ? "Building..." : "Build", {120, 30}))
        buildNativeModule();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Compile native/src into native/bin, then reload it (%s)", chordName(prefs.chord("build_native")).c_str());
    ImGui::SameLine();
    if (building) {
        static const char* spin = "|/-\\";
        ImGui::Text("%c compiling", spin[static_cast<int>(ImGui::GetTime() * 8) % 4]);
    } else if (nativeBuiltOnce_) {
        if (nativeBuildOk_)
            ImGui::TextColored({0.45f, 0.9f, 0.5f, 1}, "Built");
        else
            ImGui::TextColored({1, 0.45f, 0.45f, 1}, "Failed: %d error%s", nativeErrors_, nativeErrors_ == 1 ? "" : "s");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Open aven.h"))
        openScript("native/include/aven.h");
    ImGui::SameLine();
    if (ImGui::SmallButton("Open folder"))
        openExternal((projectDir_ / "native").string());
    if (playing_)
        ImGui::TextColored({1, 0.8f, 0.3f, 1}, "A new build loads when the game stops.");

    // Source files.
    ImGui::SeparatorText("Your code (native/src)");
    std::vector<std::string> sources;
    for (auto& f : stdfs::directory_iterator(projectDir_ / "native" / "src", ec))
        if (isNativeSource(f.path().string()))
            sources.push_back("native/src/" + f.path().filename().string());
    std::sort(sources.begin(), sources.end());
    for (auto& s : sources)
        if (ImGui::Selectable(s.c_str()))
            openScript(s);
    if (sources.empty())
        ImGui::TextDisabled("No .c or .cpp files yet.");
    if (ImGui::SmallButton("+ New C file")) {
        std::string path = uniqueName("native/src", "behavior", ".c");
        fs::writeText(projectDir_ / path, "#include \"aven.h\"\n\n/* Only one file needs AVEN_MODULE(setup); declare your "
                                          "behaviors there,\n   or call functions from this file in it. */\n");
        openScript(path);
    }

    // What's loaded.
    ImGui::SeparatorText("Loaded behaviors");
    auto behaviors = modules.behaviors();
    for (auto& m : modules.modules())
        if (!m.error.empty())
            ImGui::TextColored({1, 0.45f, 0.45f, 1}, "%s didn't load: %s", m.file.c_str(), m.error.c_str());
    if (behaviors.empty()) {
        ImGui::TextDisabled(modules.modules().empty() ? "Nothing built yet. Press Build." : "The module has no behaviors.");
    } else if (ImGui::BeginTable("##native", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Behavior");
        ImGui::TableSetupColumn("Properties");
        ImGui::TableSetupColumn("Events");
        ImGui::TableHeadersRow();
        for (auto* b : behaviors) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(b->name.c_str());
            ImGui::TableNextColumn();
            std::string props;
            for (auto& p : b->properties)
                props += (props.empty() ? "" : ", ") + p.name;
            ImGui::TextDisabled("%s", props.empty() ? "-" : props.c_str());
            ImGui::TableNextColumn();
            const AvenBehavior& cb = b->callbacks;
            std::string events;
            auto add = [&](const void* fn, const char* name) {
                if (fn)
                    events += (events.empty() ? "" : ", ") + std::string(name);
            };
            add(reinterpret_cast<const void*>(cb.on_start), "start");
            add(reinterpret_cast<const void*>(cb.on_update), "update");
            add(reinterpret_cast<const void*>(cb.on_fixed_update), "fixed update");
            add(reinterpret_cast<const void*>(cb.on_collide), "collide");
            add(reinterpret_cast<const void*>(cb.on_trigger), "trigger");
            add(reinterpret_cast<const void*>(cb.on_click), "click");
            add(reinterpret_cast<const void*>(cb.on_key_pressed), "key");
            add(reinterpret_cast<const void*>(cb.on_message), "message");
            add(reinterpret_cast<const void*>(cb.on_destroy), "destroy");
            ImGui::TextDisabled("%s", events.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Use one: select an object, Add Component > NativeScript, pick the behavior.");

    // Compiler output.
    if (!nativeOutput_.empty() && ImGui::CollapsingHeader("Build output", nativeBuildOk_ ? 0 : ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginChild("##nativeout", {0, 0}, ImGuiChildFlags_Border, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PushFont(fonts.code);
        for (auto& line : nativeOutput_) {
            bool err = line.find("error") != std::string::npos;
            bool warn = !err && line.find("warning") != std::string::npos;
            if (err || warn)
                ImGui::PushStyleColor(ImGuiCol_Text, err ? ImVec4(1, 0.5f, 0.5f, 1) : ImVec4(1, 0.8f, 0.4f, 1));
            ImGui::TextUnformatted(line.c_str());
            if (err || warn)
                ImGui::PopStyleColor();
        }
        if (building)
            ImGui::SetScrollHereY(1.0f);
        ImGui::PopFont();
        ImGui::EndChild();
    }
    ImGui::End();
}

void Editor::drawNativeScriptInspector(Entity e) {
    auto& reg = scene().registry();
    auto& ns = reg.get<NativeScript>(e);
    NativeModules& modules = NativeModules::get();
    auto behaviors = modules.behaviors();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##nativebehavior", ns.className.empty() ? "Pick a behavior..." : ns.className.c_str())) {
        for (auto* b : behaviors) {
            if (ImGui::Selectable(b->name.c_str(), b->name == ns.className)) {
                if (!playing_)
                    recordUndo("Native behavior");
                ns.className = b->name;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("From %s", b->module.c_str());
        }
        if (behaviors.empty())
            ImGui::TextDisabled("No native behaviors are built yet.");
        ImGui::EndCombo();
    }
    const NativeBehaviorInfo* info = ns.className.empty() ? nullptr : modules.find(ns.className);
    if (!info) {
        if (!ns.className.empty())
            ImGui::TextColored({1, 0.7f, 0.3f, 1}, "No behavior called '%s' is loaded.", ns.className.c_str());
        if (ImGui::Button(behaviors.empty() ? "Write one in C or C++..." : "Native Code...", {-1, 0}))
            openNativeCode();
        return;
    }
    if (info->properties.empty()) {
        ImGui::TextDisabled("%s has no properties.", info->name.c_str());
        return;
    }
    if (!ImGui::BeginTable("##nativeprops", 2, ImGuiTableFlags_SizingStretchProp))
        return;
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, 110);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    for (auto& p : info->properties) {
        ImGui::PushID(p.name.c_str());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const Json& o = ns.overrides[p.name];
        bool overridden = o.isNumber() || o.isBool();
        double value = o.isNumber() ? o.asNumber() : o.isBool() ? (o.asBool() ? 1 : 0) : p.defaultValue;
        ImGui::AlignTextToFramePadding();
        if (overridden)
            ImGui::TextUnformatted(p.name.c_str());
        else
            ImGui::TextDisabled("%s", p.name.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s%sRight-click the value to reset it.", p.tooltip.c_str(), p.tooltip.empty() ? "" : "\n");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        bool changed = false;
        if (p.type == AVEN_PROPERTY_NUMBER) {
            float f = static_cast<float>(value);
            changed = ImGui::DragFloat("##v", &f, 0.05f);
            value = f;
        } else if (p.type == AVEN_PROPERTY_INTEGER) {
            int i = static_cast<int>(value);
            changed = ImGui::DragInt("##v", &i, 0.2f);
            value = i;
        } else {
            bool b = value != 0;
            changed = ImGui::Checkbox("##v", &b);
            value = b ? 1 : 0;
        }
        if (changed) {
            if (playing_)
                noteLiveChange(e, "NativeScript/" + p.name);
            else
                edited("Change " + p.name);
            if (p.type == AVEN_PROPERTY_FLAG)
                ns.overrides[p.name] = value != 0;
            else
                ns.overrides[p.name] = value;
        }
        if (ImGui::BeginPopupContextItem("reset")) {
            if (ImGui::MenuItem("Reset to default") && overridden) {
                if (!playing_)
                    recordUndo("Reset " + p.name);
                ns.overrides.erase(p.name);
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
    if (playing_)
        ImGui::TextDisabled("Changes apply the next time the game starts.");
}

} // namespace aven::editor
