// Tile Painter: pick a tile from the tileset and paint levels straight into the Scene view.
// Paint, erase, draw boxes, flood fill and pick tiles; everything can be undone.

#include "editor.h"

#include "aven/core/fs.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace aven::editor {

namespace {

// Calls fn(x, y) for each cell on the line between two cells, so fast drags leave no gaps.
template <class Fn>
void cellLine(int x0, int y0, int x1, int y1, Fn fn) {
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (int guard = 0; guard < 10000; ++guard) {
        fn(x0, y0);
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

// Where tile `tile` sits in the tileset image, as UVs for ImGui (images are stored bottom row first).
void tileUV(const Tilemap& map, int tile, ImVec2& uv0, ImVec2& uv1) {
    int cols = std::max(1, map.columns), rows = std::max(1, map.rows);
    int t = std::max(tile, 0) % (cols * rows);
    int c = t % cols, r = t / cols;
    uv0 = {static_cast<float>(c) / cols, 1.0f - static_cast<float>(r) / rows};
    uv1 = {static_cast<float>(c + 1) / cols, 1.0f - static_cast<float>(r + 1) / rows};
}

ImU32 toU32(Color c, float alpha) { return ImGui::ColorConvertFloat4ToU32({c.r, c.g, c.b, c.a * alpha}); }

// A small "~" in the corner of tiles that things pass through.
void passBadge(ImDrawList* dl, ImVec2 corner) {
    ImVec2 a{corner.x - 17, corner.y - 15};
    dl->AddRectFilled(a, {corner.x - 2, corner.y - 2}, IM_COL32(20, 22, 28, 200), 3);
    dl->AddText({a.x + 3, a.y - 2}, IM_COL32(140, 200, 255, 255), "~");
}

ImU32 blockColor(int tile, float alpha = 1.0f) {
    Color c = tileColor(tile);
    return ImGui::ColorConvertFloat4ToU32({c.r, c.g, c.b, alpha});
}

} // namespace

void Editor::openTilePainter() {
    showTilePainter_ = focusTilePainter_ = true;
    tileStroke_ = tileBoxing_ = false;
}

Tilemap* Editor::paintingTilemap() {
    if (!showTilePainter_ || playing_ || !unlocked(Feature::Tilemap))
        return nullptr;
    Entity e = selected();
    return e ? scene().registry().tryGet<Tilemap>(e) : nullptr;
}

void Editor::useStarterTileset(Entity e) {
    auto* map = scene().registry().tryGet<Tilemap>(e);
    if (!map)
        return;
    const std::string rel = "images/tiles.png";
    if (!stdfs::exists(projectDir_ / rel)) {
        std::error_code ec;
        stdfs::create_directories(projectDir_ / "images", ec);
        stdfs::copy_file(editorDataDir() / "starter_tiles.png", projectDir_ / rel, ec);
        if (ec) {
            notify("Couldn't copy the starter tiles: " + ec.message(), true);
            return;
        }
        scanAssets();
    }
    recordUndo("Starter tiles");
    map->tileset = rel;
    map->columns = map->rows = 4;
    map->pixelArt = true;
    map->notSolid = "6, 8, 15"; // water, leaves and ladders
    notify("Added images/tiles.png: grass, dirt, stone, bricks, water, spikes and more");
}

void Editor::drawTilePainter() {
    ui::placeWindow({330, 620}, {0.18f, 0.45f});
    if (focusTilePainter_) {
        ImGui::SetNextWindowFocus();
        focusTilePainter_ = false;
    }
    if (!ImGui::Begin("Tile Painter###TilePainter", &showTilePainter_)) {
        ImGui::End();
        return;
    }
    auto& reg = scene().registry();
    Entity e = selected();
    Tilemap* map = e ? reg.tryGet<Tilemap>(e) : nullptr;
    if (!map) {
        ImGui::TextWrapped("Select a Tilemap to paint on, or make a new one.");
        ImGui::BeginDisabled(playing_);
        if (ImGui::Button("Create a Tilemap", {-1, 34}))
            createEntity("Tilemap");
        ImGui::EndDisabled();
        // Offer the tilemaps already in the scene.
        int n = 0;
        scene().walk([&](Entity t, int) {
            if (reg.has<Tilemap>(t)) {
                ImGui::PushID(n++);
                if (ImGui::Selectable(scene().info(t).name.c_str()))
                    select(t);
                ImGui::PopID();
            }
            return true;
        });
        ImGui::End();
        return;
    }
    if (playing_)
        ImGui::TextColored({1, 0.8f, 0.3f, 1}, "Stop the game to paint tiles.");

    // Tools.
    struct ToolInfo {
        TileTool tool;
        const char* name;
        const char* tip;
    };
    static const ToolInfo kTools[] = {
        {TileTool::Paint, "Paint", "Click and drag to paint. Hold Shift to erase."},
        {TileTool::Erase, "Erase", "Click and drag to remove tiles."},
        {TileTool::Box, "Box", "Drag a rectangle to fill it. Hold Shift to erase a rectangle."},
        {TileTool::Fill, "Fill", "Fill a connected area of the same tile (or empty space inside your level)."},
        {TileTool::Pick, "Pick", "Click a tile in the scene to paint with it."},
    };
    float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 4) / 5;
    for (size_t i = 0; i < std::size(kTools); ++i) {
        if (i)
            ImGui::SameLine();
        bool sel = tileTool_ == kTools[i].tool;
        if (sel)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab));
        if (ImGui::Button(kTools[i].name, {w, 0}))
            tileTool_ = kTools[i].tool;
        if (sel)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", kTools[i].tip);
    }

    // Tileset.
    ImGui::SeparatorText("Tiles");
    const bool colored = map->tileset.empty();
    int count = colored ? 16 : std::max(1, map->columns) * std::max(1, map->rows);
    tileBrush_ = std::clamp(tileBrush_, 0, count - 1);
    if (colored) {
        float cell = std::floor((ImGui::GetContentRegionAvail().x - 3 * 4) / 4);
        for (int t = 0; t < 16; ++t) {
            if (t % 4)
                ImGui::SameLine(0, 4);
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::PushID(t);
            if (ImGui::InvisibleButton("##tile", {cell, cell * 0.6f}))
                tileBrush_ = t;
            ImGui::PopID();
            auto* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, {p.x + cell, p.y + cell * 0.6f}, blockColor(t), 4);
            if (!map->isSolidTile(t))
                passBadge(dl, {p.x + cell, p.y + cell * 0.6f});
            if (t == tileBrush_)
                dl->AddRect({p.x - 2, p.y - 2}, {p.x + cell + 2, p.y + cell * 0.6f + 2}, ImGui::GetColorU32(ImGuiCol_Text), 5, 0, 2.5f);
        }
        ImGui::TextDisabled("Colored blocks. Use a tileset image for real tiles:");
    } else {
        const TextureAsset& tex = assets_.texture(map->tileset, true);
        if (tex.missing) {
            ImGui::TextColored({1, 0.5f, 0.5f, 1}, "Can't load %s", map->tileset.c_str());
        } else {
            float availW = ImGui::GetContentRegionAvail().x;
            float scale = std::min(availW / tex.width, 360.0f / tex.height);
            if (scale >= 1)
                scale = std::floor(scale);
            ImVec2 size{tex.width * scale, tex.height * scale};
            ImVec2 o = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##tileset", size);
            auto* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(o, {o.x + size.x, o.y + size.y}, IM_COL32(50, 52, 60, 255));
            dl->AddImage(static_cast<ImTextureID>(device_.nativeTexture(tex.handle)), o, {o.x + size.x, o.y + size.y}, {0, 1}, {1, 0});
            int cols = std::max(1, map->columns), rows = std::max(1, map->rows);
            float cw = size.x / cols, ch = size.y / rows;
            for (int t = 0; t < count; ++t) {
                ImVec2 a{o.x + (t % cols) * cw, o.y + (t / cols) * ch};
                dl->AddRect(a, {a.x + cw, a.y + ch}, IM_COL32(255, 255, 255, 40));
                if (!map->isSolidTile(t) && map->collision != TileCollision::None && cw >= 24)
                    passBadge(dl, {a.x + cw, a.y + ch});
            }
            ImVec2 a{o.x + (tileBrush_ % cols) * cw, o.y + (tileBrush_ / cols) * ch};
            dl->AddRect(a, {a.x + cw, a.y + ch}, ImGui::GetColorU32(ImGuiCol_Text), 2, 0, 2.5f);
            if (ImGui::IsItemClicked()) {
                ImVec2 m = ImGui::GetIO().MousePos;
                int cx = std::clamp(static_cast<int>((m.x - o.x) / cw), 0, cols - 1);
                int cy = std::clamp(static_cast<int>((m.y - o.y) / ch), 0, rows - 1);
                tileBrush_ = cy * cols + cx;
                if (tileTool_ == TileTool::Erase || tileTool_ == TileTool::Pick)
                    tileTool_ = TileTool::Paint;
            }
            ImGui::TextDisabled("Tile %d of %s", tileBrush_, map->tileset.c_str());
        }
    }
    ImGui::BeginDisabled(playing_);
    if (map->collision != TileCollision::None) {
        bool solid = map->isSolidTile(tileBrush_);
        if (ImGui::Checkbox("This tile is solid", &solid)) {
            recordUndo("Tile solid");
            map->setSolidTile(tileBrush_, solid);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Untick for tiles things should pass through, like water, ladders and background decoration.\n"
                              "Tiles marked ~ in the palette are not solid.");
    }
    if (ImGui::BeginCombo("##tileset_pick", colored ? "Colored blocks" : map->tileset.c_str())) {
        if (ImGui::Selectable("Colored blocks", colored)) {
            recordUndo("Tileset");
            map->tileset.clear();
        }
        for (auto& f : projectFiles({".png"}))
            if (ImGui::Selectable(f.c_str(), f == map->tileset)) {
                recordUndo("Tileset");
                map->tileset = f;
                // Guess the grid: square tiles of a common size.
                const TextureAsset& t = assets_.texture(f, true);
                for (int px : {16, 32, 8, 64, 24, 48})
                    if (t.width >= px && t.height >= px && t.width % px == 0 && t.height % px == 0) {
                        map->columns = t.width / px;
                        map->rows = t.height / px;
                        break;
                    }
            }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Use the starter tiles"))
        useStarterTileset(e);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Copies a ready-made tileset into images/tiles.png: grass, dirt, stone, bricks, water, spikes...");
    if (unlocked(Feature::PixelEditor) && !colored) {
        ImGui::SameLine();
        if (ImGui::Button("Edit tiles"))
            openPixelEditor(map->tileset);
    }
    if (!colored) {
        ImGui::SetNextItemWidth(90);
        int cols = map->columns, rows = map->rows;
        if (ImGui::InputInt("Across", &cols) && cols >= 1) {
            edited("Tileset grid");
            map->columns = cols;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        if (ImGui::InputInt("Down", &rows) && rows >= 1) {
            edited("Tileset grid");
            map->rows = rows;
        }
    }

    // The map itself.
    ImGui::SeparatorText("Map");
    int collision = static_cast<int>(map->collision);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##collision", &collision, "Not solid (decoration)\0Solid (walls and floors)\0Trigger (spikes, water)\0")) {
        recordUndo("Tile collision");
        map->collision = static_cast<TileCollision>(collision);
    }
    ImGui::SetNextItemWidth(-1);
    float ts = map->tileSize;
    if (ImGui::SliderFloat("##tilesize", &ts, 0.25f, 4.0f, "Tile size %.2f")) {
        edited("Tile size");
        map->tileSize = std::max(ts, 0.05f);
    }
    ImGui::Text("%d tiles painted", static_cast<int>(map->tiles.size()));
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear all") && !map->tiles.empty()) {
        recordUndo("Clear tiles");
        map->tiles.clear();
        ++map->version;
    }
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", view3D_ ? "Switch the Scene view to 2D to paint."
                                     : "Paint in the Scene view. Right-drag pans, scroll zooms, [ and ] change tile. "
                                       "Close this window to select and move objects again.");
    ImGui::PopStyleColor();
    ImGui::End();
}

void Editor::paintTilesInViewport(const CameraView& cam, ImVec2 pos) {
    Tilemap* map = paintingTilemap();
    Entity e = selected();
    if (!map || !cam.orthographic)
        return;
    ImGuiIO& io = ImGui::GetIO();
    Mat4 world = scene().worldMatrix(e);
    const float ts = std::max(map->tileSize, 0.001f);
    Vec3 local = transformPoint(inverse(world), cam.screenToWorld({io.MousePos.x - pos.x, io.MousePos.y - pos.y}, viewportSize_));
    int cx = static_cast<int>(std::floor(local.x / ts)), cy = static_cast<int>(std::floor(local.y / ts));
    auto screen = [&](float x, float y) {
        Vec2 s = cam.worldToScreen(transformPoint(world, {x * ts, y * ts, 0}), viewportSize_);
        return ImVec2(pos.x + s.x, pos.y + s.y);
    };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool erasing = tileTool_ == TileTool::Erase || (io.KeyShift && (tileTool_ == TileTool::Paint || tileTool_ == TileTool::Box));
    const int brush = erasing ? -1 : tileBrush_;
    const TextureAsset* tex = map->tileset.empty() ? nullptr : &assets_.texture(map->tileset, true);
    Color acc = prefs.accentColor();

    // What would be painted: a ghost of the tile over the cell (or the box being dragged).
    auto ghost = [&](int x0, int y0, int x1, int y1) {
        ImVec2 a = screen(static_cast<float>(x0), static_cast<float>(y0)), b = screen(static_cast<float>(x1 + 1), static_cast<float>(y0)),
               c = screen(static_cast<float>(x1 + 1), static_cast<float>(y1 + 1)), d = screen(static_cast<float>(x0), static_cast<float>(y1 + 1));
        if (brush >= 0 && tileTool_ != TileTool::Pick) {
            if (x0 == x1 && y0 == y1 && tex && !tex->missing) {
                ImVec2 uv0, uv1;
                tileUV(*map, brush, uv0, uv1);
                dl->AddImageQuad(static_cast<ImTextureID>(device_.nativeTexture(tex->handle)), d, c, b, a, uv0, {uv1.x, uv0.y}, uv1,
                                 {uv0.x, uv1.y}, IM_COL32(255, 255, 255, 170));
            } else {
                dl->AddQuadFilled(a, b, c, d, tex ? toU32(acc, 0.25f) : blockColor(brush, 0.55f));
            }
        } else if (erasing) {
            dl->AddQuadFilled(a, b, c, d, IM_COL32(255, 70, 70, 60));
        }
        dl->AddQuad(a, b, c, d, erasing ? IM_COL32(255, 90, 90, 230) : toU32(acc, 0.95f), 2);
    };

    // Keys: [ and ] step through the tiles.
    int count = map->tileset.empty() ? 16 : std::max(1, map->columns) * std::max(1, map->rows);
    if (viewportHovered_ && !io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket))
            tileBrush_ = (tileBrush_ + count - 1) % count;
        if (ImGui::IsKeyPressed(ImGuiKey_RightBracket))
            tileBrush_ = (tileBrush_ + 1) % count;
    }

    if (tileBoxing_) {
        int x0 = std::min(tileBoxStart_.first, cx), x1 = std::max(tileBoxStart_.first, cx);
        int y0 = std::min(tileBoxStart_.second, cy), y1 = std::max(tileBoxStart_.second, cy);
        ghost(x0, y0, x1, y1);
        char size[32];
        std::snprintf(size, sizeof size, "%d x %d", x1 - x0 + 1, y1 - y0 + 1);
        dl->AddText({io.MousePos.x + 14, io.MousePos.y + 10}, IM_COL32(255, 255, 255, 230), size);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            tileBoxing_ = false;
            if (static_cast<int64_t>(x1 - x0 + 1) * (y1 - y0 + 1) > 250000) {
                notify("That box is too big to fill.", true);
            } else {
                recordUndo(erasing ? "Erase tiles" : "Paint tiles");
                for (int y = y0; y <= y1; ++y)
                    for (int x = x0; x <= x1; ++x)
                        map->set(x, y, brush);
            }
        }
        return;
    }
    if (!viewportHovered_) {
        tileStroke_ = false;
        return;
    }
    ghost(cx, cy, cx, cy);
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        switch (tileTool_) {
        case TileTool::Paint:
        case TileTool::Erase:
            recordUndo(brush < 0 ? "Erase tiles" : "Paint tiles");
            map->set(cx, cy, brush);
            tileStroke_ = true;
            tileLast_ = {cx, cy};
            break;
        case TileTool::Box:
            tileBoxing_ = true;
            tileBoxStart_ = {cx, cy};
            break;
        case TileTool::Pick:
            if (int t = map->get(cx, cy); t >= 0) {
                tileBrush_ = t;
                tileTool_ = TileTool::Paint;
            }
            break;
        case TileTool::Fill: {
            int target = map->get(cx, cy);
            if (target == tileBrush_)
                break;
            // Empty space is only filled inside the level's bounds, so a click outside can't run forever.
            int minX = cx, maxX = cx, minY = cy, maxY = cy;
            for (auto& [k, t] : map->tiles) {
                minX = std::min(minX, Tilemap::keyX(k));
                maxX = std::max(maxX, Tilemap::keyX(k));
                minY = std::min(minY, Tilemap::keyY(k));
                maxY = std::max(maxY, Tilemap::keyY(k));
            }
            recordUndo("Fill tiles");
            std::vector<std::pair<int, int>> stack{{cx, cy}};
            int filled = 0;
            while (!stack.empty() && filled < 100000) {
                auto [x, y] = stack.back();
                stack.pop_back();
                if (x < minX || x > maxX || y < minY || y > maxY || map->get(x, y) != target)
                    continue;
                map->set(x, y, tileBrush_);
                ++filled;
                stack.push_back({x + 1, y});
                stack.push_back({x - 1, y});
                stack.push_back({x, y + 1});
                stack.push_back({x, y - 1});
            }
            break;
        }
        }
    } else if (tileStroke_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (std::make_pair(cx, cy) != tileLast_) {
            cellLine(tileLast_.first, tileLast_.second, cx, cy, [&](int x, int y) { map->set(x, y, brush); });
            tileLast_ = {cx, cy};
        }
    } else {
        tileStroke_ = false;
    }
}

} // namespace aven::editor
