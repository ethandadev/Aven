// aven-editor <project> --check: checks a game without opening a window, for build scripts and CI.
// It compiles every script and looks for likely mistakes (the code editor's checks), loads every
// scene and prefab, and makes sure the files they use exist. Problems print as
// "file:line:col: error: message" (the format CI systems and editors turn into annotations).
// Exit code: 0 = fine, 1 = errors (or warnings with --strict), 2 = not a project.

#include "check.h"

#include "aven/blocks/blocks.h"
#include "aven/core/fs.h"
#include "aven/core/json.h"
#include "aven/core/log.h"
#include "aven/platform/input.h"
#include "aven/runtime/project.h"
#include "aven/scene/reflection.h"
#include "aven/scene/scene.h"
#include "aven/script/intel.h"

#include <algorithm>
#include <cstdio>
#include <set>

namespace aven::editor {

namespace stdfs = std::filesystem;

namespace {

struct Report {
    int errors = 0, warnings = 0;
    void add(const std::string& file, int line, int col, bool error, const std::string& message) {
        std::printf("%s", file.c_str());
        if (line > 0)
            std::printf(":%d", line);
        if (line > 0 && col > 0)
            std::printf(":%d", col);
        std::printf(": %s: %s\n", error ? "error" : "warning", message.c_str());
        ++(error ? errors : warnings);
    }
};

std::vector<std::string> projectFiles(const stdfs::path& dir) {
    std::vector<std::string> files;
    std::error_code ec, walk;
    for (auto it = stdfs::recursive_directory_iterator(dir, stdfs::directory_options::skip_permission_denied, walk);
         !walk && it != stdfs::recursive_directory_iterator(); it.increment(walk)) {
        std::string name = it->path().filename().string();
        bool skip = (!name.empty() && name[0] == '.') || (it->is_directory(ec) && (name == "exports" || name == "captures"));
        if (skip) {
            if (it->is_directory(ec))
                it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec))
            files.push_back(fs::relativePath(it->path(), dir));
    }
    std::sort(files.begin(), files.end());
    return files;
}

} // namespace

int runProjectCheck(const stdfs::path& dir, bool strict) {
    ProjectSettings settings;
    std::string error;
    if (!settings.load(dir, &error)) {
        std::printf("%s\n", error.c_str());
        return 2;
    }
    // Engine messages (like a scene naming an unknown component) count as warnings too.
    Report report;
    std::string currentFile;
    int sink = Log::addSink([&](const LogMessage& m) {
        if (m.level == LogLevel::Error || m.level == LogLevel::Warning)
            report.add(m.file.empty() ? currentFile : m.file, m.line, 0, m.level == LogLevel::Error, m.text);
    });
    Log::setEchoToStdout(false);

    std::vector<std::string> files = projectFiles(dir);
    std::set<std::string> fileSet(files.begin(), files.end());
    auto exists = [&](const std::string& path) { return fileSet.count(path) > 0; };

    // What scripts can refer to: the engine's names, plus this project's files, objects and tags.
    script::ProjectIndex ix;
    ix.fillFromEngine();
    ix.files = files;
    ix.keyNames = Input::allNames();
    Input input;
    input.loadActions(settings.inputActions);
    for (auto& a : input.actions())
        ix.keyNames.push_back(a.name);
    auto addUnique = [](std::vector<std::string>& list, const std::string& v) {
        if (!v.empty() && std::find(list.begin(), list.end(), v) == list.end())
            list.push_back(v);
    };
    std::vector<std::pair<std::string, std::string>> scripts; // path, EasyScript source
    int scenes = 0;
    for (auto& f : files) {
        std::string ext = fs::extension(f);
        if (ext == ".scene" || ext == ".prefab") {
            if (auto text = fs::readText(dir / f)) {
                Json data = Json::parse(*text);
                for (auto& e : data["entities"].elements()) {
                    addUnique(ix.objectNames, e["name"].asString(""));
                    addUnique(ix.tags, e["tag"].asString(""));
                }
            }
        } else if (ext == ".es" || ext == ".blocks") {
            auto text = fs::readText(dir / f);
            if (!text) {
                report.add(f, 0, 0, true, "couldn't be read");
                continue;
            }
            currentFile = f;
            std::string source = ext == ".es" ? *text : blocks::compileFile(*text, nullptr);
            ix.scanScript(source);
            scripts.emplace_back(f, std::move(source));
        }
    }

    // Scripts: syntax errors and likely mistakes.
    for (auto& [path, source] : scripts) {
        script::CodeIntel intel(ix, script::CodeKind::EasyScript);
        bool blocks = fs::extension(path) == ".blocks";
        for (auto& d : intel.diagnose(source))
            report.add(path, blocks ? 0 : d.line + 1, blocks ? 0 : d.col + 1, d.error, d.message);
    }

    // Scenes and prefabs: they load, and every file they use exists.
    for (auto& f : files) {
        std::string ext = fs::extension(f);
        if (ext != ".scene" && ext != ".prefab")
            continue;
        ++scenes;
        currentFile = f;
        auto text = fs::readText(dir / f);
        std::string parseError;
        Json data = text ? Json::parse(*text, &parseError) : Json();
        if (!parseError.empty()) {
            report.add(f, 0, 0, true, parseError);
            continue;
        }
        Scene scene;
        std::string loadError;
        if (!scene.load(data, &loadError)) {
            report.add(f, 0, 0, true, loadError);
            continue;
        }
        auto& reg = scene.registry();
        scene.walk([&](Entity e, int) {
            const std::string& name = scene.info(e).name;
            std::string layer = scene.info(e).layer;
            if (!layer.empty() && layer != "Default" && settings.layerIndex(layer) == 0)
                report.add(f, 0, 0, false, "'" + name + "' is on the layer '" + layer + "', which isn't in Project Settings");
            for (auto& ci : ComponentRegistry::all()) {
                void* c = ci.get(reg, e);
                if (!c)
                    continue;
                for (auto& field : ci.fields) {
                    if (field.type != FieldType::Asset || field.options.runtime)
                        continue;
                    const std::string& path = field.ref<std::string>(c);
                    if (!path.empty() && !exists(path) && path.find('.') != std::string::npos)
                        report.add(f, 0, 0, true, "'" + name + "' " + ci.name + " uses " + path + ", which isn't in the project");
                }
            }
            return true;
        });
    }
    currentFile.clear();
    if (!exists(settings.startScene))
        report.add(ProjectSettings::kFileName, 0, 0, true, "the start scene " + settings.startScene + " isn't in the project");

    Log::removeSink(sink);
    Log::setEchoToStdout(true);
    std::printf("Checked %d scripts and %d scenes/prefabs: %d error%s, %d warning%s.\n", static_cast<int>(scripts.size()), scenes,
                report.errors, report.errors == 1 ? "" : "s", report.warnings, report.warnings == 1 ? "" : "s");
    return report.errors > 0 || (strict && report.warnings > 0) ? 1 : 0;
}

} // namespace aven::editor
