#pragma once

#include "rynax/scene/components.h"

#include <string>
#include <vector>

namespace rynax::editor {

// Ready-made particle looks: fire, smoke, sparkles, rain...
struct ParticlePreset {
    const char* name;
    const char* description;
    ParticleEmitter settings;
};

const std::vector<ParticlePreset>& particlePresets();
const ParticlePreset* findParticlePreset(const std::string& name); // case-insensitive, also matches "sparkle"

} // namespace rynax::editor
