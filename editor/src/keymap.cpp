#include "keymap.h"

#include "prefs.h"

#include <imgui_internal.h>

#include <cstring>

namespace rynax::editor {

namespace {

constexpr ImGuiKeyChord Ctrl = ImGuiMod_Ctrl, Shift = ImGuiMod_Shift, Alt = ImGuiMod_Alt;

#if defined(__APPLE__)
constexpr bool kMac = true;
#else
constexpr bool kMac = false;
#endif

// Where the second set of keys lives in Prefs::keys.
std::string altKey(std::string_view id) { return std::string(id) + "/2"; }

} // namespace

const std::vector<KeyAction>& keyActions() {
    // id, label, group, keys, second keys, places, repeats. The Mac's own habits where they differ:
    // Cmd+Shift+Z redoes, Cmd+Backspace deletes, and Cmd+H hides the app (so no Cmd+H replace).
    static const std::vector<KeyAction> list = {
        {"new_scene", "New scene", "File", Ctrl | ImGuiKey_N, 0, InScene},
        {"open_scene", "Open a scene or script (quick open)", "File", Ctrl | ImGuiKey_O, 0, InScene | InCode},
        {"save", "Save", "File", Ctrl | ImGuiKey_S, 0, Anywhere},
        {"save_as", "Save scene as...", "File", Ctrl | Shift | ImGuiKey_S, 0, InScene | InCode},
        {"close_tab", "Close the tab or window", "File", Ctrl | ImGuiKey_W, 0, Anywhere},
        {"preferences", "Preferences", "File", Ctrl | ImGuiKey_Comma, 0, Anywhere},
        {"quit", "Quit Rynax", "File", Ctrl | ImGuiKey_Q, 0, Anywhere},

        {"undo", "Undo", "Edit", Ctrl | ImGuiKey_Z, 0, Anywhere, true},
        {"redo", "Redo", "Edit", kMac ? Ctrl | Shift | ImGuiKey_Z : Ctrl | ImGuiKey_Y,
         kMac ? Ctrl | ImGuiKey_Y : Ctrl | Shift | ImGuiKey_Z, Anywhere, true},
        {"cut", "Cut", "Edit", Ctrl | ImGuiKey_X, 0, InScene | InCode},
        {"copy", "Copy", "Edit", Ctrl | ImGuiKey_C, 0, InScene | InCode},
        {"paste", "Paste", "Edit", Ctrl | ImGuiKey_V, 0, InScene | InCode},
        {"paste_in_place", "Paste in the same place", "Edit", Ctrl | Shift | ImGuiKey_V, 0, InScene},
        {"duplicate", "Duplicate", "Edit", Ctrl | ImGuiKey_D, 0, InScene},
        {"delete", "Delete", "Edit", ImGuiKey_Delete, kMac ? Ctrl | ImGuiKey_Backspace : 0, InScene},
        {"select_all", "Select all", "Edit", Ctrl | ImGuiKey_A, 0, InScene | InCode},
        {"rename", "Rename", "Edit", ImGuiKey_F2, 0, InScene},
        {"group", "Group into a folder", "Edit", Ctrl | ImGuiKey_G, 0, InScene},

        {"play", "Play / Stop", "Play", Ctrl | ImGuiKey_P, 0, Anywhere},
        {"stop", "Stop playing", "Play", Shift | ImGuiKey_F5, 0, Anywhere},
        {"pause", "Pause / Resume", "Play", Ctrl | Shift | ImGuiKey_P, 0, Anywhere},
        {"step", "Next frame (while paused)", "Play", Ctrl | Alt | ImGuiKey_P, 0, Anywhere},
        {"screenshot", "Screenshot of the game view", "Play", ImGuiKey_F12, 0, InScene | InPixels},
        {"record_gif", "Record a GIF of the game", "Play", Shift | ImGuiKey_F12, 0, InScene | InPixels},

        {"debug_continue", "Continue", "Debugger (stopped at a breakpoint)", ImGuiKey_F5, 0, InScene | InCode},
        {"debug_over", "Step over (next line)", "Debugger (stopped at a breakpoint)", ImGuiKey_F10, 0, InScene | InCode},
        {"debug_into", "Step into (the function)", "Debugger (stopped at a breakpoint)", ImGuiKey_F11, 0, InScene | InCode},
        {"debug_out", "Step out (of the function)", "Debugger (stopped at a breakpoint)", Shift | ImGuiKey_F11, 0,
         InScene | InCode},
        {"code_breakpoint", "Breakpoint on / off (in the code editor)", "Debugger (stopped at a breakpoint)", ImGuiKey_F9, 0,
         InCode},

        {"tool_move", "Move tool", "Scene view", ImGuiKey_W, 0, InScene},
        {"tool_rotate", "Rotate tool", "Scene view", ImGuiKey_E, 0, InScene},
        {"tool_scale", "Scale tool", "Scene view", ImGuiKey_R, 0, InScene},
        {"focus", "Focus on the selection", "Scene view", ImGuiKey_F, 0, InScene},
        {"toggle_2d3d", "Switch 2D / 3D view", "Scene view", ImGuiKey_F4, 0, InScene},
        {"fly_forward", "Fly forward (hold the right mouse button)", "Scene view", ImGuiKey_W, 0, InFly},
        {"fly_back", "Fly back", "Scene view", ImGuiKey_S, 0, InFly},
        {"fly_left", "Fly left", "Scene view", ImGuiKey_A, 0, InFly},
        {"fly_right", "Fly right", "Scene view", ImGuiKey_D, 0, InFly},
        {"fly_up", "Fly up", "Scene view", ImGuiKey_E, 0, InFly},
        {"fly_down", "Fly down", "Scene view", ImGuiKey_Q, 0, InFly},

        {"command_palette", "Command palette (search everything)", "Windows and tools", Ctrl | ImGuiKey_K, 0, Anywhere},
        {"ask", "Ask Rynax (describe a change)", "Windows and tools", Ctrl | ImGuiKey_J, 0, Anywhere},
        {"find", "Find in project", "Windows and tools", Ctrl | Shift | ImGuiKey_F, 0, Anywhere},
        {"explain", "Explain the selected object", "Windows and tools", ImGuiKey_F1, 0, InScene},
        {"doctor", "Error Doctor (check my game)", "Windows and tools", ImGuiKey_F8, 0, Anywhere},
        {"build_native", "Build native (C/C++) code", "Windows and tools", Ctrl | ImGuiKey_B, 0, Anywhere},
        {"ui_bigger", "Make the editor bigger", "Windows and tools", Ctrl | ImGuiKey_Equal, Ctrl | ImGuiKey_KeypadAdd, InScene},
        {"ui_smaller", "Make the editor smaller", "Windows and tools", Ctrl | ImGuiKey_Minus, Ctrl | ImGuiKey_KeypadSubtract,
         InScene},
        {"ui_reset", "Editor size back to normal", "Windows and tools", Ctrl | ImGuiKey_0, 0, InScene},
        {"assets_up", "Assets: up a folder", "Windows and tools", ImGuiKey_Backspace, 0, InScene},
        {"sound_play", "Sound Maker: play the sound", "Windows and tools", ImGuiKey_Space, 0, InScene},
        {"tile_prev", "Tile Painter: previous tile", "Windows and tools", ImGuiKey_LeftBracket, 0, InScene},
        {"tile_next", "Tile Painter: next tile", "Windows and tools", ImGuiKey_RightBracket, 0, InScene},

        {"code_suggest", "Suggestions", "Code editor", Ctrl | ImGuiKey_Space, 0, InCode},
        {"code_find", "Find", "Code editor", Ctrl | ImGuiKey_F, 0, InCode},
        {"code_replace", "Replace", "Code editor", kMac ? Ctrl | Alt | ImGuiKey_F : Ctrl | ImGuiKey_H,
         kMac ? 0 : Ctrl | Alt | ImGuiKey_F, InCode},
        {"code_find_next", "Find next", "Code editor", ImGuiKey_F3, 0, InCode, true},
        {"code_find_previous", "Find previous", "Code editor", Shift | ImGuiKey_F3, 0, InCode, true},
        {"code_go_to_line", "Go to line", "Code editor", Ctrl | ImGuiKey_G, 0, InCode},
        {"code_definition", "Go to definition", "Code editor", ImGuiKey_F12, 0, InCode},
        {"code_comment", "Comment / uncomment lines", "Code editor", Ctrl | ImGuiKey_Slash, 0, InCode},
        {"code_select_line", "Select the line", "Code editor", Ctrl | ImGuiKey_L, 0, InCode, true},
        {"code_move_up", "Move lines up", "Code editor", Alt | ImGuiKey_UpArrow, 0, InCode, true},
        {"code_move_down", "Move lines down", "Code editor", Alt | ImGuiKey_DownArrow, 0, InCode, true},
        {"code_copy_up", "Copy lines up", "Code editor", Shift | Alt | ImGuiKey_UpArrow, 0, InCode, true},
        {"code_copy_down", "Copy lines down", "Code editor", Shift | Alt | ImGuiKey_DownArrow, 0, InCode, true},
        {"code_delete_line", "Delete lines", "Code editor", Ctrl | Shift | ImGuiKey_K, 0, InCode, true},
        {"code_line_below", "New line below", "Code editor", Ctrl | ImGuiKey_Enter, Ctrl | ImGuiKey_KeypadEnter, InCode, true},
        {"code_line_above", "New line above", "Code editor", Ctrl | Shift | ImGuiKey_Enter, 0, InCode, true},
        {"code_zoom_in", "Bigger text", "Code editor", Ctrl | ImGuiKey_Equal, Ctrl | ImGuiKey_KeypadAdd, InCode, true},
        {"code_zoom_out", "Smaller text", "Code editor", Ctrl | ImGuiKey_Minus, Ctrl | ImGuiKey_KeypadSubtract, InCode, true},
        {"code_zoom_reset", "Text size back to normal", "Code editor", Ctrl | ImGuiKey_0, 0, InCode},

        {"pixel_pencil", "Pencil", "Pixel Editor", ImGuiKey_B, 0, InPixels},
        {"pixel_eraser", "Eraser", "Pixel Editor", ImGuiKey_E, 0, InPixels},
        {"pixel_fill", "Fill", "Pixel Editor", ImGuiKey_G, 0, InPixels},
        {"pixel_line", "Line", "Pixel Editor", ImGuiKey_L, 0, InPixels},
        {"pixel_rect", "Rectangle", "Pixel Editor", ImGuiKey_R, 0, InPixels},
        {"pixel_picker", "Color picker", "Pixel Editor", ImGuiKey_I, 0, InPixels},
        {"pixel_mirror", "Mirror drawing on / off", "Pixel Editor", ImGuiKey_M, 0, InPixels},
        {"pixel_prev_frame", "Previous frame", "Pixel Editor", ImGuiKey_LeftArrow, 0, InPixels},
        {"pixel_next_frame", "Next frame", "Pixel Editor", ImGuiKey_RightArrow, 0, InPixels},
    };
    return list;
}

const KeyAction* findKeyAction(std::string_view id) {
    for (auto& a : keyActions())
        if (id == a.id)
            return &a;
    return nullptr;
}

const std::vector<Keymap>& keymaps() {
    static const std::vector<Keymap> list = {
        {"Rynax", "Rynax's own keys: Ctrl+P plays, W / E / R move, rotate and scale, F focuses.", {}},
        {"Unity", "Unity's keys, which Rynax's own already follow: Ctrl+P plays, Ctrl+Shift+P pauses, "
                  "W / E / R, F to focus, Ctrl+D duplicates.",
         {}},
        {"Godot",
         "Godot's keys: F5 plays (F6 too), F7 pauses, F8 stops, Ctrl+Shift+P for the command palette, "
         "Ctrl+K comments code, Ctrl+L goes to a line, F12 continues in the debugger.",
         {
             {"play", ImGuiKey_F5, ImGuiKey_F6},
             {"pause", ImGuiKey_F7, 0},
             {"stop", ImGuiKey_F8, 0},
             {"doctor", Ctrl | ImGuiKey_F8, 0},
             {"command_palette", Ctrl | Shift | ImGuiKey_P, 0},
             {"open_scene", Ctrl | ImGuiKey_O, Shift | Alt | ImGuiKey_O},
             {"toggle_2d3d", Ctrl | ImGuiKey_F1, Ctrl | ImGuiKey_F2},
             {"debug_continue", ImGuiKey_F12, 0},
             {"screenshot", Ctrl | ImGuiKey_F12, 0},
             {"record_gif", Ctrl | Shift | ImGuiKey_F12, 0},
             {"code_definition", 0, 0}, // (Ctrl+click still goes there)
             {"code_comment", Ctrl | ImGuiKey_K, Ctrl | ImGuiKey_Slash},
             {"code_go_to_line", Ctrl | ImGuiKey_L, 0},
             {"code_select_line", 0, 0},
             {"code_replace", Ctrl | ImGuiKey_R, kMac ? Ctrl | Alt | ImGuiKey_F : Ctrl | ImGuiKey_H},
         }},
        {"Unreal",
         "Unreal's keys: Alt+P plays, Pause pauses, Ctrl+P opens anything, Ctrl+Shift+S saves everything.",
         {
             {"play", Alt | ImGuiKey_P, 0},
             {"pause", ImGuiKey_Pause, 0},
             {"open_scene", Ctrl | ImGuiKey_P, Ctrl | ImGuiKey_O},
             {"save", Ctrl | ImGuiKey_S, Ctrl | Shift | ImGuiKey_S},
             {"save_as", Ctrl | Alt | ImGuiKey_S, 0},
         }},
    };
    return list;
}

void applyKeymap(Prefs& prefs, const std::string& name) {
    prefs.keys.clear();
    prefs.keymap = "Rynax";
    for (auto& k : keymaps()) {
        if (name != k.name)
            continue;
        prefs.keymap = k.name;
        for (auto& c : k.changes) {
            prefs.keys[c.id] = c.chord;
            prefs.keys[altKey(c.id)] = c.alt;
        }
    }
}

ImGuiKeyChord keymapChord(const std::string& keymap, std::string_view id, int slot) {
    for (auto& k : keymaps())
        if (keymap == k.name)
            for (auto& c : k.changes)
                if (id == c.id)
                    return slot == 0 ? c.chord : c.alt;
    return boundChord(nullptr, id, slot);
}

bool keymapCustomized(const Prefs& prefs) {
    for (auto& a : keyActions())
        for (int slot = 0; slot < 2; ++slot)
            if (boundChord(&prefs, a.id, slot) != keymapChord(prefs.keymap, a.id, slot))
                return true;
    return false;
}

ImGuiKeyChord boundChord(const Prefs* prefs, std::string_view id, int slot) {
    if (prefs) {
        auto it = prefs->keys.find(slot == 0 ? std::string(id) : altKey(id));
        if (it != prefs->keys.end())
            return it->second;
    }
    const KeyAction* a = findKeyAction(id);
    return !a ? 0 : slot == 0 ? a->defaultChord : a->defaultAlt;
}

bool keyPressed(const Prefs* prefs, const char* id) {
    const KeyAction* a = findKeyAction(id);
    if (!a)
        return false;
    ImGuiInputFlags flags = a->repeat ? ImGuiInputFlags_Repeat : ImGuiInputFlags_None;
    for (int slot = 0; slot < 2; ++slot)
        if (ImGuiKeyChord c = boundChord(prefs, id, slot); c && ImGui::IsKeyChordPressed(c, flags, ImGuiKeyOwner_Any))
            return true;
    return false;
}

bool keyHeld(const Prefs* prefs, const char* id) {
    for (int slot = 0; slot < 2; ++slot)
        if (ImGuiKeyChord c = boundChord(prefs, id, slot); c && ImGui::IsKeyDown(static_cast<ImGuiKey>(c & ~ImGuiMod_Mask_)))
            return true;
    return false;
}

std::vector<const KeyAction*> keyClashes(const Prefs* prefs, std::string_view id, ImGuiKeyChord chord) {
    std::vector<const KeyAction*> out;
    const KeyAction* me = findKeyAction(id);
    if (!me || !chord)
        return out;
    for (auto& a : keyActions()) {
        if (id == a.id || !(a.places & me->places))
            continue;
        if (boundChord(prefs, a.id, 0) == chord || boundChord(prefs, a.id, 1) == chord)
            out.push_back(&a);
    }
    return out;
}

std::string chordProblem(const KeyAction& action, ImGuiKeyChord chord) {
    auto key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
    if (key == ImGuiKey_Escape)
        return "Esc is kept for cancelling.";
    if (action.places & InFly)
        return (chord & ImGuiMod_Mask_) ? "Flying uses plain keys (hold Shift to fly faster)." : "";
    bool function = (key >= ImGuiKey_F1 && key <= ImGuiKey_F24) || key == ImGuiKey_Pause || key == ImGuiKey_PrintScreen ||
                    key == ImGuiKey_ScrollLock;
    bool held = chord & (ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiMod_Super);
    if ((action.places & InCode) && !function && !held)
        return keyText("This works while you type, so it needs Ctrl or Alt (or an F key): a plain key would type.");
    return "";
}

ImGuiKeyChord pressedChord() {
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        auto key = static_cast<ImGuiKey>(k);
        if (ImGui::IsLRModKey(key) || (key >= ImGuiKey_ReservedForModCtrl && key <= ImGuiKey_ReservedForModSuper) ||
            (key >= ImGuiKey_MouseLeft && key <= ImGuiKey_MouseWheelY) || (key >= ImGuiKey_GamepadStart && key <= ImGuiKey_GamepadRStickDown))
            continue;
        if (ImGui::IsKeyPressed(key, false))
            return key | ImGui::GetIO().KeyMods;
    }
    return 0;
}

std::string chordName(ImGuiKeyChord chord) {
    if (chord == 0)
        return "(none)";
#if defined(__APPLE__)
    // On a Mac, ImGui's Ctrl is the Cmd key (and Super is Ctrl): name them as the keyboard does.
    std::string name = (chord & ImGuiMod_Ctrl) ? "Cmd+" : "";
    name += (chord & ImGuiMod_Shift) ? "Shift+" : "";
    name += (chord & ImGuiMod_Alt) ? "Option+" : "";
    name += (chord & ImGuiMod_Super) ? "Ctrl+" : "";
    ImGuiKey key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
    return key == ImGuiKey_None ? name.substr(0, name.empty() ? 0 : name.size() - 1) : name + ImGui::GetKeyName(key);
#else
    return ImGui::GetKeyChordName(chord);
#endif
}

std::string keysName(const Prefs* prefs, std::string_view id) {
    ImGuiKeyChord a = boundChord(prefs, id, 0), b = boundChord(prefs, id, 1);
    if (a && b)
        return chordName(a) + " or " + chordName(b);
    return chordName(a ? a : b);
}

std::string keyText(std::string text) {
#if defined(__APPLE__)
    for (auto [from, to] : {std::pair<const char*, const char*>{"Ctrl", "Cmd"}, {"Alt+", "Option+"}})
        for (size_t at = 0; (at = text.find(from, at)) != std::string::npos; at += std::strlen(to))
            text.replace(at, std::strlen(from), to);
#endif
    return text;
}

} // namespace rynax::editor
