#include "menu.h"

#include <imgui.h>

#include <algorithm>

namespace rynax::editor::menu {

namespace detail {
bool nativeOn = false;
void (*apply)(const Node& bar) = nullptr;
std::vector<std::string> clicks;
} // namespace detail

namespace {

using detail::Node;

bool recording = false;
Node current, committed;
bool haveCommitted = false;
std::vector<Node*> stack;
int disabledDepth = 0;  // native: inside beginDisabled(true)
std::vector<bool> disabledStack;
double lastRecord = -1;

// ImGui labels can end in "##id"; menus show what's before it.
std::string shown(const char* label) {
    std::string s = label ? label : "";
    size_t hash = s.find("##");
    return hash == std::string::npos ? s : s.substr(0, hash);
}

Node& add(Node::Kind kind, const char* label) {
    Node* parent = stack.back();
    Node n;
    n.kind = kind;
    n.label = shown(label);
    n.path = parent->path + "/" + (label ? label : "");
    n.enabled = disabledDepth == 0;
    parent->children.push_back(std::move(n));
    return parent->children.back();
}

bool takeClick(const std::string& path) {
    auto it = std::find(detail::clicks.begin(), detail::clicks.end(), path);
    if (it == detail::clicks.end())
        return false;
    detail::clicks.erase(it);
    return true;
}

} // namespace

bool native() { return detail::nativeOn; }

#if !defined(__APPLE__)
void installNative() {}
#endif

bool beginBar() {
    if (!detail::nativeOn)
        return ImGui::BeginMenuBar();
    // Described a few times a second (enough for check marks and greyed-out items), and right
    // away when a native item was clicked, so its action runs.
    double now = ImGui::GetTime();
    bool clicked = std::any_of(detail::clicks.begin(), detail::clicks.end(), [](const std::string& c) { return c.rfind("@app/", 0) != 0; });
    if (haveCommitted && !clicked && now - lastRecord < 0.25)
        return false;
    lastRecord = now;
    recording = true;
    current = Node{};
    current.kind = Node::Menu;
    stack = {&current};
    disabledDepth = 0;
    disabledStack.clear();
    return true;
}

void endBar() {
    if (!detail::nativeOn) {
        ImGui::EndMenuBar();
        return;
    }
    if (!recording)
        return;
    recording = false;
    stack.clear();
    if (!haveCommitted || !(current == committed)) {
        committed = current;
        haveCommitted = true;
        if (detail::apply)
            detail::apply(committed);
    }
    // Clicks on items that weren't described (they went away) are dropped; the app menu's wait for appCommand().
    std::erase_if(detail::clicks, [](const std::string& c) { return c.rfind("@app/", 0) != 0; });
}

bool begin(const char* label, bool enabled) {
    if (!detail::nativeOn)
        return ImGui::BeginMenu(label, enabled);
    Node& n = add(Node::Menu, label);
    n.enabled = n.enabled && enabled;
    stack.push_back(&n);
    return true; // described even when it can't open, so end() pairs up
}

void end() {
    if (!detail::nativeOn) {
        ImGui::EndMenu();
        return;
    }
    if (stack.size() > 1)
        stack.pop_back();
}

bool item(const char* label, const char* shortcut, bool selected, bool enabled) {
    if (!detail::nativeOn)
        return ImGui::MenuItem(label, shortcut, selected, enabled);
    Node& n = add(Node::Item, label);
    n.shortcut = shortcut ? shortcut : "";
    n.checked = selected;
    n.enabled = n.enabled && enabled;
    for (Node* p : stack)
        n.enabled = n.enabled && p->enabled;
    return takeClick(n.path) && n.enabled;
}

bool item(const char* label, const char* shortcut, bool* selected, bool enabled) {
    if (item(label, shortcut, selected && *selected, enabled)) {
        if (selected)
            *selected = !*selected;
        return true;
    }
    return false;
}

void separator() {
    if (!detail::nativeOn)
        ImGui::Separator();
    else
        add(Node::Separator, "");
}

void text(const char* label) {
    if (!detail::nativeOn)
        ImGui::TextDisabled("%s", label);
    else
        add(Node::Text, label).enabled = false;
}

void tooltip(const char* text) {
    if (!detail::nativeOn) {
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", text);
        return;
    }
    Node* parent = stack.empty() ? nullptr : stack.back();
    if (parent && !parent->children.empty())
        parent->children.back().tip = text;
}

void beginDisabled(bool disabled) {
    if (!detail::nativeOn) {
        ImGui::BeginDisabled(disabled);
        return;
    }
    disabledStack.push_back(disabled);
    disabledDepth += disabled ? 1 : 0;
}

void endDisabled() {
    if (!detail::nativeOn) {
        ImGui::EndDisabled();
        return;
    }
    if (!disabledStack.empty()) {
        disabledDepth -= disabledStack.back() ? 1 : 0;
        disabledStack.pop_back();
    }
}

std::string appCommand() {
    for (auto it = detail::clicks.begin(); it != detail::clicks.end(); ++it)
        if (it->rfind("@app/", 0) == 0) {
            std::string id = it->substr(5);
            detail::clicks.erase(it);
            return id;
        }
    return "";
}

} // namespace rynax::editor::menu
