#include "prefs.h"

#include "aven/core/embedded.h"
#include "aven/core/fs.h"
#include "editor.h"

#include <ImGuizmo.h>
#include <imgui_internal.h>

#include <algorithm>

namespace aven::editor {

// ---------------------------------------------------------------- themes

const std::vector<ThemePreset>& themePresets() {
    // background, panel, frame, frame hover, text, dim text, border, accent
    static const std::vector<ThemePreset> list = {
        {"Midnight", false, 0x181A1F, 0x1E2127, 0x2A2E36, 0x343943, 0xE2E6ED, 0x808896, 0x32363E, 0x3B82F6},
        {"Daylight", true, 0xE4E7EC, 0xF5F6F8, 0xE1E5EB, 0xD4DAE2, 0x1E222A, 0x6E7682, 0xCDD2DA, 0x2563EB},
        {"Ocean", false, 0x0C1822, 0x10202D, 0x1A3040, 0x223C50, 0xD7EBF5, 0x6E8CA0, 0x223848, 0x06B6D4},
        {"Forest", false, 0x121A14, 0x18221B, 0x223026, 0x2C3E31, 0xDCEBDE, 0x78917D, 0x28382C, 0x22C55E},
        {"Candy", false, 0x201426, 0x2A1A32, 0x3E2648, 0x4E305A, 0xFAE6FA, 0xAA8CB4, 0x462C50, 0xEC4899},
        {"Sunset", false, 0x221614, 0x2C1C1A, 0x3E2824, 0x4E322C, 0xFAE8DC, 0xAF8C7D, 0x462E28, 0xF97316},
        {"Retro Terminal", false, 0x080C08, 0x0C120C, 0x142014, 0x1C2E1C, 0xAAFFAA, 0x5A965A, 0x1E3C1E, 0x22C55E},
        {"Cloud", true, 0xDDE7F2, 0xF0F5FB, 0xDCE6F1, 0xCBD9EA, 0x1B2533, 0x66768A, 0xC3D2E4, 0x8B5CF6},
        {"High Contrast", false, 0x000000, 0x000000, 0x1E1E1E, 0x3C3C3C, 0xFFFFFF, 0xC8C8C8, 0xFFFFFF, 0xFFD400},
    };
    return list;
}

namespace {

ImVec4 hex(uint32_t c, float a = 1.0f) {
    return {((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f, a};
}
ImVec4 toVec(Color c, float a = -1) { return {c.r, c.g, c.b, a < 0 ? c.a : a}; }
ImVec4 lerp4(ImVec4 a, ImVec4 b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
}

std::string prefsPath() { return (fs::userDataDir("Aven Editor") / "preferences.json").string(); }

Json colorJson(Color c) {
    Json a = Json::array();
    for (float v : {c.r, c.g, c.b, c.a})
        a.push(v);
    return a;
}
Color colorFrom(const Json& j, Color fallback) {
    if (!j.isArray() || j.size() < 3)
        return fallback;
    return {j[0].asFloat(), j[1].asFloat(), j[2].asFloat(), j.size() > 3 ? j[3].asFloat(1) : 1.0f};
}

} // namespace

ImU32 mixColor(ImU32 a, ImU32 b, float t) {
    ImVec4 x = ImGui::ColorConvertU32ToFloat4(a), y = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(lerp4(x, y, t));
}

// ---------------------------------------------------------------- saving

const ThemePreset& Prefs::themePreset() const {
    for (auto& t : themePresets())
        if (theme == t.name)
            return t;
    return themePresets().front();
}

Color Prefs::accentColor() const {
    if (customAccent)
        return accent;
    uint32_t a = themePreset().accent;
    return Color::fromHex(a);
}

Json Prefs::toJson() const {
    Json j = Json::object();
    j["theme"] = theme;
    j["custom_accent"] = customAccent;
    j["accent"] = colorJson(accent);
    j["ui_scale"] = uiScale;
    j["font_size"] = fontSize;
    j["code_font_size"] = codeFontSize;
    j["code_theme"] = codeTheme;
    j["rounded"] = rounded;
    j["compact"] = compact;
    j["colorblind_safe"] = colorblindSafe;
    j["layout"] = layout;
    Json layouts = Json::object();
    for (auto& [name, ini] : savedLayouts)
        layouts[name] = ini;
    j["saved_layouts"] = layouts;
    j["grid_color"] = colorJson(gridColor);
    j["grid_opacity"] = gridOpacity;
    j["selection_color"] = colorJson(selectionColor);
    j["fly_speed"] = flySpeed;
    j["show_hints"] = showHints;
    j["show_icons"] = showIcons;
    j["show_colliders"] = showColliders;
    j["show_debug_draw"] = showDebugDraw;
    j["view_resolution"] = viewResolution;
    j["viewport_quality"] = viewportQuality;
    j["preview_particles"] = previewParticles;
    j["move_snap"] = moveSnap;
    j["rotate_snap"] = rotateSnap;
    j["scale_snap"] = scaleSnap;
    j["gizmo_local"] = gizmoLocal;
    j["autosave_minutes"] = autosaveMinutes;
    j["clear_console_on_play"] = clearConsoleOnPlay;
    j["pause_on_error"] = pauseOnError;
    j["confirm_delete"] = confirmDelete;
    j["paste_in_place"] = pasteInPlace;
    j["ui_sounds"] = uiSounds;
    j["record_replays"] = recordReplays;
    j["save_energy"] = saveEnergy;
    j["level"] = level;
    j["auto_level_up"] = autoLevelUp;
    j["beginner_helpers"] = beginnerHelpers;
    j["external_editor"] = externalEditor;
    j["use_external_editor"] = useExternalEditor;
    j["check_updates"] = checkUpdates;
    j["beta_updates"] = betaUpdates;
    j["skipped_update"] = skippedUpdate;
    j["last_version"] = lastVersion;
    Json counts = Json::object();
    for (auto& [k, v] : counters)
        counts[k] = v;
    j["counters"] = counts;
    Json tips = Json::array();
    for (auto& t : seenTips)
        tips.push(t);
    j["seen_tips"] = tips;
    Json quests = Json::object();
    for (auto& [k, v] : questSteps)
        quests[k] = v;
    j["quest_steps"] = quests;
    j["profile_name"] = profileName;
    j["profile_color"] = colorJson(profileColor);
    j["avatar"] = avatar;
    j["profile_picture"] = profilePicture;
    j["pronouns"] = pronouns;
    j["found_via"] = foundVia;
    j["coding_level"] = codingLevel;
    Json engines = Json::array();
    for (auto& e : enginesUsed)
        engines.push(e);
    j["engines_used"] = engines;
    j["ladder_engine"] = ladderEngine;
    j["onboarded"] = onboarded;
    j["reduce_motion"] = reduceMotion;
    j["zoom_speed"] = zoomSpeed;
    j["invert_zoom"] = invertZoom;
    j["auto_close"] = autoClose;
    j["show_fps"] = showFps;
    j["show_greeting"] = showGreeting;
    j["open_last_project"] = openLastProject;
    Json keyJson = Json::object();
    for (auto& [k, v] : keys)
        keyJson[k] = static_cast<int>(v);
    j["keys"] = keyJson;
    j["keymap"] = keymap;
    return j;
}

void Prefs::fromJson(const Json& j) {
    Prefs d; // defaults
    theme = j["theme"].asString(d.theme);
    customAccent = j["custom_accent"].asBool(d.customAccent);
    accent = colorFrom(j["accent"], d.accent);
    uiScale = std::clamp(j["ui_scale"].asFloat(d.uiScale), 0.7f, 2.0f);
    fontSize = std::clamp(j["font_size"].asInt(d.fontSize), 11, 28);
    codeFontSize = std::clamp(j["code_font_size"].asInt(d.codeFontSize), 10, 30);
    codeTheme = j["code_theme"].asString(d.codeTheme);
    rounded = j["rounded"].asBool(d.rounded);
    compact = j["compact"].asBool(d.compact);
    colorblindSafe = j["colorblind_safe"].asBool(d.colorblindSafe);
    layout = j["layout"].asString(d.layout);
    savedLayouts.clear();
    for (auto& m : j["saved_layouts"].members())
        savedLayouts[m.key] = m.value.asString();
    gridColor = colorFrom(j["grid_color"], d.gridColor);
    gridOpacity = j["grid_opacity"].asFloat(d.gridOpacity);
    selectionColor = colorFrom(j["selection_color"], d.selectionColor);
    flySpeed = j["fly_speed"].asFloat(d.flySpeed);
    showHints = j["show_hints"].asBool(d.showHints);
    showIcons = j["show_icons"].asBool(d.showIcons);
    showColliders = j["show_colliders"].asBool(d.showColliders);
    showDebugDraw = j["show_debug_draw"].asBool(d.showDebugDraw);
    viewResolution = std::clamp(j["view_resolution"].asInt(d.viewResolution), 0, 2);
    viewportQuality = std::clamp(j["viewport_quality"].asInt(d.viewportQuality), 0, 3);
    previewParticles = j["preview_particles"].asBool(d.previewParticles);
    moveSnap = j["move_snap"].asFloat(d.moveSnap);
    rotateSnap = j["rotate_snap"].asFloat(d.rotateSnap);
    scaleSnap = j["scale_snap"].asFloat(d.scaleSnap);
    gizmoLocal = j["gizmo_local"].asBool(d.gizmoLocal);
    autosaveMinutes = j["autosave_minutes"].asInt(d.autosaveMinutes);
    clearConsoleOnPlay = j["clear_console_on_play"].asBool(d.clearConsoleOnPlay);
    pauseOnError = j["pause_on_error"].asBool(d.pauseOnError);
    confirmDelete = j["confirm_delete"].asBool(d.confirmDelete);
    pasteInPlace = j["paste_in_place"].asBool(d.pasteInPlace);
    uiSounds = j["ui_sounds"].asBool(d.uiSounds);
    recordReplays = j["record_replays"].asBool(d.recordReplays);
    saveEnergy = j["save_energy"].asBool(d.saveEnergy);
    level = std::clamp(j["level"].asInt(d.level), 1, 4);
    autoLevelUp = j["auto_level_up"].asBool(d.autoLevelUp);
    beginnerHelpers = j["beginner_helpers"].asBool(d.beginnerHelpers);
    externalEditor = j["external_editor"].asString("");
    useExternalEditor = j["use_external_editor"].asBool(d.useExternalEditor);
    checkUpdates = j["check_updates"].asBool(d.checkUpdates);
    betaUpdates = j["beta_updates"].asBool(d.betaUpdates);
    skippedUpdate = j["skipped_update"].asString("");
    lastVersion = j["last_version"].asString("");
    counters.clear();
    for (auto& m : j["counters"].members())
        counters[m.key] = m.value.asInt();
    seenTips.clear();
    for (auto& t : j["seen_tips"].elements())
        seenTips.insert(t.asString());
    questSteps.clear();
    for (auto& m : j["quest_steps"].members())
        questSteps[m.key] = m.value.asInt();
    profileName = j["profile_name"].asString(d.profileName);
    profileColor = colorFrom(j["profile_color"], d.profileColor);
    avatar = j["avatar"].asString(d.avatar);
    profilePicture = j["profile_picture"].asString("");
    pronouns = j["pronouns"].asString("");
    foundVia = j["found_via"].asString("");
    codingLevel = std::clamp(j["coding_level"].asInt(d.codingLevel), -1, 3);
    enginesUsed.clear();
    for (auto& e : j["engines_used"].elements())
        enginesUsed.push_back(e.asString());
    ladderEngine = j["ladder_engine"].asString("");
    // (Anyone who used Aven before the welcome tour existed has a preferences file already: no tour.)
    onboarded = j["onboarded"].asBool(true);
    reduceMotion = j["reduce_motion"].asBool(d.reduceMotion);
    zoomSpeed = std::clamp(j["zoom_speed"].asFloat(d.zoomSpeed), 0.25f, 4.0f);
    invertZoom = j["invert_zoom"].asBool(d.invertZoom);
    autoClose = j["auto_close"].asBool(d.autoClose);
    showFps = j["show_fps"].asBool(d.showFps);
    showGreeting = j["show_greeting"].asBool(d.showGreeting);
    openLastProject = j["open_last_project"].asBool(d.openLastProject);
    keys.clear();
    for (auto& m : j["keys"].members())
        keys[m.key] = static_cast<ImGuiKeyChord>(m.value.asInt());
    keymap = j["keymap"].asString(d.keymap);
}

bool Prefs::load() {
    auto text = fs::readText(prefsPath());
    if (!text)
        return false;
    fromJson(Json::parse(*text));
    return true;
}

void Prefs::save() const {
    if (saving)
        fs::writeText(prefsPath(), toJson().dump(2));
}

// ---------------------------------------------------------------- style

namespace {
bool gColorblindSafe = false;
}

Color axisColor(int axis, float alpha) {
    static const uint32_t normal[3] = {0xE25656, 0x70C050, 0x528EE8};
    static const uint32_t safe[3] = {0xE69F00, 0x56B4E9, 0xCC79A7};
    // (The index gets its own line: GCC 13 at -O0 mis-built std::clamp's temporaries inside the ?: here.)
    int i = axis < 0 ? 0 : axis > 2 ? 2 : axis;
    Color c = Color::fromHex(gColorblindSafe ? safe[i] : normal[i]);
    c.a = alpha;
    return c;
}

unsigned int axisColorU32(int axis) {
    Color c = axisColor(axis);
    return IM_COL32(static_cast<int>(c.r * 255), static_cast<int>(c.g * 255), static_cast<int>(c.b * 255), 255);
}

float Prefs::viewPixelScale(float screenScale) const {
    if (!(screenScale > 1.0f))
        return 1.0f;
    switch (viewResolution) {
    case 0: return screenScale;
    case 2: return 1.0f;
    default: return std::min(screenScale, 1.5f);
    }
}

void applyStyle(const Prefs& prefs, float dpiScale) {
    gColorblindSafe = prefs.colorblindSafe;
    // The move/rotate/scale handles use the same axis colors.
    ImGuizmo::Style& gizmo = ImGuizmo::GetStyle();
    for (int i = 0; i < 3; ++i) {
        Color c = axisColor(i);
        gizmo.Colors[ImGuizmo::DIRECTION_X + i] = {c.r, c.g, c.b, 1.0f};
        gizmo.Colors[ImGuizmo::PLANE_X + i] = {c.r, c.g, c.b, 0.38f};
    }
    const ThemePreset& t = prefs.themePreset();
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    float r = prefs.rounded ? 1.0f : 0.0f;
    st.WindowRounding = 6 * r;
    st.ChildRounding = 6 * r;
    st.FrameRounding = 5 * r;
    st.PopupRounding = 6 * r;
    st.ScrollbarRounding = 8 * r;
    st.GrabRounding = 5 * r;
    st.TabRounding = 5 * r;
    st.WindowBorderSize = 1;
    st.FrameBorderSize = t.light ? 1.0f : 0.0f;
    st.PopupBorderSize = 1;
    float pad = prefs.compact ? 0.7f : 1.0f;
    st.WindowPadding = {10 * pad, 10 * pad};
    st.FramePadding = {8 * pad, 5 * pad};
    st.ItemSpacing = {8 * pad, 6 * pad};
    st.ItemInnerSpacing = {6 * pad, 4 * pad};
    st.IndentSpacing = 16;
    st.ScrollbarSize = 13;
    st.GrabMinSize = 10;
    st.WindowTitleAlign = {0.0f, 0.5f};
    st.WindowMenuButtonPosition = ImGuiDir_None;
    st.SeparatorTextBorderSize = 1;
    st.DockingSeparatorSize = 3;

    ImVec4 bg = hex(t.background), panel = hex(t.panel), frame = hex(t.frame), hover = hex(t.frameHover);
    ImVec4 text = hex(t.text), dim = hex(t.textDim), border = hex(t.border);
    ImVec4 accent = toVec(prefs.accentColor(), 1.0f);
    ImVec4 white{1, 1, 1, 1}, black{0, 0, 0, 1};
    ImVec4 accentHover = lerp4(accent, t.light ? black : white, 0.2f);
    ImVec4 accentActive = lerp4(accent, t.light ? white : black, 0.2f);
    ImVec4 active = lerp4(hover, t.light ? black : white, 0.08f);

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = dim;
    c[ImGuiCol_WindowBg] = panel;
    c[ImGuiCol_ChildBg] = {0, 0, 0, 0};
    c[ImGuiCol_PopupBg] = lerp4(panel, t.light ? white : bg, 0.3f);
    c[ImGuiCol_PopupBg].w = 1.0f;
    c[ImGuiCol_Border] = border;
    c[ImGuiCol_BorderShadow] = {0, 0, 0, 0};
    c[ImGuiCol_FrameBg] = frame;
    c[ImGuiCol_FrameBgHovered] = hover;
    c[ImGuiCol_FrameBgActive] = active;
    c[ImGuiCol_TitleBg] = bg;
    c[ImGuiCol_TitleBgActive] = bg;
    c[ImGuiCol_TitleBgCollapsed] = bg;
    c[ImGuiCol_MenuBarBg] = bg;
    c[ImGuiCol_ScrollbarBg] = {0, 0, 0, 0};
    c[ImGuiCol_ScrollbarGrab] = lerp4(frame, text, 0.15f);
    c[ImGuiCol_ScrollbarGrabHovered] = lerp4(frame, text, 0.25f);
    c[ImGuiCol_ScrollbarGrabActive] = lerp4(frame, text, 0.35f);
    c[ImGuiCol_CheckMark] = accentHover;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accentHover;
    c[ImGuiCol_Button] = frame;
    c[ImGuiCol_ButtonHovered] = hover;
    c[ImGuiCol_ButtonActive] = active;
    c[ImGuiCol_Header] = lerp4(panel, accent, 0.35f);
    c[ImGuiCol_HeaderHovered] = lerp4(panel, accent, 0.5f);
    c[ImGuiCol_HeaderActive] = accentActive;
    c[ImGuiCol_Separator] = border;
    c[ImGuiCol_SeparatorHovered] = accent;
    c[ImGuiCol_SeparatorActive] = accentHover;
    c[ImGuiCol_ResizeGrip] = {0, 0, 0, 0};
    c[ImGuiCol_ResizeGripHovered] = accent;
    c[ImGuiCol_ResizeGripActive] = accentHover;
    c[ImGuiCol_Tab] = bg;
    c[ImGuiCol_TabHovered] = hover;
    c[ImGuiCol_TabSelected] = panel;
    c[ImGuiCol_TabSelectedOverline] = accent;
    c[ImGuiCol_TabDimmed] = bg;
    c[ImGuiCol_TabDimmedSelected] = panel;
    c[ImGuiCol_TabDimmedSelectedOverline] = {0, 0, 0, 0};
    c[ImGuiCol_DockingPreview] = {accent.x, accent.y, accent.z, 0.5f};
    c[ImGuiCol_DockingEmptyBg] = bg;
    c[ImGuiCol_TableHeaderBg] = frame;
    c[ImGuiCol_TableBorderStrong] = border;
    c[ImGuiCol_TableBorderLight] = frame;
    c[ImGuiCol_TableRowBgAlt] = t.light ? ImVec4(0, 0, 0, 0.03f) : ImVec4(1, 1, 1, 0.02f);
    c[ImGuiCol_TextSelectedBg] = {accent.x, accent.y, accent.z, 0.35f};
    c[ImGuiCol_DragDropTarget] = accentHover;
    c[ImGuiCol_NavHighlight] = accent;
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_PlotLines] = accent;
    c[ImGuiCol_ModalWindowDimBg] = {0, 0, 0, 0.55f};
    st.ScaleAllSizes(dpiScale * prefs.uiScale);
}

void buildFonts(const Prefs& prefs, float dpiScale, Fonts& fonts, float density) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    float scale = dpiScale * prefs.uiScale;
    auto add = [&](const char* name, float size) -> ImFont* {
        std::size_t bytes = 0;
        const unsigned char* data = embedded::find(name, &bytes);
        ImFontConfig cfg;
        cfg.RasterizerDensity = std::max(1.0f, density); // Retina: rasterized at 2x, same size on screen
        if (!data) {
            cfg.SizePixels = size;
            return io.Fonts->AddFontDefault(&cfg);
        }
        cfg.FontDataOwnedByAtlas = false; // the font lives in the executable
        cfg.OversampleH = 2;
        static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2022, 0x2022, 0x2190, 0x21FF, 0x2026, 0x2026, 0x2605, 0x2606, 0};
        return io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(data), static_cast<int>(bytes), size, &cfg, ranges);
    };
    float ui = static_cast<float>(prefs.fontSize) * scale;
    ui::setPixelScale(ui / 16.0f); // fixed sizes in the editor were laid out for 16 px text
    fonts.ui = add("Roboto-Medium.ttf", ui);
    fonts.bold = add("Roboto-Medium.ttf", ui * 1.12f);
    fonts.big = add("Roboto-Medium.ttf", ui * 1.75f);
    fonts.code = add("Cousine-Regular.ttf", static_cast<float>(prefs.codeFontSize) * scale);
    io.FontDefault = fonts.ui;
    io.Fonts->Build();
}

} // namespace aven::editor
