#pragma once

#include <string>
#include <vector>

namespace aven::editor {

struct Prefs;

// Learn mode: the editor starts small and reveals more as the user gets comfortable.
// Every feature belongs to a level; "Pro" shows everything.
enum class Feature {
    // Starter
    Recipes, Assistant, Explain, Doctor, Blocks, Behaviors, PixelEditor, Share, Learn,
    // Explorer
    Assets, Console, Physics, Audio, SoundMaker, Particles, UI, Prefabs, Tilemap, Animation, Lighting, ProjectSettings,
    Capture,
    // Creator
    Code, CodeLadder, Export, History, Find, BugReplay, CommandPalette, PostProcessing, MultiScene,
    // Pro
    Advanced, Profiler, Quests, NativeCode, Keybindings,
};

struct FeatureInfo {
    Feature feature;
    const char* name;
    const char* description;
    int level;
};

const std::vector<FeatureInfo>& featureList();
int featureLevel(Feature f);
const char* levelName(int level);
const char* levelBlurb(int level);
// Features that appear when going from `level - 1` to `level`.
std::vector<const FeatureInfo*> featuresUnlockedAt(int level);
// Which level the milestones in `prefs.counters` have earned (never lower than the current level).
int earnedLevel(const Prefs& prefs);
// What's still needed for the next level, in plain words (empty at Pro).
std::string nextLevelHint(const Prefs& prefs);

} // namespace aven::editor
