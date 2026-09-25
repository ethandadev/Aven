#include "learn_mode.h"

#include "prefs.h"

#include <algorithm>

namespace aven::editor {

const std::vector<FeatureInfo>& featureList() {
    static const std::vector<FeatureInfo> list = {
        {Feature::Recipes, "Game recipes", "Tell Aven what game you want and get a small working version, explained step by step.", 1},
        {Feature::Assistant, "Ask Aven", "Describe a change in plain words, like \"make it faster\", and see the settings it changes.", 1},
        {Feature::Explain, "Explain", "Select anything to read what it does and how it connects to the rest of the game.", 1},
        {Feature::Doctor, "Error doctor", "Errors in plain English with a Fix button.", 1},
        {Feature::Blocks, "Block coding", "Snap blocks together to give objects behavior.", 1},
        {Feature::Behaviors, "Ready-made behaviors", "Patrol, chase, collect, spin... add them like any component.", 1},
        {Feature::PixelEditor, "Pixel art editor", "Draw your own sprites and animations.", 1},
        {Feature::Share, "Share", "Make a game card and a playable build to show your friends.", 1},
        {Feature::Learn, "Learn panel", "Step-by-step lessons for the current project.", 1},

        {Feature::Assets, "Assets panel", "Every file in your project: images, sounds, scripts, scenes.", 2},
        {Feature::Console, "Console", "Messages from print() and errors.", 2},
        {Feature::Physics, "Physics", "Gravity, collisions and bouncing (RigidBody and Collider components).", 2},
        {Feature::Audio, "Sounds and music", "AudioSource components and sound settings.", 2},
        {Feature::SoundMaker, "Sound effect maker", "Create retro sound effects: coins, jumps, lasers...", 2},
        {Feature::Particles, "Particles", "Fire, smoke, sparkles and explosions, with presets.", 2},
        {Feature::UI, "Screen UI", "Scores, menus and buttons pinned to the screen.", 2},
        {Feature::Prefabs, "Prefabs", "Save an object and create copies of it while the game runs.", 2},
        {Feature::Tilemap, "Tilemap painter", "Paint levels with tiles.", 2},
        {Feature::Animation, "Sprite animation", "Slice sprite sheets and play frame animations.", 2},
        {Feature::Lighting, "Lighting", "Lights, sky, fog and lighting presets for 3D.", 2},
        {Feature::ProjectSettings, "Project settings", "Game name, window size and controls.", 2},
        {Feature::Capture, "Screenshots and GIFs", "Capture your game to share it.", 2},

        {Feature::Code, "EasyScript code", "Type code in EasyScript, a friendly Python-like language.", 3},
        {Feature::CodeLadder, "Code ladder", "See your script as C#, C++, GDScript or Luau for other engines.", 3},
        {Feature::Export, "Build & export", "Make a version of your game that runs without Aven.", 3},
        {Feature::History, "Undo history", "Jump back to any earlier change.", 3},
        {Feature::Find, "Find in project", "Search every script and scene.", 3},
        {Feature::BugReplay, "Bug replay", "Watch the last 20 seconds before an error.", 3},
        {Feature::CommandPalette, "Command palette", "Ctrl+K: search every command, object and file.", 3},
        {Feature::PostProcessing, "Post-processing", "Bloom, color grading, vignette and SSAO.", 3},
        {Feature::MultiScene, "More scenes", "Levels, menus and scene switching.", 3},

        {Feature::Advanced, "Advanced settings", "Every setting of every component.", 4},
        {Feature::Profiler, "Profiler", "See where each frame's time goes.", 4},
        {Feature::Quests, "Contributor quests", "Small guided tasks to help improve Aven itself.", 4},
        {Feature::NativeCode, "Native C/C++ code", "Write performance-critical parts in C or C++.", 4},
        {Feature::Keybindings, "Custom shortcuts", "Change any keyboard shortcut.", 4},
    };
    return list;
}

int featureLevel(Feature f) {
    for (auto& info : featureList())
        if (info.feature == f)
            return info.level;
    return 1;
}

const char* levelName(int level) {
    switch (level) {
    case 1: return "Starter";
    case 2: return "Explorer";
    case 3: return "Creator";
    default: return "Pro";
    }
}

const char* levelBlurb(int level) {
    switch (level) {
    case 1: return "Just the essentials: the scene, your objects, blocks, recipes and helpers.";
    case 2: return "Adds assets, the console, physics, sounds, particles, UI, prefabs and tilemaps.";
    case 3: return "Adds code, the code ladder, exporting, history, find, bug replay and post-processing.";
    default: return "Everything: advanced settings, the profiler, native code and contributor quests.";
    }
}

std::vector<const FeatureInfo*> featuresUnlockedAt(int level) {
    std::vector<const FeatureInfo*> out;
    for (auto& info : featureList())
        if (info.level == level)
            out.push_back(&info);
    return out;
}

namespace {
int count(const Prefs& p, const char* key) {
    auto it = p.counters.find(key);
    return it == p.counters.end() ? 0 : it->second;
}
} // namespace

int earnedLevel(const Prefs& p) {
    int plays = count(p, "plays"), objects = count(p, "objects_added"), changes = count(p, "settings_changed");
    int blocks = count(p, "blocks_edited"), code = count(p, "code_saved"), exports = count(p, "exports");
    int recipes = count(p, "recipes");
    int level = 1;
    if (plays >= 2 && objects + changes / 3 + recipes * 3 >= 3)
        level = 2;
    if (level == 2 && plays >= 6 && (blocks >= 3 || code >= 1 || objects >= 15))
        level = 3;
    if (level == 3 && plays >= 15 && (code >= 3 || exports >= 1 || objects >= 40))
        level = 4;
    return std::max(level, p.level);
}

std::string nextLevelHint(const Prefs& p) {
    int plays = count(p, "plays");
    switch (p.level) {
    case 1: return plays < 2 ? "Press Play a couple of times and add a few objects." : "Add or change a few more objects.";
    case 2: return plays < 6 ? "Keep playing and testing your game." : "Edit some blocks or add more objects.";
    case 3: return plays < 15 ? "Keep building and testing." : "Write some EasyScript code or export your game.";
    default: return "";
    }
}

} // namespace aven::editor
