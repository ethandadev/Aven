#pragma once

#include "rynax/core/json.h"
#include "rynax/math/math.h"
#include "keymap.h"

#include <imgui.h>

#include <map>
#include <set>
#include <string>
#include <vector>

namespace rynax::editor {

struct Fonts;

// A color scheme for the whole editor.
struct ThemePreset {
    const char* name;
    bool light;
    uint32_t background, panel, frame, frameHover, text, textDim, border, accent;
};
const std::vector<ThemePreset>& themePresets();

// The user's editor preferences: saved per user (not per project) in the user data folder.
struct Prefs {
    // Look and feel
    std::string theme = "Midnight";
    bool customAccent = false;
    Color accent = Color::fromHex(0x3B82F6);
    float uiScale = 1.0f;
    int fontSize = 16;
    int codeFontSize = 15;
    std::string codeTheme = "Rynax Dark";
    bool rounded = true;
    bool compact = false;
    bool colorblindSafe = false; // axis colors that don't rely on telling red from green
    bool reduceMotion = false;   // calm mode: no bouncing, confetti or wiggling text
    std::string layout = "Default";
    std::map<std::string, std::string> savedLayouts; // name -> ImGui .ini data

    // Scene view
    Color gridColor{1, 1, 1, 1};
    float gridOpacity = 1.0f;
    Color selectionColor = Color::fromHex(0xFF9E1A);
    float flySpeed = 6.0f;
    float zoomSpeed = 1.0f;  // how far one notch of the mouse wheel zooms the scene view
    bool invertZoom = false; // wheel up zooms out
    bool showHints = true;
    bool showIcons = true;
    bool showColliders = true;
    bool showDebugDraw = true; // scripts' debug_line() shapes while playing
    bool previewParticles = true; // selected particle emitters play in the scene view
    // How many pixels the scene and game view draw on a high-density (Retina) screen: 0 all of them
    // (sharpest), 1 up to 1.5 per point (balanced), 2 one per point (fastest). The same on 1x screens.
    int viewResolution = 1;
    int viewportQuality = 2; // the scene view's graphics quality while editing: 0 Low ... 3 Ultra (games use their own)
    float viewPixelScale(float screenScale) const; // what that means on this screen
    float moveSnap = 0.5f, rotateSnap = 15.0f, scaleSnap = 0.25f;
    bool gizmoLocal = false;

    // Behavior
    int autosaveMinutes = 3; // 0 = off
    bool clearConsoleOnPlay = true;
    bool pauseOnError = true;
    bool confirmDelete = false;
    bool pasteInPlace = false; // pasted objects land exactly where the copies were, not nudged aside
    bool uiSounds = false;
    bool recordReplays = true;
    bool saveEnergy = true; // when nothing moves and nobody's touching anything, draw a few frames a second
    bool autoClose = true; // the code editor types the closing ) ] } and quotes for you
    bool showFps = true;   // frames per second in the status bar
    // Start screen
    bool showGreeting = true;     // "Good morning, Sam!" with their picture
    bool openLastProject = false; // start in the last game instead of the start screen
    // Opening scripts in another code editor: {file} and {line} are filled in, e.g. "code -g {file}:{line}".
    std::string externalEditor;
    bool useExternalEditor = false;

    // Updates (updater.cpp)
    bool checkUpdates = true;    // look for a new Rynax when the editor starts
    bool betaUpdates = false;    // offer beta versions too
    std::string skippedUpdate;   // "Skip this version"
    std::string lastVersion;     // the Rynax that last ran, to say "Updated to ..." once

    // Learning
    int level = 1; // 1 Starter, 2 Explorer, 3 Creator, 4 Pro
    bool autoLevelUp = true;
    bool beginnerHelpers = true; // Ask Rynax box, Doctor buttons, tips in empty panels
    std::map<std::string, int> counters; // milestones: plays, objects added, scripts edited...
    std::set<std::string> seenTips;
    std::map<std::string, int> questSteps; // contributor quests progress

    // Profile (the name is shown on game cards). Everything here stays on this computer.
    std::string profileName;
    Color profileColor = Color::fromHex(0x8B5CF6);
    std::string avatar = "cat";   // a critter (avatars.h), or "picture" for profilePicture
    std::string profilePicture;   // their own picture, copied into the user data folder
    std::string pronouns;         // "he/him", "she/her", "they/them", their own words, or "" (not said)
    std::string foundVia;         // how they found Rynax (asked in the welcome tour)
    int codingLevel = -1;         // 0 never coded, 1 a little, 2 some, 3 lots (-1: didn't say)
    std::vector<std::string> enginesUsed; // "Unity", "Godot", "Unreal"
    std::string ladderEngine;     // the Code Ladder opens on this engine's language ("" = Unity's C#)
    bool onboarded = false;       // finished (or skipped) the welcome tour

    // Shortcuts (keymap.h): action id -> keys, "id/2" -> its second keys (missing = the default, 0 = none).
    std::map<std::string, ImGuiKeyChord> keys;
    std::string keymap = "Rynax"; // the keymap they started from: Rynax, Unity, Godot or Unreal

    ImGuiKeyChord chord(const std::string& action) const { return boundChord(this, action, 0); }
    Color accentColor() const;
    const ThemePreset& themePreset() const;

    bool load(); // false when there's no preferences file yet (the first time Rynax runs)
    void save() const;
    static inline bool saving = true; // off for automated runs (--screenshot), which mustn't change the user's
    Json toJson() const;
    void fromJson(const Json& j);
};

// Rynax was called Aven before 0.6: the first time Rynax runs, it takes a copy of Aven's editor data
// (preferences, recent games, layout, profile picture...). Aven's folder stays, for going back.
// Returns true when it copied something.
bool bringOverAvenData();

// Applies the theme, spacing and roundness to ImGui's style.
void applyStyle(const Prefs& prefs, float dpiScale);
// X, Y and Z colors (move arrows, X/Y/Z fields, grid axes): red, green and blue, or with
// colorblindSafe orange, sky blue and pink (from the Okabe-Ito palette).
Color axisColor(int axis, float alpha = 1.0f);
unsigned int axisColorU32(int axis);
// (Re)builds the font atlas. Call outside of a frame, then recreate the renderer's font texture.
void buildFonts(const Prefs& prefs, float dpiScale, Fonts& fonts, float density = 1.0f);
// Blends two colors in 0-255 units, for small UI touches.
ImU32 mixColor(ImU32 a, ImU32 b, float t);

} // namespace rynax::editor
