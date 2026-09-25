// The welcome screen: pick a template to start a new game, or open an existing one.

#include "editor.h"

#include "aven/core/fs.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>

namespace aven::editor {

namespace {

ImU32 toU32(Color c, float alpha = 1.0f) {
    return ImGui::ColorConvertFloat4ToU32({c.r, c.g, c.b, c.a * alpha});
}

void pushFont(ImFont* f) { ImGui::PushFont(f ? f : ImGui::GetFont()); }

// Makes a folder name safe to create on every OS.
std::string safeFolderName(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            continue;
        out += c;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
        out.pop_back();
    while (!out.empty() && out.front() == ' ')
        out.erase(out.begin());
    return out;
}

bool accentButton(const char* label, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_SliderGrabActive));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return pressed;
}

// A rounded label like "3D" or "Beginner".
void badge(ImDrawList* dl, ImVec2& cursor, const char* text, ImU32 bg, ImU32 fg) {
    ImVec2 ts = ImGui::CalcTextSize(text);
    ImVec2 pad{6, 2};
    dl->AddRectFilled(cursor, {cursor.x + ts.x + pad.x * 2, cursor.y + ts.y + pad.y * 2}, bg, 4);
    dl->AddText({cursor.x + pad.x, cursor.y + pad.y}, fg, text);
    cursor.x += ts.x + pad.x * 2 + 5;
}

} // namespace

bool Editor::drawFolderBrowser(bool projectsOnly, stdfs::path* picked) {
    bool result = false;
    std::error_code ec;
    if (browsePath_.empty() || !stdfs::is_directory(browsePath_, ec))
        browsePath_ = stdfs::current_path(ec);

    if (ImGui::Button("Up") && browsePath_.has_parent_path() && browsePath_.parent_path() != browsePath_)
        browsePath_ = browsePath_.parent_path();
    ImGui::SameLine();
    if (ImGui::Button("Home")) {
        if (const char* home = std::getenv("HOME"))
            browsePath_ = home;
        else if (const char* profile = std::getenv("USERPROFILE"))
            browsePath_ = profile;
    }
    ImGui::SameLine();
    std::string pathText = browsePath_.string();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##path", &pathText, ImGuiInputTextFlags_EnterReturnsTrue) && stdfs::is_directory(pathText, ec))
        browsePath_ = pathText;

    struct Entry {
        std::string name;
        bool project;
    };
    std::vector<Entry> entries;
    for (auto& e : stdfs::directory_iterator(browsePath_, stdfs::directory_options::skip_permission_denied, ec)) {
        std::error_code ec2;
        if (!e.is_directory(ec2))
            continue;
        std::string name = e.path().filename().string();
        if (name.empty() || name[0] == '.')
            continue;
        entries.push_back({name, ProjectSettings::isProject(e.path())});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.project != b.project ? a.project : a.name < b.name;
    });

    float footer = ImGui::GetFrameHeightWithSpacing() + 6;
    ImGui::BeginChild("##folders", {0, -footer}, ImGuiChildFlags_Borders);
    if (ProjectSettings::isProject(browsePath_))
        ImGui::TextColored({0.45f, 0.75f, 1.0f, 1.0f}, "This folder is an Aven project.");
    if (entries.empty())
        ImGui::TextDisabled("No folders here.");
    for (auto& e : entries) {
        ImGui::PushID(e.name.c_str());
        std::string label = (e.project ? "[game]  " : "[folder]  ") + e.name;
        if (e.project)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab));
        bool clicked = ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
        if (e.project)
            ImGui::PopStyleColor();
        if (clicked) {
            stdfs::path full = browsePath_ / e.name;
            if (projectsOnly && e.project) {
                *picked = full;
                result = true;
            } else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || projectsOnly) {
                browsePath_ = full;
            }
        }
        if (ImGui::IsItemHovered() && !e.project && !projectsOnly)
            ImGui::SetTooltip("Double-click to go inside");
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (projectsOnly) {
        ImGui::BeginDisabled(!ProjectSettings::isProject(browsePath_));
        if (accentButton("Open this project", {200, 0})) {
            *picked = browsePath_;
            result = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("Pick a folder marked [game], or any folder with a project.aven file.");
    } else {
        if (accentButton("Use this folder", {160, 0})) {
            *picked = browsePath_;
            result = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("New folder...")) {
            ImGui::OpenPopup("New folder");
        }
        if (ImGui::BeginPopup("New folder")) {
            static std::string folderName = "Aven Games";
            ImGui::SetNextItemWidth(220);
            bool enter = ImGui::InputText("##name", &folderName, ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if ((ImGui::Button("Create") || enter) && !safeFolderName(folderName).empty()) {
                stdfs::create_directories(browsePath_ / safeFolderName(folderName), ec);
                browsePath_ /= safeFolderName(folderName);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    return result;
}

void Editor::drawHub() {
    if (!templatesScanned_) {
        templateCache_ = templates();
        templatesScanned_ = true;
        if (newProjectTemplate_.empty() && !templateCache_.empty())
            newProjectTemplate_ = templateCache_.front().id;
    }

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
    ImGui::Begin("##hub", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    float em = ImGui::GetFontSize();

    // ---- sidebar
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {em * 1.2f, em * 1.2f});
    ImGui::BeginChild("##side", {em * 17, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        // Logo: a little blue diamond next to the name.
        float r = em * 0.75f;
        ImVec2 c{p.x + r, p.y + r * 1.1f};
        dl->AddQuadFilled({c.x, c.y - r}, {c.x + r, c.y}, {c.x, c.y + r}, {c.x - r, c.y}, ImGui::GetColorU32(ImGuiCol_SliderGrab));
        dl->AddQuadFilled({c.x, c.y - r * 0.45f}, {c.x + r * 0.45f, c.y}, {c.x, c.y + r * 0.45f}, {c.x - r * 0.45f, c.y},
                          ImGui::GetColorU32(ImGuiCol_WindowBg));
        ImGui::SetCursorScreenPos({p.x + r * 2 + em * 0.6f, p.y});
        pushFont(fonts.big);
        ImGui::TextUnformatted("Aven");
        ImGui::PopFont();
        ImGui::TextDisabled("Make games. Learn as you go.");
        ImGui::Dummy({0, em * 0.8f});

        auto navButton = [&](const char* label, int page) {
            bool active = hubPage_ == page;
            ImGui::PushStyleColor(ImGuiCol_Button, active ? ImGui::GetColorU32(ImGuiCol_Header) : IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, {0.0f, 0.5f});
            if (ImGui::Button(label, {-1, em * 2.2f}))
                hubPage_ = page;
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        };
        navButton("  +   New game", 0);
        navButton("  >   Open a game", 1);

        ImGui::Dummy({0, em * 0.8f});
        ImGui::SeparatorText("Recent");
        if (recentProjects_.empty())
            ImGui::TextDisabled("Games you open show up here.");
        std::string removeRecent;
        for (auto& path : recentProjects_) {
            ImGui::PushID(path.c_str());
            stdfs::path p2(path);
            std::string name = p2.filename().string();
            ImVec2 start = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##recent", false, 0, {0, em * 2.4f}))
                openProject(path);
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Remove from list"))
                    removeRecent = path;
                ImGui::EndPopup();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", path.c_str());
            ImDrawList* sdl = ImGui::GetWindowDrawList();
            sdl->AddText({start.x + 4, start.y + 2}, ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
            std::string parent = p2.parent_path().string();
            float maxW = ImGui::GetContentRegionAvail().x - 8;
            while (parent.size() > 4 && ImGui::CalcTextSize(parent.c_str()).x > maxW)
                parent = "..." + parent.substr(std::min<size_t>(parent.size(), 6));
            sdl->AddText({start.x + 4, start.y + em * 1.15f}, ImGui::GetColorU32(ImGuiCol_TextDisabled), parent.c_str());
            ImGui::PopID();
        }
        if (!removeRecent.empty()) {
            std::erase(recentProjects_, removeRecent);
            saveRecent();
        }

        ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - em * 3.2f));
        ImGui::TextDisabled("Aven %s", AVEN_VERSION);
        if (hasProject()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Back to project"))
                showHub_ = false;
        }
    }
    ImGui::EndChild();
    ImGui::SameLine(0, 0);

    // ---- main area
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {em * 1.6f, em * 1.4f});
    ImGui::BeginChild("##main", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    if (hubPage_ == 0) {
        pushFont(fonts.big);
        ImGui::TextUnformatted("Start a new game");
        ImGui::PopFont();
        ImGui::TextDisabled("Pick a template. Every template is a small, working game you can play right away and change.");
        ImGui::Dummy({0, em * 0.4f});

        float footerH = em * 6.5f;
        ImGui::BeginChild("##cards", {0, -footerH});
        const TemplateInfo* chosen = nullptr;
        if (templateCache_.empty()) {
            ImGui::TextDisabled("No templates were found next to the editor. You can still start from an empty project.");
        }
        float cardW = em * 15.5f, cardH = em * 14.5f, gap = em * 0.9f;
        float avail = ImGui::GetContentRegionAvail().x;
        int columns = std::max(1, static_cast<int>((avail + gap) / (cardW + gap)));
        cardW = (avail - gap * (columns - 1)) / columns;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (size_t i = 0; i < templateCache_.size(); ++i) {
            const TemplateInfo& t = templateCache_[i];
            if (i % columns != 0)
                ImGui::SameLine(0, gap);
            ImGui::PushID(t.id.c_str());
            ImVec2 p = ImGui::GetCursorScreenPos();
            bool clicked = ImGui::InvisibleButton("##card", {cardW, cardH});
            bool hovered = ImGui::IsItemHovered();
            bool selected = newProjectTemplate_ == t.id;
            if (clicked)
                newProjectTemplate_ = t.id;
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                newProjectTemplate_ = t.id;
            if (selected)
                chosen = &t;
            ImVec2 q{p.x + cardW, p.y + cardH};
            dl->AddRectFilled(p, q, ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), 8);
            // Header: a screenshot of the game if the template has one, else its color and initial.
            float headerH = cardH * 0.5f;
            std::error_code thumbError;
            stdfs::path thumb = t.folder / "thumbnail.png";
            const TextureAsset* tex = stdfs::exists(thumb, thumbError) ? &assets_.texture(thumb.string()) : nullptr;
            if (tex && !tex->missing && tex->width > 0) {
                // Crop the image to fill the header ("cover").
                float boxAspect = cardW / headerH, imgAspect = static_cast<float>(tex->width) / tex->height;
                ImVec2 uv0{0, 1}, uv1{1, 0};
                if (imgAspect > boxAspect) {
                    float crop = (1 - boxAspect / imgAspect) * 0.5f;
                    uv0.x = crop;
                    uv1.x = 1 - crop;
                } else {
                    float crop = (1 - imgAspect / boxAspect) * 0.5f;
                    uv0.y = 1 - crop;
                    uv1.y = crop;
                }
                dl->AddImageRounded(static_cast<ImTextureID>(device_.nativeTexture(tex->handle)), p, {q.x, p.y + headerH}, uv0, uv1,
                                    IM_COL32(255, 255, 255, 255), 8, ImDrawFlags_RoundCornersTop);
            } else {
                dl->AddRectFilled(p, {q.x, p.y + headerH}, toU32(t.color), 8, ImDrawFlags_RoundCornersTop);
                dl->AddRectFilledMultiColor({p.x, p.y + headerH * 0.4f}, {q.x, p.y + headerH}, IM_COL32(0, 0, 0, 0),
                                            IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 60), IM_COL32(0, 0, 0, 60));
                if (fonts.big) {
                    std::string initial = t.name.substr(0, 1);
                    dl->AddText(fonts.big, fonts.big->FontSize * 1.3f,
                                {p.x + em * 0.8f, p.y + headerH * 0.5f - fonts.big->FontSize * 0.65f}, IM_COL32(255, 255, 255, 200),
                                initial.c_str());
                }
            }
            dl->AddRectFilled({p.x, p.y + headerH - 3}, {q.x, p.y + headerH}, toU32(t.color));
            ImVec2 badgePos{q.x - em * 4.2f, p.y + em * 0.6f};
            badge(dl, badgePos, t.is3D ? "3D" : "2D", IM_COL32(0, 0, 0, 90), IM_COL32(255, 255, 255, 255));
            // Text.
            float x = p.x + em * 0.8f, y = p.y + headerH + em * 0.5f;
            if (fonts.bold)
                dl->AddText(fonts.bold, fonts.bold->FontSize, {x, y}, ImGui::GetColorU32(ImGuiCol_Text), t.name.c_str());
            y += em * 1.4f;
            // Two lines of description; the tooltip shows all of it.
            float lineH = ImGui::GetFontSize() * 0.92f;
            dl->PushClipRect({x, y}, {q.x - em * 0.6f, y + lineH * 2.05f}, true);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.92f, {x, y}, ImGui::GetColorU32(ImGuiCol_TextDisabled), t.description.c_str(),
                        nullptr, cardW - em * 1.6f);
            dl->PopClipRect();
            ImVec2 tagPos{x, q.y - em * 1.6f};
            badge(dl, tagPos, t.style.c_str(), ImGui::GetColorU32(ImGuiCol_SliderGrab, 0.25f), ImGui::GetColorU32(ImGuiCol_Text));
            badge(dl, tagPos, t.difficulty.c_str(), ImGui::GetColorU32(ImGuiCol_Border), ImGui::GetColorU32(ImGuiCol_Text));
            if (selected)
                dl->AddRect({p.x - 1, p.y - 1}, {q.x + 1, q.y + 1}, ImGui::GetColorU32(ImGuiCol_SliderGrab), 9, 0, 2.5f);
            if (hovered && !t.description.empty()) {
                ImGui::SetNextWindowSize({em * 20, 0});
                ImGui::BeginTooltip();
                ImGui::TextWrapped("%s", t.description.c_str());
                ImGui::EndTooltip();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();

        // ---- footer: name, location, create
        ImGui::Separator();
        ImGui::Dummy({0, em * 0.2f});
        std::string folderName = safeFolderName(newProjectName_);
        stdfs::path target = browsePath_ / folderName;
        std::error_code ec;
        bool exists = !folderName.empty() && stdfs::exists(target, ec);
        if (ImGui::BeginTable("##form", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, em * 5.5f);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Game name");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(em * 18);
            ImGui::InputText("##name", newProjectName_, sizeof(newProjectName_));
            ImGui::SameLine();
            ImGui::BeginDisabled(folderName.empty() || exists);
            bool create = accentButton(chosen ? "Create game" : "Create empty game", {em * 10, 0});
            ImGui::EndDisabled();
            if (exists) {
                ImGui::SameLine();
                ImGui::TextColored({1.0f, 0.6f, 0.4f, 1.0f}, "A folder with that name already exists here.");
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Save in");
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", target.string().c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Change..."))
                hubPickFolder_ = true;
            ImGui::EndTable();
            if (create) {
                if (createProject(target, newProjectName_, chosen))
                    notify("Created '" + std::string(newProjectName_) + "'. Press Play to try it!");
            }
        }

        if (hubPickFolder_) {
            ImGui::OpenPopup("Choose where to save");
            hubPickFolder_ = false;
        }
        ImGui::SetNextWindowSize({em * 38, em * 26}, ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Choose where to save", nullptr)) {
            stdfs::path picked;
            if (drawFolderBrowser(false, &picked)) {
                browsePath_ = picked;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    } else {
        pushFont(fonts.big);
        ImGui::TextUnformatted("Open a game");
        ImGui::PopFont();
        ImGui::TextDisabled("Find the folder of a game you made before. Aven games have a project.aven file inside.");
        ImGui::Dummy({0, em * 0.4f});
        stdfs::path picked;
        if (drawFolderBrowser(true, &picked))
            openProject(picked);
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace aven::editor
