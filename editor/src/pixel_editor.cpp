// Pixel Editor: draw sprites and animation frames right inside Aven. Pencil, eraser, fill,
// lines, rectangles, an eyedropper, mirror drawing, onion skin for animations, and one click
// to put the drawing on the selected object.

#include "editor.h"

#include "aven/core/fs.h"

#include <imgui.h>
#include <imgui_stdlib.h>
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aven::editor {

namespace {

// A friendly 16-color palette (like classic fantasy consoles), plus transparent.
const uint32_t kPalette[] = {0x000000, 0x1D2B53, 0x7E2553, 0x008751, 0xAB5236, 0x5F574F, 0xC2C3C7, 0xFFF1E8,
                             0xFF004D, 0xFFA300, 0xFFEC27, 0x00E436, 0x29ADFF, 0x83769C, 0xFF77A8, 0xFFCCAA};

uint32_t pack(const float c[4]) {
    auto b = [](float v) { return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return b(c[0]) | (b(c[1]) << 8) | (b(c[2]) << 16) | (b(c[3]) << 24);
}

void unpack(uint32_t v, float c[4]) {
    for (int i = 0; i < 4; ++i)
        c[i] = static_cast<float>((v >> (8 * i)) & 0xFF) / 255.0f;
}

uint32_t fromHex(uint32_t rgb) { return ((rgb >> 16) & 0xFF) | (((rgb >> 8) & 0xFF) << 8) | ((rgb & 0xFF) << 16) | 0xFF000000u; }

} // namespace

// ---------------------------------------------------------------- document

void Editor::PixelDoc::resize(int frameW, int frameH, int frameCount) {
    fw = frameW;
    fh = frameH;
    frames = frameCount;
    px.assign(static_cast<size_t>(fw) * frames * fh, 0);
}

uint32_t& Editor::PixelDoc::at(int frame, int x, int y) { return px[static_cast<size_t>(y) * fw * frames + frame * fw + x]; }

void Editor::openPixelEditor(const std::string& imagePath) {
    showPixelEditor_ = focusPixelEditor_ = true;
    PixelDoc& d = pixel_;
    d.undo.clear();
    d.redo.clear();
    d.frame = 0;
    d.dirty = false;
    if (imagePath.empty()) {
        if (d.px.empty()) {
            d.resize(16, 16, 1);
            d.path.clear();
            d.name = "sprite";
        }
    } else {
        int w, h, n;
        auto bytes = fs::readBinary(projectDir_ / imagePath);
        unsigned char* data = bytes ? stbi_load_from_memory(bytes->data(), static_cast<int>(bytes->size()), &w, &h, &n, 4) : nullptr;
        if (!data) {
            notify("Couldn't open " + imagePath, true);
            return;
        }
        // A sprite sheet laid out in a row opens as animation frames (if the selected sprite says so).
        int frames = 1;
        if (Entity e = selected())
            if (auto* sr = scene().registry().tryGet<SpriteRenderer>(e); sr && sr->texture == imagePath && sr->rows == 1 && sr->columns > 1 &&
                                                                          w % sr->columns == 0)
                frames = sr->columns;
        d.resize(w / frames, h, frames);
        std::memcpy(d.px.data(), data, d.px.size() * 4);
        stbi_image_free(data);
        d.path = imagePath;
        d.name = stdfs::path(imagePath).stem().string();
    }
    d.textureDirty = true;
}

void Editor::pixelSnapshot() {
    pixel_.undo.push_back(pixel_.px);
    if (pixel_.undo.size() > 100)
        pixel_.undo.erase(pixel_.undo.begin());
    pixel_.redo.clear();
}

bool Editor::savePixelImage() {
    PixelDoc& d = pixel_;
    if (d.path.empty()) {
        std::string base;
        for (char c : d.name)
            base += std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' ? c : '_';
        d.path = uniqueName("images", base.empty() ? "sprite" : base, ".png");
    }
    std::error_code ec;
    stdfs::create_directories((projectDir_ / d.path).parent_path(), ec);
    if (!Assets::savePng(projectDir_ / d.path, reinterpret_cast<const uint8_t*>(d.px.data()), d.fw * d.frames, d.fh, false)) {
        notify("Couldn't save " + d.path, true);
        return false;
    }
    d.dirty = false;
    assets_.reloadChanged();
    scanAssets();
    notify("Saved " + d.path);
    milestone("images_drawn");
    return true;
}

// ---------------------------------------------------------------- drawing operations

void Editor::pixelPlot(int x, int y, uint32_t color) {
    PixelDoc& d = pixel_;
    auto put = [&](int px, int py) {
        if (px >= 0 && py >= 0 && px < d.fw && py < d.fh)
            d.at(d.frame, px, py) = color;
    };
    for (int dy = 0; dy < d.brush; ++dy)
        for (int dx = 0; dx < d.brush; ++dx) {
            put(x + dx, y + dy);
            if (d.mirror)
                put(d.fw - 1 - (x + dx), y + dy);
        }
}

void Editor::pixelLine(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    while (true) {
        pixelPlot(x0, y0, color);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void Editor::pixelFill(int x, int y, uint32_t color) {
    PixelDoc& d = pixel_;
    if (x < 0 || y < 0 || x >= d.fw || y >= d.fh)
        return;
    uint32_t target = d.at(d.frame, x, y);
    if (target == color)
        return;
    std::vector<std::pair<int, int>> stack{{x, y}};
    while (!stack.empty()) {
        auto [cx, cy] = stack.back();
        stack.pop_back();
        if (cx < 0 || cy < 0 || cx >= d.fw || cy >= d.fh || d.at(d.frame, cx, cy) != target)
            continue;
        d.at(d.frame, cx, cy) = color;
        stack.push_back({cx + 1, cy});
        stack.push_back({cx - 1, cy});
        stack.push_back({cx, cy + 1});
        stack.push_back({cx, cy - 1});
    }
}

// ---------------------------------------------------------------- window

void Editor::drawPixelEditor() {
    ui::placeWindow({900, 640}, {0.5f, 0.5f});
    if (focusPixelEditor_) {
        ImGui::SetNextWindowFocus();
        focusPixelEditor_ = false;
    }
    PixelDoc& d = pixel_;
    std::string title = "Pixel Editor - " + (d.path.empty() ? d.name + " (new)" : d.path) + (d.dirty ? " *" : "") + "###PixelEditor";
    if (!ImGui::Begin(title.c_str(), &showPixelEditor_)) {
        pixelEditorFocused_ = false;
        ImGui::End();
        return;
    }
    pixelEditorFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (d.px.empty())
        d.resize(16, 16, 1);
    d.frame = std::clamp(d.frame, 0, d.frames - 1);

    // Keyboard.
    ImGuiIO& io = ImGui::GetIO();
    if (pixelEditorFocused_ && !io.WantTextInput) {
        struct K {
            ImGuiKey key;
            PixelTool tool;
        };
        for (K k : {K{ImGuiKey_B, PixelTool::Pencil}, K{ImGuiKey_E, PixelTool::Eraser}, K{ImGuiKey_G, PixelTool::Fill},
                    K{ImGuiKey_L, PixelTool::Line}, K{ImGuiKey_R, PixelTool::Rect}, K{ImGuiKey_I, PixelTool::Picker}})
            if (ImGui::IsKeyPressed(k.key, false) && !io.KeyCtrl)
                d.tool = k.tool;
        if (ImGui::IsKeyPressed(ImGuiKey_M, false) && !io.KeyCtrl)
            d.mirror = !d.mirror;
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z) && !d.undo.empty()) {
            d.redo.push_back(d.px);
            d.px = d.undo.back();
            d.undo.pop_back();
            d.textureDirty = d.dirty = true;
        }
        if ((ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) &&
            !d.redo.empty()) {
            d.undo.push_back(d.px);
            d.px = d.redo.back();
            d.redo.pop_back();
            d.textureDirty = d.dirty = true;
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
            savePixelImage();
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) && d.frames > 1)
            d.frame = (d.frame + d.frames - 1) % d.frames;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) && d.frames > 1)
            d.frame = (d.frame + 1) % d.frames;
    }

    // Left column: tools, colors, file.
    ImGui::BeginChild("##pixtools", {ImGui::GetFontSize() * 14, 0}, ImGuiChildFlags_Border);
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    struct T {
        PixelTool tool;
        const char* name;
        const char* key;
    };
    for (T t : {T{PixelTool::Pencil, "Pencil", "B"}, T{PixelTool::Eraser, "Eraser", "E"}, T{PixelTool::Fill, "Fill", "G"},
                T{PixelTool::Line, "Line", "L"}, T{PixelTool::Rect, "Box", "R"}, T{PixelTool::Picker, "Pick color", "I"}}) {
        bool sel = d.tool == t.tool;
        if (sel)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab));
        if (ImGui::Button(t.name, {half, 0}))
            d.tool = t.tool;
        if (sel)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s (%s)", t.name, t.key);
        if (t.tool == PixelTool::Eraser || t.tool == PixelTool::Line || t.tool == PixelTool::Picker)
            continue;
        ImGui::SameLine();
    }
    ImGui::Checkbox("Mirror (M)", &d.mirror);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Draw both sides at once, for symmetric characters");
    ImGui::Checkbox("Filled box", &d.filledRect);
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##brush", &d.brush, 1, 4, "Brush %d px");
    ui::sectionHeader("Colors");
    ImGui::ColorEdit4("##primary", d.color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf);
    ImGui::SameLine();
    ImGui::TextDisabled("left click");
    for (size_t i = 0; i < std::size(kPalette); ++i) {
        if (i % 8)
            ImGui::SameLine(0, 2);
        ImGui::PushID(static_cast<int>(i));
        float c[4];
        unpack(fromHex(kPalette[i]), c);
        if (ImGui::ColorButton("##pal", {c[0], c[1], c[2], 1}, ImGuiColorEditFlags_NoTooltip, {20, 20}))
            std::copy(c, c + 4, d.color);
        ImGui::PopID();
    }
    ImGui::TextDisabled("Right click erases.");
    ui::sectionHeader("View");
    ImGui::Checkbox("Grid", &d.grid);
    if (d.frames > 1) {
        ImGui::SameLine();
        ImGui::Checkbox("Onion skin", &d.onion);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Show the frame before this one faintly, to line up animations");
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##zoom", &d.zoom, 2, 48, "Zoom %.0fx");

    ui::sectionHeader("Image");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##pixname", &d.name);
    if (ImGui::Button("Save (Ctrl+S)", {-1, 0}))
        savePixelImage();
    if (ImGui::BeginMenu("New image...")) {
        for (int size : {8, 16, 24, 32, 48, 64, 128}) {
            std::string label = std::to_string(size) + " x " + std::to_string(size);
            if (ImGui::MenuItem(label.c_str())) {
                d.resize(size, size, 1);
                d.path.clear();
                d.name = "sprite";
                d.undo.clear();
                d.redo.clear();
                d.textureDirty = true;
                d.zoom = std::clamp(384.0f / size, 2.0f, 48.0f);
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Open...")) {
        auto images = projectFiles({".png"});
        if (images.empty())
            ImGui::TextDisabled("No images yet.");
        for (auto& img : images)
            if (ImGui::MenuItem(img.c_str()))
                openPixelEditor(img);
        ImGui::EndMenu();
    }
    Entity sel = selected();
    if (sel && !playing_ && ImGui::Button("Use on selected object", {-1, 0})) {
        if (d.dirty || d.path.empty())
            savePixelImage();
        if (!d.path.empty()) {
            recordUndo("Use drawing");
            auto& reg = scene().registry();
            auto& sr = reg.getOrEmplace<SpriteRenderer>(sel);
            sr.texture = d.path;
            sr.columns = d.frames;
            sr.rows = 1;
            sr.frame = 0;
            sr.pixelArt = true;
            if (d.frames > 1) {
                auto& anim = reg.getOrEmplace<SpriteAnimator>(sel);
                anim.firstFrame = 0;
                anim.lastFrame = d.frames - 1;
            }
            notify(scene().info(sel).name + " now uses " + d.path);
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // Right: frames and canvas.
    ImGui::BeginGroup();
    // Frame strip.
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Frame %d of %d", d.frame + 1, d.frames);
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Frame")) {
        pixelSnapshot();
        // Frames sit side by side; the new one starts as a copy of the current one.
        std::vector<uint32_t> old = d.px;
        int oldFrames = d.frames, cur = d.frame;
        d.frames += 1;
        d.px.assign(static_cast<size_t>(d.fw) * d.frames * d.fh, 0);
        for (int y = 0; y < d.fh; ++y)
            for (int f = 0; f < oldFrames; ++f)
                for (int x = 0; x < d.fw; ++x) {
                    int to = f <= cur ? f : f + 1;
                    d.at(to, x, y) = old[static_cast<size_t>(y) * d.fw * oldFrames + f * d.fw + x];
                    if (f == cur)
                        d.at(cur + 1, x, y) = old[static_cast<size_t>(y) * d.fw * oldFrames + f * d.fw + x];
                }
        d.frame = cur + 1;
        d.textureDirty = d.dirty = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Add an animation frame (a copy of this one)");
    if (d.frames > 1) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete frame")) {
            pixelSnapshot();
            std::vector<uint32_t> old = d.px;
            int oldFrames = d.frames, cur = d.frame;
            d.frames -= 1;
            d.px.assign(static_cast<size_t>(d.fw) * d.frames * d.fh, 0);
            for (int y = 0; y < d.fh; ++y)
                for (int f = 0, to = 0; f < oldFrames; ++f) {
                    if (f == cur)
                        continue;
                    for (int x = 0; x < d.fw; ++x)
                        d.at(to, x, y) = old[static_cast<size_t>(y) * d.fw * oldFrames + f * d.fw + x];
                    ++to;
                }
            d.frame = std::min(cur, d.frames - 1);
            d.textureDirty = d.dirty = true;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Play", &d.playing);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        ImGui::SliderFloat("##pfps", &d.fps, 1, 24, "%.0f fps");
        if (d.playing) {
            d.playTime += io.DeltaTime * d.fps;
            d.frame = static_cast<int>(d.playTime) % d.frames;
        }
    }

    // Upload the image when it changed.
    if (d.textureDirty || !d.texture) {
        int w = d.fw * d.frames, h = d.fh;
        if (d.texture && (d.texW != w || d.texH != h)) {
            device_.destroy(d.texture);
            d.texture = {};
        }
        if (!d.texture) {
            rhi::TextureDesc desc;
            desc.width = w;
            desc.height = h;
            desc.filter = rhi::Filter::Nearest;
            desc.data = d.px.data();
            desc.label = "pixel editor";
            d.texture = device_.createTexture(desc);
            d.texW = w;
            d.texH = h;
        } else {
            device_.updateTexture(d.texture, d.px.data());
        }
        d.textureDirty = false;
    }

    // Canvas.
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##canvas", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                                  ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    ImVec2 c0 = ImGui::GetItemRectMin();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(c0, {c0.x + avail.x, c0.y + avail.y}, true);
    dl->AddRectFilled(c0, {c0.x + avail.x, c0.y + avail.y}, IM_COL32(40, 42, 48, 255));
    if (hovered && io.MouseWheel != 0)
        d.zoom = std::clamp(d.zoom * (io.MouseWheel > 0 ? 1.2f : 1 / 1.2f), 2.0f, 48.0f);
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        d.pan.x += io.MouseDelta.x;
        d.pan.y += io.MouseDelta.y;
    }
    float z = std::round(d.zoom);
    ImVec2 size{d.fw * z, d.fh * z};
    ImVec2 o{std::round(c0.x + (avail.x - size.x) * 0.5f + d.pan.x), std::round(c0.y + (avail.y - size.y) * 0.5f + d.pan.y)};
    // Checkerboard behind transparent pixels.
    float check = std::max(z, 8.0f);
    for (float y = 0; y < size.y; y += check)
        for (float x = 0; x < size.x; x += check) {
            bool dark = (static_cast<int>(x / check) + static_cast<int>(y / check)) % 2;
            dl->AddRectFilled({o.x + x, o.y + y}, {o.x + std::min(size.x, x + check), o.y + std::min(size.y, y + check)},
                              dark ? IM_COL32(150, 150, 155, 255) : IM_COL32(200, 200, 205, 255));
        }
    ImTextureID tex = static_cast<ImTextureID>(device_.nativeTexture(d.texture));
    float u0 = static_cast<float>(d.frame) / d.frames, u1 = static_cast<float>(d.frame + 1) / d.frames;
    if (d.onion && d.frames > 1 && !d.playing) {
        int prev = (d.frame + d.frames - 1) % d.frames;
        dl->AddImage(tex, o, {o.x + size.x, o.y + size.y}, {static_cast<float>(prev) / d.frames, 0},
                     {static_cast<float>(prev + 1) / d.frames, 1}, IM_COL32(255, 255, 255, 70));
    }
    dl->AddImage(tex, o, {o.x + size.x, o.y + size.y}, {u0, 0}, {u1, 1});
    if (d.grid && z >= 6) {
        for (int x = 0; x <= d.fw; ++x)
            dl->AddLine({o.x + x * z, o.y}, {o.x + x * z, o.y + size.y}, IM_COL32(0, 0, 0, 40));
        for (int y = 0; y <= d.fh; ++y)
            dl->AddLine({o.x, o.y + y * z}, {o.x + size.x, o.y + y * z}, IM_COL32(0, 0, 0, 40));
    }
    if (d.mirror)
        dl->AddLine({o.x + size.x * 0.5f, o.y - 6}, {o.x + size.x * 0.5f, o.y + size.y + 6}, IM_COL32(255, 80, 180, 160), 2);
    dl->AddRect({o.x - 1, o.y - 1}, {o.x + size.x + 1, o.y + size.y + 1}, IM_COL32(0, 0, 0, 160));

    int mx = static_cast<int>(std::floor((io.MousePos.x - o.x) / z)), my = static_cast<int>(std::floor((io.MousePos.y - o.y) / z));
    bool inside = mx >= 0 && my >= 0 && mx < d.fw && my < d.fh;
    if (hovered && inside) {
        dl->AddRect({o.x + mx * z, o.y + my * z}, {o.x + (mx + d.brush) * z, o.y + (my + d.brush) * z}, IM_COL32(255, 255, 255, 200));
        ImGui::SetTooltip("%d, %d", mx, my);
    }

    // Painting.
    bool left = ImGui::IsMouseDown(ImGuiMouseButton_Left), right = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    bool started = hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right));
    uint32_t paint = right || d.tool == PixelTool::Eraser ? 0u : pack(d.color);
    if (started && !d.playing) {
        pixelSnapshot();
        d.stroke = true;
        d.start = {mx, my};
        d.last = {mx, my};
        d.before = d.px;
        if (d.tool == PixelTool::Fill) {
            pixelFill(mx, my, paint);
            d.stroke = false;
        } else if (d.tool == PixelTool::Picker && inside) {
            unpack(d.at(d.frame, mx, my), d.color);
            d.tool = PixelTool::Pencil;
            d.stroke = false;
        } else if (d.tool == PixelTool::Pencil || d.tool == PixelTool::Eraser) {
            pixelPlot(mx, my, paint);
        }
        d.textureDirty = d.dirty = true;
    }
    if (d.stroke && (left || right)) {
        if (d.tool == PixelTool::Pencil || d.tool == PixelTool::Eraser) {
            if (mx != d.last.first || my != d.last.second)
                pixelLine(d.last.first, d.last.second, mx, my, paint);
        } else if (d.tool == PixelTool::Line || d.tool == PixelTool::Rect) {
            // Preview the shape: start from the image before the drag each frame.
            d.px = d.before;
            if (d.tool == PixelTool::Line) {
                pixelLine(d.start.first, d.start.second, mx, my, paint);
            } else {
                int x0 = std::min(d.start.first, mx), x1 = std::max(d.start.first, mx);
                int y0 = std::min(d.start.second, my), y1 = std::max(d.start.second, my);
                for (int y = y0; y <= y1; ++y)
                    for (int x = x0; x <= x1; ++x)
                        if (d.filledRect || x == x0 || x == x1 || y == y0 || y == y1)
                            pixelPlot(x, y, paint);
            }
        }
        d.last = {mx, my};
        d.textureDirty = true;
    }
    if (d.stroke && !left && !right)
        d.stroke = false;
    dl->PopClipRect();
    ImGui::EndGroup();
    ImGui::End();
}

} // namespace aven::editor
