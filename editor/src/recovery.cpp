// Crash recovery. While there are unsaved changes, a copy of them goes to the user data folder
// every 30 seconds: the scene (or prefab) being edited and every script with unsaved edits. The
// real files are never touched. Saving everything, or closing Aven normally (whatever the answer to
// "Save changes?"), removes the copy; if Aven closes any other way (a crash, a power cut), opening
// the project again offers the changes back.

#include "editor.h"

#include "block_editor.h"
#include "code_editor.h"

#include "aven/core/fs.h"
#include "aven/core/log.h"
#include "aven/core/update.h"

#include <imgui.h>

#include <chrono>
#include <cstdlib>

namespace aven::editor {

namespace {

constexpr float kRecoveryEvery = 30.0f; // seconds

int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string timeAgo(int64_t seconds) {
    if (seconds < 60)
        return "moments ago";
    if (seconds < 90)
        return "a minute ago";
    if (seconds < 3600)
        return std::to_string(seconds / 60) + " minutes ago";
    if (seconds < 2 * 3600)
        return "an hour ago";
    if (seconds < 48 * 3600)
        return std::to_string(seconds / 3600) + " hours ago";
    return std::to_string(seconds / 86400) + " days ago";
}

} // namespace

stdfs::path Editor::recoveryDir() const {
    // One folder per project, named after a fingerprint of where it is.
    std::string where = projectDir_.string();
    return fs::userDataDir("Aven Editor") / "recovery" / update::sha256(where.data(), where.size()).substr(0, 16);
}

bool Editor::hasUnsavedWork() const {
    if (!hasProject())
        return false;
    if (dirty_)
        return true;
    for (auto& t : tabs_)
        if (t->modified)
            return true;
    return false;
}

void Editor::updateRecovery(float dt) {
    if (!hasProject() || pendingRecovery_.isObject())
        return;
    if (!hasUnsavedWork()) {
        if (recoveryWritten_)
            clearRecovery(); // everything's saved: nothing to recover
        recoveryTimer_ = 0;
        return;
    }
    if (!options_.screenshot.empty())
        return; // automated runs write a copy only when asked ("recoverysnapshot")
    recoveryTimer_ += dt;
    if (recoveryTimer_ >= kRecoveryEvery) {
        recoveryTimer_ = 0;
        writeRecovery();
    }
}

void Editor::writeRecovery() {
    if (!hasProject())
        return;
    stdfs::path dir = recoveryDir();
    std::error_code ec;
    stdfs::create_directories(dir, ec);
    Json info = Json::object();
    info["project"] = projectDir_.string();
    info["scene"] = editingPrefab() ? prefabPath_ : scenePath_;
    info["prefab"] = editingPrefab();
    info["time"] = static_cast<double>(nowSeconds());
    info["version"] = AVEN_VERSION;
    if (dirty_) {
        Json scene = editingPrefab() ? scene_->saveEntities(scene_->roots()) : scene_->save();
        fs::writeText(dir / "scene.json", scene.dump());
        info["sceneChanged"] = true;
    } else {
        stdfs::remove(dir / "scene.json", ec);
    }
    Json scripts = Json::array();
    for (auto& t : tabs_) {
        if (!t->modified)
            continue;
        Json s = Json::object();
        s["path"] = t->path;
        s["text"] = t->code ? t->code->text() : t->blocks ? t->blocks->save().dump(2) : std::string();
        scripts.push(s);
    }
    info["scripts"] = scripts;
    fs::writeText(dir / "info.json", info.dump(1));
    recoveryWritten_ = true;
}

void Editor::clearRecovery() {
    if (!hasProject())
        return;
    std::error_code ec;
    stdfs::remove_all(recoveryDir(), ec);
    recoveryWritten_ = false;
}

void Editor::checkRecovery() {
    pendingRecovery_ = Json();
    auto text = fs::readText(recoveryDir() / "info.json");
    if (!text)
        return;
    Json info = Json::parse(*text);
    std::error_code ec;
    if (!info.isObject() || !stdfs::equivalent(stdfs::path(info["project"].asString("")), projectDir_, ec))
        return;
    if (!info["sceneChanged"].asBool(false) && info["scripts"].size() == 0)
        return;
    pendingRecovery_ = info;
    Log::info("recovery: unsaved changes from ", timeAgo(nowSeconds() - static_cast<int64_t>(info["time"].asNumber(0))));
}

void Editor::recover(bool keep) {
    Json info = pendingRecovery_;
    pendingRecovery_ = Json();
    if (!keep) {
        clearRecovery();
        notify("Thrown away.");
        return;
    }
    std::string scenePath = info["scene"].asString("");
    int restored = 0;
    if (info["sceneChanged"].asBool(false)) {
        std::string error;
        Json data = Json::parse(fs::readText(recoveryDir() / "scene.json").value_or(""), &error);
        if (error.empty() && data.isObject()) {
            if (info["prefab"].asBool(false)) {
                openPrefab(scenePath);
                auto s = std::make_unique<Scene>();
                s->name = stdfs::path(scenePath).stem().string();
                s->instantiate(data);
                scene_ = std::move(s);
            } else {
                auto s = std::make_unique<Scene>();
                if (s->load(data, &error)) {
                    if (scenePath_ != scenePath && !scenePath.empty())
                        openScene(scenePath);
                    scene_ = std::move(s);
                    scenePath_ = scenePath;
                }
            }
            if (error.empty()) {
                dirty_ = true;
                undo_.clear();
                redo_.clear();
                snapshotValid_ = false;
                selection_.clear();
                refreshTitle();
                ++restored;
            } else {
                notify("The recovered scene couldn't be read: " + error, true);
            }
        }
    }
    for (auto& s : info["scripts"].elements()) {
        std::string path = s["path"].asString(""), content = s["text"].asString("");
        if (path.empty() || fs::insideFolder(projectDir_, path).empty())
            continue;
        openScript(path);
        for (auto& t : tabs_)
            if (t->path == path) {
                if (t->code)
                    t->code->setText(content);
                else if (t->blocks)
                    t->blocks->load(Json::parse(content));
                t->modified = true;
                ++restored;
            }
    }
    // Still unsaved (until you save): the copy stays until then, in case Aven closes again.
    writeRecovery();
    notify(restored ? "Recovered your unsaved changes. Save to keep them." : "There was nothing left to recover.", !restored);
}

void Editor::drawRecoveryPrompt() {
    if (!pendingRecovery_.isObject() || !hasProject() || playing_)
        return;
    if (!ImGui::IsPopupOpen("Recover unsaved changes?"))
        ImGui::OpenPopup("Recover unsaved changes?");
    ImGui::SetNextWindowSize({ui::px(520), 0});
    if (!ImGui::BeginPopupModal("Recover unsaved changes?", nullptr, ImGuiWindowFlags_NoResize))
        return;
    const Json& info = pendingRecovery_;
    ImGui::PushTextWrapPos(0);
    int64_t ago = nowSeconds() - static_cast<int64_t>(info["time"].asNumber(0));
    ImGui::Text("Aven closed before these changes were saved (the last copy is from %s):", timeAgo(std::max<int64_t>(ago, 0)).c_str());
    if (info["sceneChanged"].asBool(false))
        ImGui::BulletText("%s", info["scene"].asString("the scene").c_str());
    for (auto& s : info["scripts"].elements())
        ImGui::BulletText("%s", s["path"].asString("").c_str());
    ImGui::Spacing();
    ImGui::TextDisabled("Recovering opens them with the changes, not saved yet; Save keeps them.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
    if (ImGui::Button("Recover them", {w, ui::px(32)})) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        recover(true);
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Throw them away", {w, ui::px(32)})) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        recover(false);
        return;
    }
    ImGui::EndPopup();
}

} // namespace aven::editor
