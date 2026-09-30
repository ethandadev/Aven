#pragma once

// Profile pictures: little critters drawn with shapes (no image files), for people who'd rather not
// use a photo. The onboarding and Preferences > Profile offer them, next to "your own picture".

#include <imgui.h>

#include <string_view>
#include <vector>

namespace rynax::editor {

struct Critter {
    const char* id;   // saved in the preferences ("cat")
    const char* name; // shown ("Cat")
};
const std::vector<Critter>& critters();

// Draws a critter filling the circle at `center`. `background` fills the circle behind it; `time`
// (seconds) makes it blink now and then, 0 keeps it still. Unknown ids draw the first critter.
void drawCritter(ImDrawList* dl, ImVec2 center, float radius, std::string_view id, ImU32 background, float time = 0);

} // namespace rynax::editor
