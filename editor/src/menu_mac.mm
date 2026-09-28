// The macOS menu bar for menu.h: the standard Aven menu, then the editor's menus as described.

#include "menu.h"

#import <Cocoa/Cocoa.h>

#include <cctype>
#include <string>

using aven::editor::menu::detail::Node;

@interface AvenMenuTarget : NSObject
- (void)clicked:(id)sender;
@end

@implementation AvenMenuTarget
- (void)clicked:(id)sender {
    NSString* path = [sender representedObject];
    if (path)
        aven::editor::menu::detail::clicks.push_back(std::string([path UTF8String]));
}
@end

// The editor's menus show their shortcuts but let the keys through to the editor, which knows
// when they apply (Cmd+C copies text in a text box, objects in the scene).
@interface AvenShowOnlyMenu : NSMenu
@end

@implementation AvenShowOnlyMenu
- (BOOL)performKeyEquivalent:(NSEvent*)event {
    (void)event;
    return NO;
}
@end

namespace {

AvenMenuTarget* target = nil;

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

NSMenuItem* makeItem(const Node& n) {
    if (n.kind == Node::Separator)
        return [NSMenuItem separatorItem];
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:str(n.label) action:nil keyEquivalent:@""];
    item.enabled = n.enabled;
    if (!n.tip.empty())
        item.toolTip = str(n.tip);
    if (n.kind == Node::Menu) {
        AvenShowOnlyMenu* sub = [[AvenShowOnlyMenu alloc] initWithTitle:str(n.label)];
        sub.autoenablesItems = NO;
        bool lastSeparator = true; // no separators at the top, bottom, or two in a row
        for (const Node& c : n.children) {
            if (c.kind == Node::Separator && lastSeparator)
                continue;
            [sub addItem:makeItem(c)];
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

NSMenuItem* appItem(NSString* title, NSString* command, NSString* key) {
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:@selector(clicked:) keyEquivalent:key];
    item.target = target;
    item.representedObject = [@"@app/" stringByAppendingString:command];
    return item;
}

void build(const Node& bar) {
    @autoreleasepool {
        NSString* name = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleName"];
        if (!name.length)
            name = @"Aven";
        NSMenu* main = [[NSMenu alloc] init];

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
        NSApp.servicesMenu = servicesMenu;
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
        [main addItem:appHolder];

        for (const Node& n : bar.children)
            if (n.kind == Node::Menu)
                [main addItem:makeItem(n)];

        NSApp.mainMenu = main;
    }
}

} // namespace

namespace aven::editor::menu {

void installNative() {
    if (!NSApp)
        return;
    target = [[AvenMenuTarget alloc] init];
    detail::nativeOn = true;
    detail::apply = build;
    build(Node{});
}

} // namespace aven::editor::menu
