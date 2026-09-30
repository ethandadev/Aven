#pragma once

// Menus written once that show as ImGui's menu bar inside the window (Windows, Linux), or on a Mac
// as the real menu bar at the top of the screen, with the standard Rynax menu (About, Settings,
// Check for Updates, Hide, Quit). Used like ImGui's: the same calls, in menu::.
//
// On a Mac the menus are described every few frames; when something changed, the native menus
// follow: an item enabled or checked is changed in place, and only a menu whose items changed
// (a new scene in Open Scene) is made again. A click on a native item
// is remembered and returned by item() the next time that item is described. Shortcuts show in
// the native menus, but the keys still go to the editor, which knows when they apply (Cmd+C
// copies text in a text box, objects elsewhere).

#include <string>
#include <vector>

namespace rynax::editor::menu {

bool native(); // true once installNative() ran (macOS)
void installNative(); // macOS: take over the menu bar (menu_mac.mm). Elsewhere: nothing.

// Around all the menus. False: nothing to describe this frame, skip them (then don't call endBar).
bool beginBar();
void endBar();

bool begin(const char* label, bool enabled = true);
void end();
bool item(const char* label, const char* shortcut = nullptr, bool selected = false, bool enabled = true);
bool item(const char* label, const char* shortcut, bool* selected, bool enabled = true);
void separator();
void text(const char* label);    // a line that can't be clicked
void tooltip(const char* text);  // for the item just added
void beginDisabled(bool disabled);
void endDisabled();

// The standard Rynax menu on a Mac: "about", "settings" or "updates" when one was chosen since the
// last call (Quit, Hide and the rest are handled by macOS).
std::string appCommand();

namespace detail {
struct Node {
    enum Kind { Item, Menu, Separator, Text } kind = Item;
    std::string label, shortcut, tip, path;
    bool enabled = true, checked = false;
    std::vector<Node> children;
    bool operator==(const Node& o) const {
        return kind == o.kind && label == o.label && shortcut == o.shortcut && tip == o.tip && path == o.path &&
               enabled == o.enabled && checked == o.checked && children == o.children;
    }
};
extern bool nativeOn;
extern void (*apply)(const Node& bar); // builds the native menus (menu_mac.mm)
extern std::vector<std::string> clicks; // paths of native items clicked, oldest first
} // namespace detail

} // namespace rynax::editor::menu
