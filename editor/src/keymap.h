#pragma once

// Keyboard shortcuts: every command that has keys, where it works, and ready-made keymaps for
// people coming from Unity, Godot or Unreal. Any of them can be changed in Preferences > Shortcuts,
// and each command can have two sets of keys (Ctrl+Y and Ctrl+Shift+Z both redo).
//
// "Ctrl" here is Cmd on a Mac (ImGui swaps them there), so Ctrl+C is Cmd+C without anything
// special: the Mac's own Ctrl key is ImGuiMod_Super.

#include <imgui.h>

#include <string>
#include <string_view>
#include <vector>

namespace rynax::editor {

struct Prefs;

// Where a shortcut works. Two commands on the same keys clash when their places overlap.
enum KeyPlace : unsigned {
    InScene = 1,  // the editor, while not typing (scene view, Hierarchy, Assets...)
    InCode = 2,   // the code editor (and text boxes, for the ones that work while typing)
    InPixels = 4, // the Pixel Editor
    InFly = 8,    // flying the scene camera (holding the right mouse button)
    Anywhere = InScene | InCode | InPixels,
};

struct KeyAction {
    const char* id;
    const char* label;
    const char* group; // heading in Preferences > Shortcuts
    ImGuiKeyChord defaultChord;
    ImGuiKeyChord defaultAlt = 0; // a second set of keys that also works
    unsigned places = InScene;
    bool repeat = false; // held down, it happens again and again (undo, zoom)
};
const std::vector<KeyAction>& keyActions();
const KeyAction* findKeyAction(std::string_view id);

// A keymap: a starting set of shortcuts. Rynax's own is the defaults above.
struct Keymap {
    struct Change {
        const char* id;
        ImGuiKeyChord chord, alt;
    };
    const char* name;
    const char* blurb;
    std::vector<Change> changes; // from the defaults
};
const std::vector<Keymap>& keymaps();
// Starts over from that keymap (clearing the user's own changes).
void applyKeymap(Prefs& prefs, const std::string& name);
// An action's keys in a keymap, before the user changed anything.
ImGuiKeyChord keymapChord(const std::string& keymap, std::string_view id, int slot);
// Has the user changed any keys since picking their keymap?
bool keymapCustomized(const Prefs& prefs);

// The keys for an action: slot 0 is the main one, 1 the second. 0 = none. (prefs may be null: defaults)
ImGuiKeyChord boundChord(const Prefs* prefs, std::string_view id, int slot = 0);
// Were an action's keys pressed this frame? Doesn't check where the keyboard is: callers do.
bool keyPressed(const Prefs* prefs, const char* id);
// Are an action's keys held down (flying the camera)? Only the key counts, not Shift and friends.
bool keyHeld(const Prefs* prefs, const char* id);
// Other actions using these keys in the same places.
std::vector<const KeyAction*> keyClashes(const Prefs* prefs, std::string_view id, ImGuiKeyChord chord);
// Why these keys can't be used for this action, or "" if they're fine (a plain letter would type
// into the code editor instead, and Esc cancels choosing keys).
std::string chordProblem(const KeyAction& action, ImGuiKeyChord chord);
// The keys being pressed right now as a chord, or 0 while only modifiers are down.
ImGuiKeyChord pressedChord();

std::string chordName(ImGuiKeyChord chord);
// Both of an action's keys for a tooltip: "Ctrl+Y or Ctrl+Shift+Z".
std::string keysName(const Prefs* prefs, std::string_view id);
// Shortcuts in help text as this computer's keyboard names them: "Ctrl+Z" is "Cmd+Z" on a Mac.
std::string keyText(std::string text);

} // namespace rynax::editor
