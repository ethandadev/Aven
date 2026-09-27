// The Assets panel: the project's files. Click, Ctrl-click and Shift-click select; drag files or
// folders onto a folder (or onto the path at the top) to move them; drag an object from the
// Hierarchy in to save it as a prefab. Renaming and moving fix every reference to the file
// (scenes, prefabs, scripts, the start scene), and deleting asks first and keeps a copy.

#include "editor.h"

#include "aven/core/fs.h"
#include "aven/core/log.h"
#include "aven/scene/reflection.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace aven::editor {

namespace {

bool isImageFile(const std::string& ext) {
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga";
}

// Files that can mention other files by path.
bool mayReferenceFiles(const std::string& ext) {
    return ext == ".scene" || ext == ".prefab" || ext == ".es" || ext == ".blocks";
}

bool insidePath(const std::string& path, const std::string& folder) {
    return path.size() > folder.size() && path.compare(0, folder.size(), folder) == 0 && path[folder.size()] == '/';
}

// A path after the moves: itself, or inside a moved folder.
std::string movedPath(const std::string& path, const std::vector<std::pair<std::string, std::string>>& moves) {
    for (auto& [from, to] : moves) {
        if (path == from)
            return to;
        if (insidePath(path, from))
            return to + path.substr(from.size());
    }
    return path;
}

// Paths are written as strings: "images/a.png" in scenes and blocks, 'images/a.png' or
// "images/a.png" in scripts. Replace whole paths, and paths inside a moved folder.
bool rewritePaths(std::string& text, const std::vector<std::pair<std::string, std::string>>& moves) {
    bool changed = false;
    for (auto& [from, to] : moves)
        for (char q : {'"', '\''}) {
            for (const std::string& end : {std::string(1, q), std::string("/")}) {
                std::string a = q + from + end, b = q + to + end;
                for (size_t at = text.find(a); at != std::string::npos; at = text.find(a, at + b.size())) {
                    text.replace(at, a.size(), b);
                    changed = true;
                }
            }
        }
    return changed;
}

} // namespace

bool Editor::assetSelected(const std::string& rel) const {
    return std::find(selectedAssets_.begin(), selectedAssets_.end(), rel) != selectedAssets_.end();
}

std::vector<std::string> Editor::assetUsers(const std::string& rel) const {
    std::vector<std::string> users;
    for (auto& f : assetFiles_) {
        if (f == rel || !mayReferenceFiles(fs::extension(f)))
            continue;
        auto text = fs::readText(projectDir_ / f);
        if (!text)
            continue;
        for (char q : {'"', '\''})
            if (text->find(q + rel + q) != std::string::npos || text->find(q + rel + "/") != std::string::npos) {
                users.push_back(f);
                break;
            }
    }
    if (settings_.startScene == rel || insidePath(settings_.startScene, rel))
        users.insert(users.begin(), "the start scene (Project Settings)");
    return users;
}

int Editor::moveAssets(const std::vector<std::pair<std::string, std::string>>& requested) {
    if (playing_) {
        notify("Stop the game before moving or renaming files.", true);
        return 0;
    }
    std::vector<std::pair<std::string, std::string>> done;
    std::string problem;
    std::error_code ec;
    for (auto& [from, to] : requested) {
        if (from == to || to.empty())
            continue;
        if (to == from || insidePath(to, from)) {
            problem = "A folder can't go inside itself.";
            continue;
        }
        if (stdfs::exists(projectDir_ / to, ec)) {
            problem = "There's already a " + to + ".";
            continue;
        }
        stdfs::create_directories((projectDir_ / to).parent_path(), ec);
        stdfs::rename(projectDir_ / from, projectDir_ / to, ec);
        if (ec) {
            problem = "Couldn't move " + from + ": " + ec.message();
            continue;
        }
        done.push_back({from, to});
    }
    if (!problem.empty())
        notify(problem, true);
    if (done.empty())
        return 0;

    // References in files on disk.
    scanAssets();
    int filesFixed = 0;
    for (auto& f : assetFiles_) {
        if (!mayReferenceFiles(fs::extension(f)))
            continue;
        auto text = fs::readText(projectDir_ / f);
        if (text && rewritePaths(*text, done)) {
            fs::writeText(projectDir_ / f, *text);
            ++filesFixed;
        }
    }
    // ...and in what's open: the scene (and its undo history), the scene a prefab returns to,
    // script tabs, the start scene.
    auto fixJson = [&](Json& j) {
        std::string text = j.dump();
        if (rewritePaths(text, done))
            j = Json::parse(text);
    };
    {
        Json current = scene_->save();
        std::string text = current.dump();
        if (rewritePaths(text, done)) {
            bool wasDirty = dirty_;
            scene_->load(Json::parse(text));
            snapshotValid_ = false;
            dirty_ = wasDirty;
        }
    }
    for (auto& snap : undo_)
        fixJson(snap.scene);
    for (auto& snap : redo_)
        fixJson(snap.scene);
    if (!prefabReturnScene_.isNull())
        fixJson(prefabReturnScene_);
    scenePath_ = movedPath(scenePath_, done);
    prefabPath_ = movedPath(prefabPath_, done);
    prefabReturnPath_ = movedPath(prefabReturnPath_, done);
    for (auto& t : tabs_)
        t->path = movedPath(t->path, done);
    for (auto& a : selectedAssets_)
        a = movedPath(a, done);
    assetFolder_ = movedPath(assetFolder_, done);
    std::string start = movedPath(settings_.startScene, done);
    if (start != settings_.startScene) {
        settings_.startScene = start;
        settings_.save(projectDir_);
    }
    scanAssets();
    refreshTitle();
    if (filesFixed > 0)
        Log::info("Updated the paths in ", filesFixed, filesFixed == 1 ? " file." : " files.");
    return static_cast<int>(done.size());
}

void Editor::moveAssetsInto(const std::vector<std::string>& items, const std::string& folder) {
    std::vector<std::pair<std::string, std::string>> moves;
    for (auto& rel : items) {
        std::string name = stdfs::path(rel).filename().string();
        std::string to = folder.empty() ? name : folder + "/" + name;
        if (to != rel)
            moves.push_back({rel, to});
    }
    int n = moveAssets(moves);
    if (n > 0)
        notify("Moved " + plural(static_cast<size_t>(n), "item") + " to " + (folder.empty() ? "the project folder" : folder) + ".");
}

void Editor::deleteAssets(const std::vector<std::string>& items) {
    // Into .aven/trash rather than gone for good: easy to get back from the file manager.
    auto stamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    stdfs::path trash = projectDir_ / ".aven" / "trash" / std::to_string(stamp);
    std::error_code ec;
    int deleted = 0;
    for (auto& rel : items) {
        stdfs::path to = trash / rel;
        stdfs::create_directories(to.parent_path(), ec);
        stdfs::rename(projectDir_ / rel, to, ec);
        if (ec) { // e.g. another drive: copy, then remove
            ec.clear();
            stdfs::copy(projectDir_ / rel, to, stdfs::copy_options::recursive, ec);
            if (!ec)
                stdfs::remove_all(projectDir_ / rel, ec);
        }
        if (ec) {
            notify("Couldn't delete " + rel + ": " + ec.message(), true);
            continue;
        }
        ++deleted;
    }
    std::erase_if(selectedAssets_, [&](const std::string& a) {
        return std::any_of(items.begin(), items.end(), [&](const std::string& d) { return a == d || insidePath(a, d); });
    });
    scanAssets();
    if (deleted > 0)
        notify("Deleted " + (deleted == 1 ? items.front() : plural(static_cast<size_t>(deleted), "item")) +
               ". A copy is in .aven/trash in the project folder.");
}

void Editor::drawAssets() {
    ui::panelClass();
    ImGui::Begin("Assets", &showAssets_);
    const bool panelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    std::vector<std::string> dropInto; // what was dropped, and where
    std::string dropFolder;
    bool dropped = false;
    // A folder (or a breadcrumb) accepts files, folders, and objects from the Hierarchy.
    auto folderTarget = [&](const std::string& folder) {
        if (!ImGui::BeginDragDropTarget())
            return;
        for (const char* type : {"ASSET_PATH", "ASSET_FOLDER"})
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload(type)) {
                std::string rel(static_cast<const char*>(pl->Data), static_cast<size_t>(pl->DataSize));
                dropInto = std::find(assetDrag_.begin(), assetDrag_.end(), rel) != assetDrag_.end() ? assetDrag_
                                                                                                  : std::vector<std::string>{rel};
                dropFolder = folder;
                dropped = true;
            }
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ENTITY"))
            if (Entity e = scene().findByUUID({*static_cast<const uint64_t*>(pl->Data)}); e && !playing_)
                savePrefab(e, folder.empty() ? "prefabs" : folder);
        ImGui::EndDragDropTarget();
    };

    // Breadcrumbs (drop onto one to move things up).
    if (ImGui::SmallButton("Project"))
        assetFolder_.clear();
    folderTarget("");
    std::string built;
    size_t start = 0;
    while (start < assetFolder_.size()) {
        size_t slash = assetFolder_.find('/', start);
        std::string part = assetFolder_.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        built += (built.empty() ? "" : "/") + part;
        ImGui::SameLine();
        ImGui::TextDisabled(">");
        ImGui::SameLine();
        ImGui::PushID(built.c_str());
        if (ImGui::SmallButton(part.c_str()))
            assetFolder_ = built;
        folderTarget(built);
        ImGui::PopID();
        if (slash == std::string::npos)
            break;
        start = slash + 1;
    }
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10, ImGui::GetWindowWidth() - 390));
    ImGui::SetNextItemWidth(170);
    ImGui::InputTextWithHint("##assetsearch", "Search files", &assetSearch_);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Size");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    ImGui::SliderFloat("##cell", &assetCell_, 64, 160, "");
    ImGui::SameLine();
    if (ImGui::SmallButton("Library"))
        openAssetLibrary();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Ready-made sprites, animations, backgrounds, 3D models and textures");
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Create"))
        ImGui::OpenPopup("create_asset");
    if (ImGui::BeginPopup("create_asset")) {
        if (ImGui::MenuItem("Blocks script"))
            openBlocks(newScriptFile("new_blocks", true));
        if (ImGui::MenuItem("EasyScript"))
            openScript(newScriptFile("new_script", false));
        if (ImGui::MenuItem("Scene (2D)"))
            newScene(false);
        if (ImGui::MenuItem("Scene (3D)"))
            newScene(true);
        if (ImGui::MenuItem("Folder")) {
            std::string p = uniqueName(assetFolder_.empty() ? "." : assetFolder_, "new_folder", "");
            std::error_code ec;
            stdfs::create_directories(projectDir_ / p, ec);
            scanAssets();
            // Name it straight away.
            renameTarget_ = fs::relativePath(projectDir_ / p, projectDir_);
            std::snprintf(renameBuffer_, sizeof renameBuffer_, "%s", stdfs::path(p).filename().string().c_str());
        }
        ImGui::EndPopup();
    }
    ImGui::Separator();

    ImGui::BeginChild("##files");
    std::error_code ec;
    stdfs::path dir = projectDir_ / assetFolder_;
    if (!assetFolder_.empty() && !stdfs::is_directory(dir, ec)) {
        assetFolder_.clear(); // it was moved or deleted outside Aven
        dir = projectDir_;
    }
    std::vector<stdfs::directory_entry> entries;
    if (!assetSearch_.empty()) {
        // Searching looks through every folder.
        std::string needle = assetSearch_;
        std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
        for (auto& f : assetFiles_) {
            std::string hay = f;
            std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
            if (hay.find(needle) != std::string::npos)
                entries.emplace_back(projectDir_ / f, ec);
        }
    } else {
        for (auto& entry : stdfs::directory_iterator(dir, ec)) {
            std::string name = entry.path().filename().string();
            if (name.empty() || name[0] == '.' || name == ProjectSettings::kFileName || name == "tutorial.json")
                continue;
            entries.push_back(entry);
        }
    }
    std::sort(entries.begin(), entries.end(), [](auto& a, auto& b) {
        if (a.is_directory() != b.is_directory())
            return a.is_directory();
        return a.path().filename() < b.path().filename();
    });
    std::vector<std::string> visible;
    for (auto& entry : entries)
        visible.push_back(fs::relativePath(entry.path(), projectDir_));

    float cell = assetCell_;
    int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / (cell + 8)));
    int i = 0;
    std::vector<std::string> askDelete;
    for (auto& entry : entries) {
        std::string name = entry.path().filename().string();
        std::string rel = visible[static_cast<size_t>(i)];
        std::string ext = fs::extension(entry.path());
        bool isDir = entry.is_directory();
        if (i++ % columns)
            ImGui::SameLine();
        ImGui::PushID(rel.c_str());
        ImGui::BeginGroup();
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool isSelected = assetSelected(rel);
        ImGui::InvisibleButton("##item", {cell, cell});
        bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, {p.x + cell, p.y + cell}, isSelected ? ImGui::GetColorU32(ImGuiCol_Header) : hovered ? ImGui::GetColorU32(ImGuiCol_FrameBgHovered) : 0, 6);
        ImVec2 iconMin{p.x + 16, p.y + 8}, iconMax{p.x + cell - 16, p.y + cell - 30};
        if (isDir) {
            dl->AddRectFilled({iconMin.x, iconMin.y + 6}, iconMax, IM_COL32(230, 180, 70, 255), 4);
            dl->AddRectFilled(iconMin, {iconMin.x + 22, iconMin.y + 10}, IM_COL32(230, 180, 70, 255), 3);
        } else if (isImageFile(ext)) {
            const TextureAsset& t = assets_.texture(rel, true);
            float aspect = t.height ? static_cast<float>(t.width) / t.height : 1.0f;
            float w = iconMax.x - iconMin.x, h = iconMax.y - iconMin.y;
            ImVec2 size = aspect > w / h ? ImVec2(w, w / aspect) : ImVec2(h * aspect, h);
            ImVec2 o{iconMin.x + (w - size.x) * 0.5f, iconMin.y + (h - size.y) * 0.5f};
            dl->AddImage(static_cast<ImTextureID>(device_.nativeTexture(t.handle)), o, {o.x + size.x, o.y + size.y}, {0, 1}, {1, 0});
        } else {
            ImU32 col = IM_COL32(120, 130, 150, 255);
            const char* badge = "FILE";
            if (ext == ".es") { col = IM_COL32(255, 140, 90, 255); badge = "CODE"; }
            else if (ext == ".c" || ext == ".cpp" || ext == ".cc") { col = IM_COL32(90, 140, 230, 255); badge = ext == ".c" ? "C" : "C++"; }
            else if (ext == ".h" || ext == ".hpp") { col = IM_COL32(120, 150, 200, 255); badge = "HEADER"; }
            else if (ext == ".blocks") { col = IM_COL32(242, 176, 30, 255); badge = "BLOCKS"; }
            else if (ext == ".scene") { col = IM_COL32(90, 170, 255, 255); badge = "SCENE"; }
            else if (ext == ".prefab") { col = IM_COL32(120, 220, 170, 255); badge = "PREFAB"; }
            else if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") { col = IM_COL32(207, 99, 207, 255); badge = "SOUND"; }
            else if (ext == ".gltf" || ext == ".glb") { col = IM_COL32(170, 140, 255, 255); badge = "MODEL"; }
            else if (ext == ".ttf" || ext == ".otf") { col = IM_COL32(200, 200, 200, 255); badge = "FONT"; }
            dl->AddRectFilled(iconMin, iconMax, col, 6);
            ImVec2 ts = ImGui::CalcTextSize(badge);
            dl->AddText({(iconMin.x + iconMax.x - ts.x) * 0.5f, (iconMin.y + iconMax.y - ts.y) * 0.5f}, IM_COL32(20, 20, 30, 255), badge);
        }
        size_t maxChars = static_cast<size_t>(std::max(6.0f, cell / 7.0f));
        std::string shown = name.size() > maxChars ? name.substr(0, maxChars - 2) + ".." : name;
        ImVec2 ts = ImGui::CalcTextSize(shown.c_str());
        dl->AddText({p.x + (cell - ts.x) * 0.5f, p.y + cell - 22}, ImGui::GetColorU32(ImGuiCol_Text), shown.c_str());
        if (hovered && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            ImGui::SetTooltip("%s", assetSearch_.empty() ? name.c_str() : rel.c_str());

        // Selecting: click, Ctrl-click (add or remove), Shift-click (a range). A click on one of
        // several selected items waits for the release, so dragging moves all of them.
        if (ImGui::IsItemClicked()) {
            ImGuiIO& io = ImGui::GetIO();
            if (io.KeyCtrl || io.KeySuper) {
                if (isSelected)
                    std::erase(selectedAssets_, rel);
                else
                    selectedAssets_.push_back(rel);
                assetAnchor_ = rel;
            } else if (io.KeyShift && !assetAnchor_.empty()) {
                auto a = std::find(visible.begin(), visible.end(), assetAnchor_);
                auto b = std::find(visible.begin(), visible.end(), rel);
                if (a != visible.end() && b != visible.end()) {
                    if (a > b)
                        std::swap(a, b);
                    selectedAssets_.assign(a, b + 1);
                }
            } else if (isSelected && selectedAssets_.size() > 1) {
                assetPendingSelect_ = rel;
            } else {
                selectedAssets_ = {rel};
                assetAnchor_ = rel;
            }
        }
        if (assetPendingSelect_ == rel && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (hovered && ImGui::GetIO().MouseDragMaxDistanceSqr[0] < 16.0f) {
                selectedAssets_ = {rel};
                assetAnchor_ = rel;
            }
            assetPendingSelect_.clear();
        }
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (isDir) {
                assetFolder_ = rel;
                assetSearch_.clear();
            } else if (ext == ".scene") {
                openScene(rel);
            } else if (ext == ".es" || ext == ".blocks" || isNativeSource(rel)) {
                openScript(rel);
            } else if (ext == ".prefab") {
                openPrefab(rel);
            } else if (ext == ".png" && unlocked(Feature::PixelEditor)) {
                openPixelEditor(rel);
            }
        }
        if (ImGui::BeginDragDropSource()) {
            if (assetDrag_.empty()) // the drag's first frame
                assetDrag_ = assetSelected(rel) ? selectedAssets_ : std::vector<std::string>{rel};
            ImGui::SetDragDropPayload(isDir ? "ASSET_FOLDER" : "ASSET_PATH", rel.data(), rel.size());
            if (assetDrag_.size() > 1)
                ImGui::Text("%d items", static_cast<int>(assetDrag_.size()));
            else
                ImGui::Text("%s", name.c_str());
            ImGui::EndDragDropSource();
        }
        if (isDir)
            folderTarget(rel);
        if (ImGui::BeginPopupContextItem()) {
            if (!isSelected)
                selectedAssets_ = {rel};
            size_t count = selectedAssets_.size();
            if (count == 1 && ImGui::MenuItem("Rename", "F2")) {
                renameTarget_ = rel;
                std::snprintf(renameBuffer_, sizeof renameBuffer_, "%s", name.c_str());
            }
            if (ext == ".prefab" && count == 1 && ImGui::MenuItem("Add to scene"))
                instantiatePrefab(rel, view3D_ ? Vec3{0, 0, 0} : Vec3{cam2D_.x, cam2D_.y, 0});
            if (ext == ".prefab" && count == 1 && ImGui::MenuItem("Edit prefab"))
                openPrefab(rel);
            if (ext == ".png" && count == 1 && unlocked(Feature::PixelEditor) && ImGui::MenuItem("Edit in Pixel Editor"))
                openPixelEditor(rel);
            if (ImGui::MenuItem("Duplicate", nullptr, false, !isDir)) {
                int made = 0;
                for (auto& item : selectedAssets_) {
                    if (stdfs::is_directory(projectDir_ / item, ec))
                        continue;
                    stdfs::path relPath(item);
                    std::string copy = uniqueName(relPath.parent_path().generic_string(), relPath.stem().string() + "_copy",
                                                  relPath.extension().string());
                    std::error_code copyError;
                    stdfs::copy_file(projectDir_ / item, projectDir_ / copy, copyError);
                    if (copyError)
                        notify("Couldn't duplicate " + item + ": " + copyError.message(), true);
                    else
                        ++made;
                }
                if (made)
                    notify("Made " + plural(static_cast<size_t>(made), "copy", "copies") + ".");
                scanAssets();
            }
            if (ImGui::MenuItem("Show in file manager"))
                openExternal((isDir ? projectDir_ / rel : (projectDir_ / rel).parent_path()).string());
            ImGui::Separator();
            if (ImGui::MenuItem(count > 1 ? ("Delete " + std::to_string(count) + " items").c_str() : "Delete", "Del"))
                askDelete = selectedAssets_;
            ImGui::EndPopup();
        }
        ImGui::EndGroup();
        ImGui::PopID();
    }
    if (entries.empty())
        ImGui::TextDisabled(assetSearch_.empty() ? "This folder is empty. Drag images, sounds or models from your computer into this window."
                                                 : "No files match.");
    // The empty space below the files: a drop target for the folder being shown, and a click
    // there clears the selection.
    ImVec2 rest = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##empty", {std::max(rest.x, 1.0f), std::max(rest.y, 40.0f)});
    if (ImGui::IsItemClicked())
        selectedAssets_.clear();
    if (assetSearch_.empty())
        folderTarget(assetFolder_);
    ImGui::EndChild();

    // Keys, while the panel has focus.
    if (panelFocused && !ImGui::GetIO().WantTextInput && !playing_) {
        if ((ImGui::IsKeyPressed(ImGuiKey_Delete) || (ImGui::GetIO().KeySuper && ImGui::IsKeyPressed(ImGuiKey_Backspace))) &&
            !selectedAssets_.empty())
            askDelete = selectedAssets_;
        if (ImGui::IsKeyPressed(ImGuiKey_F2) && selectedAssets_.size() == 1) {
            renameTarget_ = selectedAssets_.front();
            std::snprintf(renameBuffer_, sizeof renameBuffer_, "%s", stdfs::path(renameTarget_).filename().string().c_str());
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A))
            selectedAssets_ = visible;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            selectedAssets_.clear();
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !ImGui::GetIO().KeySuper && !assetFolder_.empty())
            assetFolder_ = stdfs::path(assetFolder_).parent_path().generic_string(); // up a folder
    }

    if (dropped) {
        // Dropping onto a folder that's being moved (or into itself) does nothing.
        std::erase_if(dropInto, [&](const std::string& item) { return item == dropFolder || insidePath(dropFolder, item); });
        if (!dropInto.empty())
            moveAssetsInto(dropInto, dropFolder);
        assetDrag_.clear();
    }
    if (!ImGui::GetDragDropPayload())
        assetDrag_.clear();

    // --- rename
    if (!renameTarget_.empty() && !ImGui::IsPopupOpen("Rename file"))
        ImGui::OpenPopup("Rename file");
    if (ImGui::BeginPopupModal("Rename file", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(300);
        bool enter = ImGui::InputText("##rename", renameBuffer_, sizeof renameBuffer_, ImGuiInputTextFlags_EnterReturnsTrue);
        std::string newName = renameBuffer_;
        while (!newName.empty() && newName.back() == ' ')
            newName.pop_back();
        stdfs::path from(renameTarget_);
        // Keep the type: "hero" for "player.png" becomes "hero.png".
        bool isDir = stdfs::is_directory(projectDir_ / renameTarget_, ec);
        if (!isDir && !newName.empty() && stdfs::path(newName).extension().empty())
            newName += from.extension().string();
        std::string to = (from.parent_path() / newName).generic_string();
        std::string why;
        if (newName.empty())
            why = "Type a name.";
        else if (newName.find_first_of("/\\:*?\"<>|") != std::string::npos)
            why = "Names can't contain / \\ : * ? \" < > |";
        else if (to != renameTarget_ && stdfs::exists(projectDir_ / to, ec))
            why = "There's already a file called " + newName + " here.";
        if (!why.empty())
            ImGui::TextColored({1, 0.6f, 0.4f, 1}, "%s", why.c_str());
        else if (!isDir && stdfs::path(newName).extension() != from.extension())
            ImGui::TextColored({1, 0.8f, 0.35f, 1}, "The type changes from %s to %s.", from.extension().string().c_str(),
                               stdfs::path(newName).extension().string().c_str());
        else
            ImGui::TextDisabled("Scenes, prefabs and scripts that use it are updated too.");
        ImGui::BeginDisabled(!why.empty());
        bool ok = ImGui::Button("Rename", {120, 0});
        ImGui::EndDisabled();
        if ((enter || ok) && why.empty()) {
            if (to != renameTarget_)
                moveAssets({{renameTarget_, to}});
            renameTarget_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {120, 0}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            renameTarget_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // --- delete (asks first, and says what still uses it)
    if (!askDelete.empty()) {
        confirmDelete_ = askDelete;
        deleteUsers_.clear();
        for (auto& item : confirmDelete_)
            for (auto& u : assetUsers(item))
                if (std::find(deleteUsers_.begin(), deleteUsers_.end(), u) == deleteUsers_.end() &&
                    std::find(confirmDelete_.begin(), confirmDelete_.end(), u) == confirmDelete_.end())
                    deleteUsers_.push_back(u);
        ImGui::OpenPopup("Delete files?");
    }
    ImGui::SetNextWindowSize({440, 0});
    if (ImGui::BeginPopupModal("Delete files?", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextUnformatted(confirmDelete_.size() == 1 ? "Delete this?" : ("Delete these " + std::to_string(confirmDelete_.size()) + "?").c_str());
        for (size_t k = 0; k < confirmDelete_.size() && k < 8; ++k)
            ImGui::BulletText("%s", confirmDelete_[k].c_str());
        if (confirmDelete_.size() > 8)
            ImGui::TextDisabled("  ...and %d more", static_cast<int>(confirmDelete_.size() - 8));
        if (!deleteUsers_.empty()) {
            ImGui::Spacing();
            ImGui::TextColored({1, 0.8f, 0.35f, 1}, "Still used by:");
            for (size_t k = 0; k < deleteUsers_.size() && k < 6; ++k)
                ImGui::BulletText("%s", deleteUsers_[k].c_str());
        }
        ImGui::Spacing();
        ImGui::TextDisabled("A copy goes to .aven/trash in the project folder.");
        ImGui::Spacing();
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.2f, 0.2f, 1));
        if (ImGui::Button("Delete", {120, 0}) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            deleteAssets(confirmDelete_);
            confirmDelete_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {120, 0}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            confirmDelete_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::End();
}

} // namespace aven::editor
