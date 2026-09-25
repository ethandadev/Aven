// Bug replay: every play session is recorded (inputs, frame times and the random seed), so the
// moments before an error, or before Aven closed unexpectedly, can be played again exactly,
// paused and inspected, or saved as a bug report someone else can replay.

#include "editor.h"

#include "aven/core/fs.h"
#include "aven/platform/input.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <random>

namespace aven::editor {

namespace {

constexpr float kWindow = 20.0f;     // seconds shown before an error
constexpr float kThumbEvery = 0.5f;  // seconds between thumbnails
constexpr int kThumbWidth = 192;

std::string clock(float seconds) {
    int s = static_cast<int>(seconds);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%02d", s / 60, s % 60);
    return buf;
}

std::string timestamp() {
    std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d_%H-%M-%S", std::localtime(&t));
    return buf;
}

} // namespace

stdfs::path Editor::sessionReplayPath() const { return projectDir_ / ".aven" / "last_session.replay"; }

// ---------------------------------------------------------------- recording

void Editor::beginRecording(uint32_t seed, const Json& startScene) {
    clearThumbnails();
    thumbTimer_ = 0;
    recordTime_ = 0;
    replayReason_.clear();
    if (!prefs.recordReplays)
        return;
    Replay header;
    header.seed = seed;
    header.scenePath = scenePath_;
    header.startScene = startScene;
    header.project = settings_.name;
    recorder_.begin(sessionReplayPath(), header);
}

void Editor::recordFrame(float dt) {
    if (!recorder_.recording())
        return;
    recorder_.addFrame(dt, viewportSize_, gameInput_);
    recordTime_ += dt;
    thumbTimer_ += dt;
}

void Editor::endRecording() {
    if (!recorder_.recording())
        return;
    recorder_.end();
    lastReplay_ = recorder_.replay();
}

void Editor::captureThumbnail(int w, int h) {
    if (!recorder_.recording() || thumbTimer_ < kThumbEvery || w <= 0 || h <= 0)
        return;
    thumbTimer_ = 0;
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
    device_.readPixels(renderer_.outputFramebuffer(), 0, 0, 0, w, h, pixels.data());
    Thumb t;
    t.frame = static_cast<int>(recorder_.replay().frames.size());
    t.time = recordTime_;
    t.w = kThumbWidth;
    t.h = std::max(1, kThumbWidth * h / w);
    t.rgba.resize(static_cast<size_t>(t.w) * t.h * 4);
    // Box-sample down and flip (the framebuffer is bottom-up).
    for (int y = 0; y < t.h; ++y)
        for (int x = 0; x < t.w; ++x) {
            int sx = x * w / t.w, sy = (t.h - 1 - y) * h / t.h;
            int sx2 = std::min(w - 1, sx + w / t.w / 2), sy2 = std::min(h - 1, sy + h / t.h / 2);
            for (int c = 0; c < 4; ++c) {
                int sum = pixels[(static_cast<size_t>(sy) * w + sx) * 4 + c] + pixels[(static_cast<size_t>(sy) * w + sx2) * 4 + c] +
                          pixels[(static_cast<size_t>(sy2) * w + sx) * 4 + c] + pixels[(static_cast<size_t>(sy2) * w + sx2) * 4 + c];
                t.rgba[(static_cast<size_t>(y) * t.w + x) * 4 + c] = static_cast<uint8_t>(c == 3 ? 255 : sum / 4);
            }
        }
    thumbs_.push_back(std::move(t));
    while (!thumbs_.empty() && thumbs_.front().time < recordTime_ - kWindow - 0.01f) {
        if (thumbs_.front().texture)
            device_.destroy(thumbs_.front().texture);
        thumbs_.pop_front();
    }
}

void Editor::clearThumbnails() {
    for (auto& t : thumbs_)
        if (t.texture)
            device_.destroy(t.texture);
    thumbs_.clear();
}

void Editor::checkLastSession() {
    Replay r;
    std::error_code ec;
    if (!stdfs::exists(sessionReplayPath(), ec) || !r.load(sessionReplayPath()) || r.cleanExit || r.frames.size() < 30)
        return;
    lastReplay_ = std::move(r);
    // Keep it, but don't ask again next time.
    stdfs::rename(sessionReplayPath(), sessionReplayPath().parent_path() / "crashed_session.replay", ec);
    clearThumbnails();
    replayReason_ = "Aven closed while your game was running.";
    showReplay_ = focusReplay_ = true;
    notify("Aven closed unexpectedly last time while your game was running. The Bug Replay window can play the last 20 "
           "seconds again.", true);
}

// ---------------------------------------------------------------- replaying

void Editor::startReplay(const Replay& replay, int fromFrame) {
    if (replay.empty())
        return;
    if (playing_)
        stop();
    replayData_ = replay;
    console_.clear();
    game_ = makeGame();
    if (!replayPlayer_.start(*game_, gameInput_, replayData_)) {
        game_.reset();
        notify("This replay's scene couldn't be loaded.", true);
        return;
    }
    playing_ = true;
    replaying_ = true;
    paused_ = false;
    pausedOnError_ = false;
    errorPauses_.clear();
    replayShowFrom_ = std::clamp(fromFrame, 0, static_cast<int>(replayData_.frames.size()) - 1);
    replayClock_ = 0;
    replayDone_ = false;
    focusViewport_ = true;
}

void Editor::updateReplay(float dt) {
    if (!game_)
        return;
    int count = replayPlayer_.frameCount();
    // Fast-forward to where the interesting part starts, a slice of time per editor frame.
    if (replayPlayer_.frame() < replayShowFrom_) {
        auto t0 = std::chrono::steady_clock::now();
        while (replayPlayer_.frame() < replayShowFrom_ &&
               std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count() < 25.0f)
            replayPlayer_.step();
        return;
    }
    if (replayDone_)
        return;
    if (paused_ && !stepOnce_)
        return;
    if (stepOnce_) {
        replayPlayer_.step();
        stepOnce_ = false;
    } else {
        // Play at the speed it was recorded (or slower).
        replayClock_ += dt * replaySpeed_;
        while (replayPlayer_.frame() < count) {
            float next = replayData_.frames[static_cast<size_t>(replayPlayer_.frame())].dt;
            if (replayClock_ < next)
                break;
            replayClock_ -= next;
            replayPlayer_.step();
        }
    }
    if (replayPlayer_.frame() >= count) {
        replayDone_ = true;
        paused_ = true;
        notify(replayReason_.empty() ? "That's the end of the recording. Click objects to inspect them."
                                     : "This is the moment the problem happened. Click objects to see their settings.");
    }
}

void Editor::drawReplayBar(ImVec2 pos, ImVec2 size) {
    if (!replaying_)
        return;
    int count = replayPlayer_.frameCount();
    int frame = replayPlayer_.frame();
    float h = ImGui::GetFrameHeight() + 12;
    ImGui::SetCursorScreenPos({pos.x + 10, pos.y + size.y - h - 34});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(30, 64, 175, 235));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 245, 255, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6);
    ImGui::BeginChild("##replaybar", {std::min(size.x - 20, 720.0f), h}, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::AlignTextToFramePadding();
    if (frame < replayShowFrom_) {
        ImGui::Text("REPLAY  Rewinding... %d%%", replayShowFrom_ ? frame * 100 / replayShowFrom_ : 100);
    } else {
        float shown = 0, total = 0;
        for (int i = replayShowFrom_; i < count; ++i) {
            total += replayData_.frames[static_cast<size_t>(i)].dt;
            if (i < frame)
                shown += replayData_.frames[static_cast<size_t>(i)].dt;
        }
        ImGui::Text("REPLAY  %.1f / %.1f s", shown, total);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        float progress = total > 0 ? shown / total : 1;
        ImGui::ProgressBar(progress, {120, 0}, "");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(20, 40, 110, 255));
        if (ImGui::SmallButton(paused_ ? "Play" : "Pause")) {
            paused_ = !paused_;
            if (!paused_ && replayDone_) {
                startReplay(replayData_, replayShowFrom_);
                ImGui::PopStyleColor();
                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
                return;
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Next frame")) {
            paused_ = true;
            stepOnce_ = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(replaySpeed_ < 1 ? "Speed: 1/4" : "Speed: 1x"))
            replaySpeed_ = replaySpeed_ < 1 ? 1.0f : 0.25f;
        ImGui::SameLine();
        if (ImGui::SmallButton("Again"))
            startReplay(replayData_, replayShowFrom_);
        ImGui::SameLine();
        if (ImGui::SmallButton("Exit replay"))
            stop();
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

// ---------------------------------------------------------------- bug reports

std::string Editor::saveBugReport() {
    const Replay& r = lastReplay_;
    if (r.empty())
        return "";
    std::string folder = "bug_reports/" + timestamp();
    stdfs::path dir = projectDir_ / folder;
    std::error_code ec;
    stdfs::create_directories(dir, ec);
    r.save(dir / "session.replay");
    // Pictures of the last moments, and a strip of them side by side.
    int saved = 0;
    if (!thumbs_.empty()) {
        int w = thumbs_.front().w, h = thumbs_.front().h;
        size_t step = std::max<size_t>(1, thumbs_.size() / 8);
        std::vector<const Thumb*> picks;
        for (size_t i = 0; i < thumbs_.size(); i += step)
            picks.push_back(&thumbs_[i]);
        if (picks.back() != &thumbs_.back())
            picks.push_back(&thumbs_.back());
        std::vector<uint8_t> strip(static_cast<size_t>(w) * picks.size() * h * 4);
        for (size_t p = 0; p < picks.size(); ++p) {
            const Thumb& t = *picks[p];
            if (t.w != w || t.h != h)
                continue;
            for (int y = 0; y < h; ++y)
                std::copy_n(t.rgba.begin() + static_cast<ptrdiff_t>(y) * w * 4, w * 4,
                            strip.begin() + (static_cast<ptrdiff_t>(y) * w * static_cast<ptrdiff_t>(picks.size()) + static_cast<ptrdiff_t>(p) * w) * 4);
            char name[32];
            std::snprintf(name, sizeof name, "moment_%02d.png", saved + 1);
            Assets::savePng(dir / name, t.rgba.data(), t.w, t.h, false);
            ++saved;
        }
        Assets::savePng(dir / "filmstrip.png", strip.data(), w * static_cast<int>(picks.size()), h, false);
    }
    std::string md = "# Bug report: " + settings_.name + "\n\n";
    md += "- When: " + timestamp() + "\n";
    md += "- Scene: " + r.scenePath + "\n";
    md += "- Played for: " + clock(r.duration()) + " (" + std::to_string(r.frames.size()) + " frames)\n";
    md += std::string("- Aven: ") + AVEN_VERSION + "\n\n";
    if (!replayReason_.empty())
        md += "## What happened\n\n" + replayReason_ + "\n\n";
    if (!r.events.empty()) {
        md += "## Errors and events\n\n";
        for (auto& e : r.events) {
            float t = 0;
            for (int i = 0; i < std::min(e.frame, static_cast<int>(r.frames.size())); ++i)
                t += r.frames[static_cast<size_t>(i)].dt;
            md += "- " + clock(t) + " " + e.kind + ": " + e.text + (e.file.empty() ? "" : " (" + e.file + " line " + std::to_string(e.line) + ")") + "\n";
        }
        md += "\n";
    }
    if (r.edited())
        md += "Note: things were changed while the game was paused, so the replay may not match exactly.\n\n";
    md += "## How to watch it\n\nOpen this project in Aven, then Window > Bug Replay > Open a saved replay, and pick\n`" + folder +
          "/session.replay`. It plays the game again with the same inputs, so the problem happens again.\n";
    if (saved)
        md += "\n![The last moments](filmstrip.png)\n";
    fs::writeText(dir / "report.md", md);
    scanAssets();
    return folder;
}

// ---------------------------------------------------------------- window

void Editor::openBugReplay() {
    showReplay_ = focusReplay_ = true;
    if (lastReplay_.empty() && recorder_.recording())
        lastReplay_ = recorder_.replay();
    // After a restart, the last session is still on disk.
    if (lastReplay_.empty() && !recorder_.recording())
        lastReplay_.load(sessionReplayPath());
}

void Editor::drawBugReplay() {
    ui::placeWindow({760, 440}, {0.5f, 0.55f});
    if (focusReplay_) {
        ImGui::SetNextWindowFocus();
        focusReplay_ = false;
    }
    if (!ImGui::Begin("Bug Replay###BugReplay", &showReplay_)) {
        ImGui::End();
        return;
    }
    // While playing, the replay can be taken from the recording so far.
    if (recorder_.recording() && !replaying_)
        lastReplay_ = recorder_.replay();
    const Replay& r = lastReplay_;
    if (r.empty()) {
        ImGui::TextWrapped("Every time you press Play, Aven records what happens (it's tiny: just the keys, the mouse and "
                           "the timing). When something goes wrong, you can play the last 20 seconds again exactly, pause, "
                           "step frame by frame and inspect every object.");
        if (!prefs.recordReplays)
            ImGui::TextColored({1, 0.7f, 0.3f, 1}, "Recording is turned off in Preferences > Behavior.");
    } else {
        ImGui::PushFont(fonts.bold);
        ImGui::TextUnformatted(replayReason_.empty() ? ("Your last play session (" + clock(r.duration()) + ")").c_str()
                                                     : replayReason_.c_str());
        ImGui::PopFont();
        ImGui::TextDisabled("%s, %d frames recorded", r.scenePath.c_str(), static_cast<int>(r.frames.size()));
        if (r.edited())
            ImGui::TextColored({1, 0.8f, 0.35f, 1}, "Things were changed while paused, so the replay may drift from what you saw.");

        // Filmstrip of the last seconds.
        float thumbH = 96;
        if (!thumbs_.empty()) {
            ImGui::BeginChild("##film", {0, thumbH + ImGui::GetTextLineHeightWithSpacing() + 18}, ImGuiChildFlags_None,
                              ImGuiWindowFlags_HorizontalScrollbar);
            float end = thumbs_.back().time;
            for (size_t i = 0; i < thumbs_.size(); ++i) {
                Thumb& t = thumbs_[i];
                if (!t.texture) {
                    rhi::TextureDesc desc;
                    desc.width = t.w;
                    desc.height = t.h;
                    desc.data = t.rgba.data();
                    desc.label = "replay thumbnail";
                    t.texture = device_.createTexture(desc);
                }
                if (i)
                    ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::PushID(static_cast<int>(i));
                float w = thumbH * static_cast<float>(t.w) / static_cast<float>(t.h);
                if (ImGui::ImageButton("##thumb", static_cast<ImTextureID>(device_.nativeTexture(t.texture)), {w, thumbH}))
                    startReplay(r, std::max(0, t.frame - 1));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Replay from here");
                ImGui::TextDisabled("-%.1fs", end - t.time);
                ImGui::PopID();
                ImGui::EndGroup();
            }
            ImGui::SetScrollHereX(1.0f);
            ImGui::EndChild();
        } else {
            ImGui::TextDisabled("(No pictures for this recording. Press Replay to watch it.)");
        }

        // Timeline of the last 20 seconds: errors, and when keys were pressed.
        int from = r.frameAtSecondsBeforeEnd(kWindow);
        float total = 0;
        for (size_t i = static_cast<size_t>(from); i < r.frames.size(); ++i)
            total += r.frames[i].dt;
        ImVec2 p = ImGui::GetCursorScreenPos();
        float barW = ImGui::GetContentRegionAvail().x, barH = 22;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, {p.x + barW, p.y + barH}, ImGui::GetColorU32(ImGuiCol_FrameBg), 4);
        auto xAt = [&](int frame) {
            float t = 0;
            for (int i = from; i < std::min(frame, static_cast<int>(r.frames.size())); ++i)
                t += r.frames[static_cast<size_t>(i)].dt;
            return p.x + (total > 0 ? t / total : 1) * barW;
        };
        for (size_t i = static_cast<size_t>(from) + 1; i < r.frames.size(); ++i) {
            const Json& a = r.frames[i].input["k"];
            const Json& b = r.frames[i - 1].input["k"];
            if (a.size() > b.size() || r.frames[i].input["m"].size() > r.frames[i - 1].input["m"].size()) {
                float x = xAt(static_cast<int>(i));
                dl->AddLine({x, p.y + 12}, {x, p.y + barH - 3}, ImGui::GetColorU32(ImGuiCol_TextDisabled));
            }
        }
        for (auto& e : r.events) {
            if (e.frame < from)
                continue;
            float x = xAt(e.frame);
            ImU32 c = e.kind == "error" ? IM_COL32(239, 68, 68, 255) : e.kind == "warning" ? IM_COL32(245, 190, 60, 255) : IM_COL32(96, 165, 250, 255);
            dl->AddRectFilled({x - 2, p.y + 2}, {x + 2, p.y + barH - 2}, c, 1);
        }
        ImGui::Dummy({barW, barH});
        ImGui::TextDisabled("The last %.0f seconds. Red: errors, yellow: warnings, blue: changes while paused, grey: key and mouse presses.", total);

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(37, 99, 235, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
        if (ImGui::Button("Replay the last 20 seconds"))
            startReplay(r, from);
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (ImGui::Button("Replay from the start"))
            startReplay(r, 0);
        ImGui::SameLine();
        if (ImGui::Button("Save a bug report")) {
            std::string folder = saveBugReport();
            if (!folder.empty())
                notify("Saved " + folder + ": the replay, pictures of the last moments and report.md.");
        }
        if (!r.cleanExit && !replaying_ && !recorder_.recording())
            ImGui::TextColored({1, 0.7f, 0.35f, 1}, "If Aven itself crashed, the replay may crash it again. A saved bug report is "
                                                    "the safe way to share it.");

        // Errors in the recording.
        bool any = false;
        for (auto& e : r.events)
            if (e.kind == "error" || e.kind == "warning") {
                if (!any) {
                    ImGui::Separator();
                    any = true;
                }
                ImGui::PushID(&e);
                ImGui::TextColored(e.kind == "error" ? ImVec4(1, 0.5f, 0.5f, 1) : ImVec4(1, 0.8f, 0.35f, 1), "%s", e.text.c_str());
                if (unlocked(Feature::Doctor)) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Doctor"))
                        openDoctorFor(e.text, e.file, e.line);
                }
                ImGui::PopID();
            }
    }

    // Saved replays in this project.
    ImGui::Separator();
    if (ImGui::BeginCombo("##open", "Open a saved replay...")) {
        std::vector<std::string> files;
        std::error_code ec;
        for (auto it = stdfs::recursive_directory_iterator(projectDir_ / "bug_reports", ec); !ec && it != stdfs::recursive_directory_iterator();
             it.increment(ec))
            if (it->path().extension() == ".replay")
                files.push_back(stdfs::relative(it->path(), projectDir_, ec).generic_string());
        std::sort(files.rbegin(), files.rend());
        if (files.empty())
            ImGui::TextDisabled("No saved bug reports yet.");
        for (auto& f : files)
            if (ImGui::Selectable(f.c_str())) {
                Replay loaded;
                std::string error;
                if (loaded.load(projectDir_ / f, &error)) {
                    lastReplay_ = std::move(loaded);
                    clearThumbnails();
                    replayReason_ = "Saved replay: " + f;
                } else {
                    notify(error, true);
                }
            }
        ImGui::EndCombo();
    }
    ImGui::End();
}

} // namespace aven::editor
