// The macOS menu bar for menu.h: the standard Rynax menu, then the editor's menus as described.

#include "menu.h"

#import <Cocoa/Cocoa.h>

#include <cctype>
#include <string>
#include <vector>

using rynax::editor::menu::detail::Node;

@interface RynaxMenuTarget : NSObject
- (void)clicked:(id)sender;
@end

@implementation RynaxMenuTarget
- (void)clicked:(id)sender {
    NSString* path = [sender representedObject];
    if (path)
        rynax::editor::menu::detail::clicks.push_back(std::string([path UTF8String]));
}
@end

// macOS's own items (Minimize, Zoom, Full Screen) in the editor's menus: these answer to their keys.
static const NSInteger kSystemItem = 7;

// The editor's menus show their shortcuts but let the keys through to the editor, which knows
// when they apply (Cmd+C copies text in a text box, objects in the scene).
@interface RynaxShowOnlyMenu : NSMenu
@end

@implementation RynaxShowOnlyMenu
- (BOOL)performKeyEquivalent:(NSEvent*)event {
    const NSEventModifierFlags relevant =
        NSEventModifierFlagCommand | NSEventModifierFlagShift | NSEventModifierFlagOption | NSEventModifierFlagControl;
    NSEventModifierFlags mods = event.modifierFlags & relevant;
    NSString* key = event.charactersIgnoringModifiers.lowercaseString;
    for (NSMenuItem* item in self.itemArray)
        if (item.tag == kSystemItem && item.keyEquivalent.length && [item.keyEquivalent isEqualToString:key] &&
            (item.keyEquivalentModifierMask & relevant) == mods) {
            [NSApp sendAction:item.action to:nil from:item];
            return YES;
        }
    return NO;
}
@end

namespace {

RynaxMenuTarget* target = nil;

NSString* str(const std::string& s) {
    NSString* out = [NSString stringWithUTF8String:s.c_str()];
    return out ? out : @"";
}

// "Cmd+Shift+S", "Delete", "F2" (from chordName) as a key equivalent and modifiers.
void setShortcut(NSMenuItem* item, const std::string& shortcut) {
    if (shortcut.empty() || shortcut == "(none)")
        return;
    NSEventModifierFlags mods = 0;
    std::string key = shortcut;
    for (size_t plus; key.size() > 1 && (plus = key.find('+', 1)) != std::string::npos;) {
        std::string mod = key.substr(0, plus);
        key = key.substr(plus + 1);
        if (mod == "Cmd") mods |= NSEventModifierFlagCommand;
        else if (mod == "Shift") mods |= NSEventModifierFlagShift;
        else if (mod == "Option" || mod == "Alt") mods |= NSEventModifierFlagOption;
        else if (mod == "Ctrl") mods |= NSEventModifierFlagControl;
    }
    NSString* equivalent = nil;
    unichar c = 0;
    if (key.size() == 1) {
        equivalent = [str(key) lowercaseString];
    } else if (key.size() >= 2 && key.size() <= 3 && key[0] == 'F' && isdigit(static_cast<unsigned char>(key[1]))) {
        c = static_cast<unichar>(NSF1FunctionKey + std::stoi(key.substr(1)) - 1);
    } else {
        static const struct { const char* name; unichar c; } named[] = {
            {"Delete", NSDeleteFunctionKey}, {"Backspace", NSBackspaceCharacter}, {"Escape", 0x1b}, {"Enter", '\r'},
            {"Tab", '\t'}, {"Space", ' '}, {"UpArrow", NSUpArrowFunctionKey}, {"DownArrow", NSDownArrowFunctionKey},
            {"LeftArrow", NSLeftArrowFunctionKey}, {"RightArrow", NSRightArrowFunctionKey}, {"Home", NSHomeFunctionKey},
            {"End", NSEndFunctionKey}, {"Comma", ','}, {"Period", '.'}, {"Slash", '/'}, {"Minus", '-'}, {"Equal", '='},
            {"Semicolon", ';'}, {"Apostrophe", '\''}, {"LeftBracket", '['}, {"RightBracket", ']'}, {"Backslash", '\\'},
            {"GraveAccent", '`'},
        };
        for (auto& n : named)
            if (key == n.name)
                c = n.c;
    }
    if (c)
        equivalent = [NSString stringWithCharacters:&c length:1];
    if (!equivalent)
        return;
    item.keyEquivalent = equivalent;
    item.keyEquivalentModifierMask = mods;
}

// The native items made for a Node, in the same shape (nil for separators), so a later
// description can be applied to them in place.
struct Built {
    NSMenuItem* item = nil;
    std::vector<Built> children;
};

NSMenuItem* makeItem(const Node& n, Built& built) {
    if (n.kind == Node::Separator)
        return [NSMenuItem separatorItem];
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:str(n.label) action:nil keyEquivalent:@""];
    built.item = item;
    item.enabled = n.enabled;
    if (!n.tip.empty())
        item.toolTip = str(n.tip);
    if (n.kind == Node::Menu) {
        RynaxShowOnlyMenu* sub = [[RynaxShowOnlyMenu alloc] initWithTitle:str(n.label)];
        sub.autoenablesItems = NO;
        built.children.resize(n.children.size());
        bool lastSeparator = true; // no separators at the top, bottom, or two in a row
        for (size_t i = 0; i < n.children.size(); ++i) {
            const Node& c = n.children[i];
            if (c.kind == Node::Separator && lastSeparator)
                continue;
            [sub addItem:makeItem(c, built.children[i])];
            lastSeparator = c.kind == Node::Separator;
        }
        if (lastSeparator && sub.numberOfItems > 0)
            [sub removeItemAtIndex:sub.numberOfItems - 1];
        item.submenu = sub;
        return item;
    }
    if (n.kind == Node::Item) {
        item.target = target;
        item.action = @selector(clicked:);
        item.representedObject = str(n.path);
        item.state = n.checked ? NSControlStateValueOn : NSControlStateValueOff;
        setShortcut(item, n.shortcut);
    }
    return item;
}

// Same items in the same places (what's greyed out, checked or explained may differ).
bool sameShape(const Node& a, const Node& b) {
    if (a.kind != b.kind || a.label != b.label || a.shortcut != b.shortcut || a.path != b.path ||
        a.children.size() != b.children.size())
        return false;
    for (size_t i = 0; i < a.children.size(); ++i)
        if (!sameShape(a.children[i], b.children[i]))
            return false;
    return true;
}

// Changes only what changed, on the items already in the menu bar (cheap: no new menus).
void update(const Node& now, const Node& was, Built& built) {
    if (NSMenuItem* item = built.item) {
        if (now.enabled != was.enabled)
            item.enabled = now.enabled;
        if (now.checked != was.checked)
            item.state = now.checked ? NSControlStateValueOn : NSControlStateValueOff;
        if (now.tip != was.tip)
            item.toolTip = now.tip.empty() ? nil : str(now.tip);
    }
    for (size_t i = 0; i < now.children.size() && i < built.children.size(); ++i)
        update(now.children[i], was.children[i], built.children[i]);
}

NSMenuItem* appItem(NSString* title, NSString* command, NSString* key) {
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:@selector(clicked:) keyEquivalent:key];
    item.target = target;
    item.representedObject = [@"@app/" stringByAppendingString:command];
    return item;
}

// The menu bar: the Rynax menu (made once), then the editor's menus as last applied.
NSMenu* mainMenu = nil;
std::vector<Node> shownMenus;
std::vector<Built> builtMenus;

void installMainMenu() {
    NSString* name = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleName"];
    if (!name.length)
        name = @"Rynax";
    mainMenu = [[NSMenu alloc] init];

    // The standard application menu. Its keys work as usual (Cmd+Q, Cmd+H, Cmd+,).
    NSMenu* app = [[NSMenu alloc] initWithTitle:name];
    [app addItem:appItem([@"About " stringByAppendingString:name], @"about", @"")];
    [app addItem:appItem(@"Check for Updates…", @"updates", @"")];
    [app addItem:[NSMenuItem separatorItem]];
    [app addItem:appItem(@"Settings…", @"settings", @",")];
    [app addItem:[NSMenuItem separatorItem]];
    NSMenuItem* services = [[NSMenuItem alloc] initWithTitle:@"Services" action:nil keyEquivalent:@""];
    NSMenu* servicesMenu = [[NSMenu alloc] initWithTitle:@"Services"];
    services.submenu = servicesMenu;
    NSApp.servicesMenu = servicesMenu; // (once: macOS fills it in, which is slow)
    [app addItem:services];
    [app addItem:[NSMenuItem separatorItem]];
    [app addItemWithTitle:[@"Hide " stringByAppendingString:name] action:@selector(hide:) keyEquivalent:@"h"];
    NSMenuItem* others = [app addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
    others.keyEquivalentModifierMask = NSEventModifierFlagOption | NSEventModifierFlagCommand;
    [app addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];
    [app addItem:[NSMenuItem separatorItem]];
    // terminate: reaches GLFW as a close request, so unsaved work is asked about first.
    [app addItemWithTitle:[@"Quit " stringByAppendingString:name] action:@selector(terminate:) keyEquivalent:@"q"];
    NSMenuItem* appHolder = [[NSMenuItem alloc] initWithTitle:name action:nil keyEquivalent:@""];
    appHolder.submenu = app;
    [mainMenu addItem:appHolder];
    NSApp.mainMenu = mainMenu;
}

// One of the editor's menus, new, for the menu bar.
NSMenuItem* makeTop(const Node& n, Built& built) {
    NSMenuItem* top = makeItem(n, built);
    if (n.label == "Window") {
        // The standard window commands first, as in every Mac app; macOS lists the windows below.
        NSMenu* w = top.submenu;
        NSMenuItem* minimize = [[NSMenuItem alloc] initWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
        NSMenuItem* zoom = [[NSMenuItem alloc] initWithTitle:@"Zoom" action:@selector(performZoom:) keyEquivalent:@""];
        NSMenuItem* full = [[NSMenuItem alloc] initWithTitle:@"Enter Full Screen" action:@selector(toggleFullScreen:) keyEquivalent:@"f"];
        full.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagControl;
        NSArray* standard = @[minimize, zoom, full, [NSMenuItem separatorItem]];
        for (NSUInteger i = 0; i < standard.count; ++i) {
            [standard[i] setTag:kSystemItem];
            [w insertItem:standard[i] atIndex:static_cast<NSInteger>(i)];
        }
        NSApp.windowsMenu = w;
    }
    return top;
}

// Brings the menu bar in line with a new description, touching as little as possible: replacing
// the whole menu bar makes macOS lay it out again and redo the Services and Window menus, which
// stalls the editor for a moment each time (selecting an object, say, changes what Cut can do).
void apply(const Node& bar) {
    @autoreleasepool {
        if (!mainMenu)
            installMainMenu();
        std::vector<Node> menus;
        for (const Node& n : bar.children)
            if (n.kind == Node::Menu)
                menus.push_back(n);
        bool sameMenus = menus.size() == shownMenus.size();
        for (size_t i = 0; sameMenus && i < menus.size(); ++i)
            sameMenus = menus[i].label == shownMenus[i].label;
        if (!sameMenus) { // a different set of menus (the start screen has none): all of them again
            while (mainMenu.numberOfItems > 1)
                [mainMenu removeItemAtIndex:mainMenu.numberOfItems - 1];
            builtMenus.assign(menus.size(), Built{});
            for (size_t i = 0; i < menus.size(); ++i)
                [mainMenu addItem:makeTop(menus[i], builtMenus[i])];
        } else {
            for (size_t i = 0; i < menus.size(); ++i) {
                if (sameShape(menus[i], shownMenus[i])) {
                    update(menus[i], shownMenus[i], builtMenus[i]);
                } else { // this menu's items changed (a new scene to open, a tool added): this one again
                    NSInteger at = static_cast<NSInteger>(i) + 1; // (after the Rynax menu)
                    builtMenus[i] = Built{};
                    NSMenuItem* top = makeTop(menus[i], builtMenus[i]);
                    [mainMenu removeItemAtIndex:at];
                    [mainMenu insertItem:top atIndex:at];
                }
            }
        }
        shownMenus = std::move(menus);
    }
}

} // namespace

namespace rynax::editor::menu {

void installNative() {
    if (!NSApp)
        return;
    // No "Start Dictation" and "Emoji & Symbols" in the Edit menu, nor a second "Enter Full Screen":
    // macOS adds those on its own, and they'd act on the editor's canvas rather than a text field.
    [[NSUserDefaults standardUserDefaults] registerDefaults:@{
        @"NSDisabledDictationMenuItem" : @YES,
        @"NSDisabledCharacterPaletteMenuItem" : @YES,
        @"NSFullScreenMenuItemEverywhere" : @NO,
    }];
    target = [[RynaxMenuTarget alloc] init];
    detail::nativeOn = true;
    detail::apply = apply;
    apply(Node{});
}

} // namespace rynax::editor::menu
