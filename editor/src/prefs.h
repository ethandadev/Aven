#pragma once

#include "aven/core/json.h"
#include "aven/math/math.h"

#include <imgui.h>

#include <map>
#include <set>
#include <string>
#include <vector>

namespace aven::editor {

struct Fonts;

// A color scheme for the whole editor.
struct ThemePreset {
    const char* name;
    bool light;
    uint32_t background, panel, frame, frameHover, text, textDim, border, accent;
};
const std::vector<ThemePreset>& themePresets();

// A command that can have a keyboard shortcut.
struct KeyAction {
    const char* id;
    const char* label;
    ImGuiKeyChord defaultChord;
    bool whileTyping; // also works while a text field is focused (e.g. Save)
};
const std::vector<KeyAction>& keyActions();
std::string chordName(ImGuiKeyChord chord);

// The user's editor preferences: saved per user (not per project) in the user data folder.
struct Prefs {
    // Look and feel
    std::string theme = "Midnight";
    bool customAccent = false;
    Color accent = Color::fromHex(0x3B82F6);
    float uiScale = 1.0f;
    int fontSize = 16;
    int codeFontSize = 15;
    std::string codeTheme = "Aven Dark";
    bool rounded = true;
    bool compact = false;
    std::string layout = "Default";
    std::map<std::string, std::string> savedLayouts; // name -> ImGui .ini data

    // Scene view
    Color gridColor{1, 1, 1, 1};
    float gridOpacity = 1.0f;
    Color selectionColor = Color::fromHex(0xFF9E1A);
    float flySpeed = 6.0f;
    bool showHints = true;
    bool showIcons = true;
    bool showColliders = true;
    float moveSnap = 0.5f, rotateSnap = 15.0f, scaleSnap = 0.25f;
    bool gizmoLocal = false;

    // Behavior
    int autosaveMinutes = 3; // 0 = off
    bool clearConsoleOnPlay = true;
    bool pauseOnError = true;
    bool confirmDelete = false;
    bool uiSounds = false;
    bool recordReplays = true;

    // Learning
    int level = 1; // 1 Starter, 2 Explorer, 3 Creator, 4 Pro
    bool autoLevelUp = true;
    std::map<std::string, int> counters; // milestones: plays, objects added, scripts edited...
    std::set<std::string> seenTips;
    std::map<std::string, int> questSteps; // contributor quests progress

    // Profile (shown on game cards)
    std::string profileName;
    Color profileColor = Color::fromHex(0x8B5CF6);

    std::map<std::string, ImGuiKeyChord> keys; // action id -> chord (missing = default)

    ImGuiKeyChord chord(const std::string& action) const;
    Color accentColor() const;
    const ThemePreset& themePreset() const;

    void load();
    void save() const;
    Json toJson() const;
    void fromJson(const Json& j);
};

// Applies the theme, spacing and roundness to ImGui's style.
void applyStyle(const Prefs& prefs, float dpiScale);
// (Re)builds the font atlas. Call outside of a frame, then recreate the renderer's font texture.
void buildFonts(const Prefs& prefs, float dpiScale, Fonts& fonts);
// Blends two colors in 0-255 units, for small UI touches.
ImU32 mixColor(ImU32 a, ImU32 b, float t);

} // namespace aven::editor
