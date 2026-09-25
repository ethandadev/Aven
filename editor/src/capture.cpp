// Capture: a screenshot of the game view (F12) and GIF recordings (Shift+F12), saved in
// the project's captures/ folder, ready to post or put on a game page.

#include "editor.h"

#include "aven/core/fs.h"

#include <gif.h>

#include <imgui.h>

#include <algorithm>
#include <ctime>
#include <thread>

namespace aven::editor {

namespace {

constexpr int kGifFps = 15;
constexpr float kGifMaxSeconds = 12;
constexpr int kGifMaxWidth = 480;

std::string stamp() {
    std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y%m%d_%H%M%S", std::localtime(&t));
    return buf;
}

// The viewport image, top row first.
std::vector<uint8_t> readView(rhi::Device& device, SceneRenderer& renderer, int w, int h) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    device.readPixels(renderer.outputFramebuffer(), 0, 0, 0, w, h, px.data());
    std::vector<uint8_t> flipped(px.size());
    for (int y = 0; y < h; ++y)
        std::copy_n(px.begin() + static_cast<ptrdiff_t>(y) * w * 4, w * 4, flipped.begin() + static_cast<ptrdiff_t>(h - 1 - y) * w * 4);
    for (size_t i = 3; i < flipped.size(); i += 4)
        flipped[i] = 255;
    return flipped;
}

} // namespace

void Editor::takeScreenshot() { screenshotPending_ = true; }

void Editor::toggleGifRecording() {
    if (gifRecording_) {
        finishGif();
        return;
    }
    gifFrames_.clear();
    gifTimer_ = 0;
    gifTime_ = 0;
    gifRecording_ = true;
    notify("Recording a GIF of the game view. Press Shift+F12 (or the red button) to stop.");
}

// Called right after the viewport is drawn.
void Editor::captureView(int w, int h, float dt) {
    if (w <= 0 || h <= 0)
        return;
    if (screenshotPending_) {
        screenshotPending_ = false;
        auto px = readView(device_, renderer_, w, h);
        std::string path = "captures/screenshot_" + stamp() + ".png";
        std::error_code ec;
        stdfs::create_directories(projectDir_ / "captures", ec);
        if (Assets::savePng(projectDir_ / path, px.data(), w, h, false)) {
            notify("Saved " + path);
            scanAssets();
        }
    }
    if (!gifRecording_)
        return;
    gifTime_ += dt;
    gifTimer_ += dt;
    if (gifTimer_ < 1.0f / kGifFps)
        return;
    gifTimer_ = 0;
    auto px = readView(device_, renderer_, w, h);
    // Downscale so the file stays small enough to share.
    int gw = std::min(w, kGifMaxWidth), gh = std::max(1, h * gw / w);
    GifFrame f;
    f.w = gw;
    f.h = gh;
    f.rgba.resize(static_cast<size_t>(gw) * gh * 4);
    for (int y = 0; y < gh; ++y)
        for (int x = 0; x < gw; ++x)
            std::copy_n(&px[(static_cast<size_t>(y * h / gh) * w + x * w / gw) * 4], 4, &f.rgba[(static_cast<size_t>(y) * gw + x) * 4]);
    if (!gifFrames_.empty() && (gifFrames_.front().w != gw || gifFrames_.front().h != gh))
        return; // the view was resized mid-recording; keep the size it started with
    gifFrames_.push_back(std::move(f));
    if (gifTime_ >= kGifMaxSeconds)
        finishGif();
}

void Editor::finishGif() {
    gifRecording_ = false;
    if (gifFrames_.size() < 2) {
        gifFrames_.clear();
        return;
    }
    std::error_code ec;
    stdfs::create_directories(projectDir_ / "captures", ec);
    std::string rel = "captures/recording_" + stamp() + ".gif";
    stdfs::path path = projectDir_ / rel;
    // Encoding takes a moment; do it in the background.
    auto frames = std::make_shared<std::vector<GifFrame>>(std::move(gifFrames_));
    gifFrames_.clear();
    if (gifThread_.joinable())
        gifThread_.join();
    gifEncoding_ = true;
    gifThread_ = std::thread([this, frames, path, rel] {
        GifWriter writer{};
        int w = frames->front().w, h = frames->front().h;
        if (GifBegin(&writer, path.string().c_str(), static_cast<uint32_t>(w), static_cast<uint32_t>(h), 100 / kGifFps)) {
            for (auto& f : *frames)
                GifWriteFrame(&writer, f.rgba.data(), static_cast<uint32_t>(w), static_cast<uint32_t>(h), 100 / kGifFps, 8, true);
            GifEnd(&writer);
            gifDone_ = rel;
        }
        gifEncoding_ = false;
    });
    notify("Saving the GIF...");
}

void Editor::drawCaptureStatus() {
    if (!gifEncoding_ && gifThread_.joinable()) {
        gifThread_.join(); // gifDone_ is safe to read once the encoder has finished
        if (!gifDone_.empty())
            notify("Saved " + gifDone_ + ". Drag it into a chat or post it!");
        gifDone_.clear();
        scanAssets();
    }
    if (!gifRecording_ && !gifEncoding_)
        return;
    ImGui::SameLine(0, 18);
    if (gifRecording_) {
        float blink = std::fmod(static_cast<float>(ImGui::GetTime()), 1.0f) < 0.6f ? 1.0f : 0.4f;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f * blink, 0.15f, 0.2f, 1));
        char label[64];
        std::snprintf(label, sizeof label, "REC %.1fs  (stop)", gifTime_);
        if (ImGui::SmallButton(label))
            finishGif();
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("Saving GIF...");
    }
}

} // namespace aven::editor
