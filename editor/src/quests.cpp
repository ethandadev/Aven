// Contributor quests: small, guided ways to help build Aven (quests/*.json). The panel walks
// through the steps, opens the files involved and checks the work, so a first open-source
// contribution feels like a game quest instead of a wall of code.

#include "editor.h"

#include "aven/core/fs.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>

namespace aven::editor {

namespace {

stdfs::path questsDir() {
    std::error_code ec;
    if (!sourceDir().empty() && stdfs::exists(sourceDir() / "quests", ec))
        return sourceDir() / "quests";
    return fs::executableDir() / "quests";
}

// Files matching a simple pattern with at most one '*' per path part: "templates/*/template.json".
std::vector<stdfs::path> glob(const stdfs::path& root, const std::string& pattern) {
    std::vector<stdfs::path> found{root};
    size_t start = 0;
    while (start <= pattern.size()) {
        size_t slash = pattern.find('/', start);
        std::string part = pattern.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        std::vector<stdfs::path> next;
        for (auto& dir : found) {
            size_t star = part.find('*');
            if (star == std::string::npos) {
                std::error_code ec;
                if (stdfs::exists(dir / part, ec))
                    next.push_back(dir / part);
                continue;
            }
            std::string before = part.substr(0, star), after = part.substr(star + 1);
            std::error_code ec;
            for (auto& entry : stdfs::directory_iterator(dir, ec)) {
                std::string name = entry.path().filename().string();
                if (name.size() >= before.size() + after.size() && name.compare(0, before.size(), before) == 0 &&
                    name.compare(name.size() - after.size(), after.size(), after) == 0)
                    next.push_back(entry.path());
            }
        }
        found = std::move(next);
        if (slash == std::string::npos)
            break;
        start = slash + 1;
    }
    std::sort(found.begin(), found.end());
    return found;
}

ImVec4 levelColor(const std::string& level) {
    if (level == "Starter")
        return {0.35f, 0.8f, 0.5f, 1};
    if (level == "Easy")
        return {0.4f, 0.65f, 1.0f, 1};
    return {1.0f, 0.65f, 0.3f, 1};
}

} // namespace

void Editor::loadQuests() {
    quests_.clear();
    std::error_code ec;
    for (auto& path : glob(questsDir(), "*.json")) {
        auto text = fs::readText(path);
        std::string error;
        Json q = text ? Json::parse(*text, &error) : Json();
        if (!error.empty() || !q.isObject()) {
            Log::warn("Quest ", path.filename().string(), " couldn't be read: ", error);
            continue;
        }
        quests_.push_back(std::move(q));
    }
    static const char* order[] = {"Starter", "Easy", "Medium"};
    auto rank = [&](const Json& q) {
        for (int i = 0; i < 3; ++i)
            if (q["level"].asString("") == order[i])
                return i;
        return 3;
    };
    std::stable_sort(quests_.begin(), quests_.end(), [&](const Json& a, const Json& b) { return rank(a) < rank(b); });
    questsLoaded_ = true;
}

// Whether a step's check passes (-1: the step has no automatic check).
int Editor::questCheck(const Json& check) {
    if (!check.isObject())
        return -1;
    std::string type = check["type"].asString("");
    std::string path = check["path"].asString("");
    bool inProject = type == "project_files";
    stdfs::path root = inProject ? projectDir_ : sourceDir();
    if (root.empty())
        return 0;
    auto files = glob(root, path);
    if (type == "files" || type == "project_files")
        return static_cast<int>(files.size()) >= check["min"].asInt(1) ? 1 : 0;
    if (type == "json_valid") {
        if (files.empty())
            return 0;
        for (auto& f : files) {
            std::string error;
            Json::parse(fs::readText(f).value_or(""), &error);
            if (!error.empty())
                return 0;
        }
        return 1;
    }
    if (type == "contains") {
        for (auto& f : files)
            if (fs::readText(f).value_or("").find(check["text"].asString("")) != std::string::npos)
                return 1;
        return 0;
    }
    return -1;
}

bool Editor::packageTemplate(std::string& message) {
    TemplatePackage& t = templatePackage_;
    std::string id;
    for (char c : t.id)
        id += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '-';
    if (id.empty() || t.name.empty()) {
        message = "Give the template a short id and a name first.";
        return false;
    }
    stdfs::path dest = sourceDir().empty() ? projectDir_ / "exports" / ("template-" + id) : sourceDir() / "templates" / id;
    std::error_code ec;
    if (stdfs::exists(dest, ec) && !stdfs::exists(dest / "template.json", ec)) {
        message = dest.string() + " already exists and isn't a template. Pick another id.";
        return false;
    }
    saveScene();
    saveAllScripts();
    copyGameFiles(dest, projectDir_ / "exports", nullptr, GameCopy::Template);
    Json info = Json::object();
    info["name"] = t.name;
    info["description"] = t.description;
    info["style"] = t.style;
    info["difficulty"] = t.difficulty;
    info["3d"] = view3D_;
    Color c = prefs.accentColor();
    char hex[8];
    std::snprintf(hex, sizeof hex, "%02X%02X%02X", static_cast<int>(c.r * 255), static_cast<int>(c.g * 255), static_cast<int>(c.b * 255));
    info["color"] = hex;
    fs::writeText(dest / "template.json", info.dump(2) + "\n");
    // A picture for the New Game screen.
    std::vector<uint8_t> shot = renderStartScene(640, 360);
    if (!shot.empty())
        Assets::savePng(dest / "thumbnail.png", shot.data(), 640, 360, false);
    templateCache_.clear();
    templatesScanned_ = false;
    message = "Packaged as a template in " + dest.string() + (sourceDir().empty() ? " (copy it into Aven's templates folder)." : ".");
    return true;
}

void Editor::drawQuests() {
    ui::placeWindow({900, 600}, {0.5f, 0.5f});
    if (focusQuests_) {
        ImGui::SetNextWindowFocus();
        focusQuests_ = false;
    }
    if (!ImGui::Begin("Contributor Quests###Quests", &showQuests_)) {
        ImGui::End();
        return;
    }
    if (!questsLoaded_)
        loadQuests();
    if (quests_.empty()) {
        ImGui::TextWrapped("No quests found. They live in the quests folder of Aven's source code.");
        ImGui::End();
        return;
    }
    questIndex_ = std::clamp(questIndex_, 0, static_cast<int>(quests_.size()) - 1);

    // Quest list.
    ImGui::BeginChild("##questlist", {280, 0}, ImGuiChildFlags_Border);
    ImGui::PushFont(fonts.bold);
    ImGui::TextUnformatted("Help build Aven");
    ImGui::PopFont();
    ImGui::TextDisabled("Small tasks, one step at a time.");
    ImGui::Separator();
    for (size_t i = 0; i < quests_.size(); ++i) {
        const Json& q = quests_[i];
        std::string id = q["id"].asString("");
        bool done = prefs.counters.count("quest_done:" + id) > 0;
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable("##q", questIndex_ == static_cast<int>(i), 0, {0, ImGui::GetTextLineHeight() * 2.3f}))
            questIndex_ = static_cast<int>(i);
        ImGui::SameLine(8);
        ImGui::BeginGroup();
        ImGui::TextColored(levelColor(q["level"].asString("")), "%s%s", q["level"].asString("").c_str(), done ? "  (done)" : "");
        ImGui::TextUnformatted(q["title"].asString("").c_str());
        ImGui::EndGroup();
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // The chosen quest.
    const Json& q = quests_[static_cast<size_t>(questIndex_)];
    std::string id = q["id"].asString("");
    ImGui::BeginChild("##quest");
    ImGui::PushFont(fonts.big);
    ImGui::TextUnformatted(q["title"].asString("").c_str());
    ImGui::PopFont();
    ImGui::TextColored(levelColor(q["level"].asString("")), "%s", q["level"].asString("").c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("|  about %s  |  %s", q["time"].asString("?").c_str(), q["touches"].asString("").c_str());
    ImGui::PushTextWrapPos(0);
    ImGui::Spacing();
    ImGui::TextUnformatted(q["summary"].asString("").c_str());
    ImGui::TextColored({0.6f, 0.8f, 1.0f, 1}, "Why it matters: %s", q["why"].asString("").c_str());
    std::string learn;
    for (auto& l : q["learn"].elements())
        learn += (learn.empty() ? "" : ", ") + l.asString("");
    if (!learn.empty())
        ImGui::TextDisabled("You'll learn: %s", learn.c_str());
    ImGui::PopTextWrapPos();

    if (sourceDir().empty()) {
        ImGui::Spacing();
        ImGui::TextColored({1, 0.75f, 0.35f, 1}, "This Aven wasn't built from its source code, so files can't be opened or checked here.");
        ImGui::TextWrapped("Get the code (it's free and open source), build it, and run that Aven to do quests:");
        std::string cmd = "git clone https://github.com/ethandadev/Aven\ncd Aven\ncmake -B build -G Ninja && cmake --build build";
        ImGui::InputTextMultiline("##clone", &cmd, {-1, ImGui::GetTextLineHeight() * 4}, ImGuiInputTextFlags_ReadOnly);
    }

    ui::sectionHeader("Steps");
    int total = 0, finished = 0;
    const Json& steps = q["steps"];
    for (size_t i = 0; i < steps.size(); ++i) {
        const Json& step = steps[i];
        ImGui::PushID(static_cast<int>(i));
        std::string key = "quest:" + id + ":" + std::to_string(i);
        int auto_ = questCheck(step["check"]);
        bool ticked = auto_ == 1 || prefs.counters.count(key) > 0;
        ++total;
        finished += ticked;
        bool box = ticked;
        ImGui::BeginDisabled(auto_ >= 0);
        if (ImGui::Checkbox("##step", &box)) {
            if (box)
                prefs.counters[key] = 1;
            else
                prefs.counters.erase(key);
        }
        ImGui::EndDisabled();
        if (auto_ >= 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Aven checks this one for you.");
        ImGui::SameLine();
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(ticked ? ImVec4(0.6f, 0.65f, 0.7f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%d. %s", static_cast<int>(i) + 1,
                           step["text"].asString("").c_str());
        ImGui::PopTextWrapPos();
        if (step.contains("open") && !sourceDir().empty()) {
            std::string file = step["open"].asString("");
            ImGui::Indent(ImGui::GetFrameHeight() + 8);
            if (ImGui::SmallButton(("Open " + file).c_str()))
                openExternal((sourceDir() / file).string());
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy path"))
                ImGui::SetClipboardText((sourceDir() / file).string().c_str());
            ImGui::Unindent(ImGui::GetFrameHeight() + 8);
        }
        ImGui::PopID();
    }
    ImGui::ProgressBar(total ? static_cast<float>(finished) / total : 0, {-1, 0},
                       (std::to_string(finished) + " of " + std::to_string(total) + " steps").c_str());

    // Built-in tools some quests use.
    if (q["tool"].asString("") == "package-template" && hasProject()) {
        ui::sectionHeader("Package as a template");
        TemplatePackage& t = templatePackage_;
        if (t.name.empty()) {
            t.name = settings_.name;
            t.id = safeGameName();
            t.description = gameDescription();
        }
        ImGui::InputText("Id (folder name)", &t.id);
        ImGui::InputText("Name", &t.name);
        ImGui::InputTextMultiline("Description", &t.description, {-1, ImGui::GetTextLineHeight() * 3});
        const char* styles[] = {"Behaviors", "Blocks", "EasyScript", "Blocks + EasyScript"};
        if (ImGui::BeginCombo("Made with", t.style.c_str())) {
            for (const char* s : styles)
                if (ImGui::Selectable(s, t.style == s))
                    t.style = s;
            ImGui::EndCombo();
        }
        const char* levels[] = {"Beginner", "Intermediate", "Advanced"};
        if (ImGui::BeginCombo("Difficulty", t.difficulty.c_str())) {
            for (const char* l : levels)
                if (ImGui::Selectable(l, t.difficulty == l))
                    t.difficulty = l;
            ImGui::EndCombo();
        }
        if (ImGui::Button("Package as a template"))
            packageTemplate(questMessage_);
    }
    if (!questMessage_.empty())
        ImGui::TextWrapped("%s", questMessage_.c_str());

    ui::sectionHeader("When you're done");
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(q["submit"].asString("").c_str());
    ImGui::PopTextWrapPos();
    bool done = prefs.counters.count("quest_done:" + id) > 0;
    if (!done && finished == total && ImGui::Button("I sent it! Mark this quest done")) {
        prefs.counters["quest_done:" + id] = 1;
        milestone("quests");
        notify("Thank you for helping build Aven!");
    }
    if (done)
        ImGui::TextColored({0.45f, 0.9f, 0.6f, 1}, "Quest complete. Thank you!");
    ImGui::SameLine();
    if (ImGui::SmallButton("Open Aven on GitHub"))
        openExternal("https://github.com/ethandadev/Aven");
    ImGui::EndChild();
    ImGui::End();
}

} // namespace aven::editor
