// Sprite Sheet: split a picture into frames, pick which one a sprite shows, and turn a run
// of frames into an animation with a live preview.

#include "editor.h"

#include <imgui.h>

#include <algorithm>

namespace aven::editor {

void Editor::openSpriteSheet() {
    showSpriteSheet_ = focusSpriteSheet_ = true;
    sheetRangeStart_ = -1;
}

void Editor::drawSpriteSheet() {
    ui::placeWindow({700, 560}, {0.5f, 0.5f});
    if (focusSpriteSheet_) {
        ImGui::SetNextWindowFocus();
        focusSpriteSheet_ = false;
    }
    if (!ImGui::Begin("Sprite Sheet and Animation###SpriteSheet", &showSpriteSheet_)) {
        ImGui::End();
        return;
    }
    Entity e = selected();
    auto& reg = scene().registry();
    auto* sr = e ? reg.tryGet<SpriteRenderer>(e) : nullptr;
    if (!sr || sr->texture.empty()) {
        ImGui::TextWrapped("Select an object with a picture (a SpriteRenderer with an Image) to split it into frames and "
                           "animate it. Draw one in the Pixel Editor, or drag an image from Assets onto the scene.");
        ImGui::End();
        return;
    }
    const TextureAsset& tex = assets_.texture(sr->texture);
    if (tex.missing || tex.width <= 0) {
        ImGui::TextColored({1, 0.5f, 0.5f, 1}, "Can't load %s.", sr->texture.c_str());
        ImGui::End();
        return;
    }
    auto* anim = reg.tryGet<SpriteAnimator>(e);
    auto change = [&](const char* what) {
        if (!playing_)
            edited(what);
    };

    ImGui::Text("%s  (%d x %d pixels)", sr->texture.c_str(), tex.width, tex.height);
    ImGui::SetNextItemWidth(110);
    int cols = sr->columns, rows = sr->rows;
    if (ImGui::InputInt("Columns", &cols) && cols >= 1) {
        change("Sprite sheet");
        sr->columns = std::min(cols, tex.width);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    if (ImGui::InputInt("Rows", &rows) && rows >= 1) {
        change("Sprite sheet");
        sr->rows = std::min(rows, tex.height);
    }
    ImGui::SameLine();
    if (ImGui::Button("Guess") && tex.height > 0) {
        change("Sprite sheet");
        // Square frames in a row or a column are the most common layout.
        if (tex.width >= tex.height && tex.width % tex.height == 0) {
            sr->columns = tex.width / tex.height;
            sr->rows = 1;
        } else if (tex.height % tex.width == 0) {
            sr->rows = tex.height / tex.width;
            sr->columns = 1;
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Guess the frames from the picture's shape (square frames)");
    if (unlocked(Feature::PixelEditor)) {
        ImGui::SameLine();
        if (ImGui::Button("Edit in Pixel Editor"))
            openPixelEditor(sr->texture);
    }
    int frames = std::max(1, sr->columns * sr->rows);
    ImGui::TextDisabled("Click a frame to show it. Shift-click another to pick a run of frames for an animation.");

    // The sheet with its grid.
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float previewW = 180;
    float scale = std::min((avail.x - previewW - 16) / tex.width, (avail.y - 10) / tex.height);
    scale = std::max(scale, 0.5f);
    if (scale >= 1)
        scale = std::floor(scale);
    ImVec2 size{tex.width * scale, tex.height * scale};
    // Zoomed in, show crisp pixels. Images are stored bottom row first, so V is flipped.
    auto image = static_cast<ImTextureID>(device_.nativeTexture(assets_.texture(sr->texture, sr->pixelArt || scale >= 1).handle));
    ImVec2 o = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##sheet", size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(o, {o.x + size.x, o.y + size.y}, IM_COL32(60, 62, 70, 255));
    dl->AddImage(image, o, {o.x + size.x, o.y + size.y}, {0, 1}, {1, 0});
    float cw = size.x / sr->columns, ch = size.y / sr->rows;
    int first = anim ? anim->firstFrame : sr->frame, last = anim ? anim->lastFrame : sr->frame;
    if (sheetRangeStart_ >= 0) {
        first = std::min(sheetRangeStart_, sr->frame);
        last = std::max(sheetRangeStart_, sr->frame);
    }
    for (int f = 0; f < frames; ++f) {
        int cx = f % sr->columns, cy = f / sr->columns;
        ImVec2 a{o.x + cx * cw, o.y + cy * ch}, b{a.x + cw, a.y + ch};
        if (f >= first && f <= last)
            dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_SliderGrab, 0.25f));
        dl->AddRect(a, b, f == sr->frame ? ImGui::GetColorU32(ImGuiCol_SliderGrab) : IM_COL32(255, 255, 255, 60), 0, 0,
                    f == sr->frame ? 3.0f : 1.0f);
        char n[16];
        std::snprintf(n, sizeof n, "%d", f);
        dl->AddText({a.x + 3, a.y + 2}, IM_COL32(255, 255, 255, 170), n);
    }
    if (ImGui::IsItemClicked()) {
        ImVec2 m = ImGui::GetIO().MousePos;
        int cx = std::clamp(static_cast<int>((m.x - o.x) / cw), 0, sr->columns - 1);
        int cy = std::clamp(static_cast<int>((m.y - o.y) / ch), 0, sr->rows - 1);
        int f = cy * sr->columns + cx;
        change("Pick frame");
        if (ImGui::GetIO().KeyShift) {
            if (sheetRangeStart_ < 0)
                sheetRangeStart_ = sr->frame;
        } else {
            sheetRangeStart_ = -1;
        }
        sr->frame = f;
    }

    // Animation settings and a live preview.
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushFont(fonts.bold);
    ImGui::TextUnformatted("Animation");
    ImGui::PopFont();
    ImGui::TextDisabled("Frames %d to %d", first, last);
    static float fps = 10;
    static bool loop = true;
    if (anim && sheetRangeStart_ < 0) {
        fps = anim->fps;
        loop = anim->loop;
    }
    ImGui::SetNextItemWidth(previewW - 10);
    bool fpsChanged = ImGui::SliderFloat("##fps", &fps, 1, 30, "%.0f frames/second");
    bool loopChanged = ImGui::Checkbox("Loop", &loop);
    if (anim && (fpsChanged || loopChanged)) {
        change("Animation");
        anim->fps = fps;
        anim->loop = loop;
    }
    if (ImGui::Button(anim ? "Use this run of frames" : "Animate these frames", {previewW - 10, 0})) {
        if (!playing_)
            recordUndo("Animate sprite");
        auto& a = reg.getOrEmplace<SpriteAnimator>(e);
        a.firstFrame = first;
        a.lastFrame = last;
        a.fps = fps;
        a.loop = loop;
        a.playing = true;
        sheetRangeStart_ = -1;
        notify("It plays when the game runs. In scripts: self.play_animation(" + std::to_string(first) + ", " + std::to_string(last) + ")");
    }
    // Preview.
    sheetPreviewTime_ += ImGui::GetIO().DeltaTime * fps;
    int count = std::max(1, last - first + 1);
    int shown = first + static_cast<int>(sheetPreviewTime_) % count;
    int cx = shown % sr->columns, cy = shown / sr->columns;
    float fwPx = static_cast<float>(tex.width) / sr->columns, fhPx = static_cast<float>(tex.height) / sr->rows;
    float pv = previewW - 10;
    float pvH = pv * fhPx / std::max(fwPx, 1.0f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy({pv, pvH});
    dl->AddRectFilled(p, {p.x + pv, p.y + pvH}, IM_COL32(40, 42, 48, 255), 6);
    dl->AddImage(image, p, {p.x + pv, p.y + pvH}, {cx * fwPx / tex.width, 1 - cy * fhPx / tex.height},
                 {(cx + 1) * fwPx / tex.width, 1 - (cy + 1) * fhPx / tex.height});
    ImGui::TextDisabled("Preview");
    if (anim && ImGui::SmallButton("Stop animating")) {
        change("Animation");
        reg.remove<SpriteAnimator>(e);
    }
    ImGui::EndGroup();
    ImGui::End();
}

} // namespace aven::editor
