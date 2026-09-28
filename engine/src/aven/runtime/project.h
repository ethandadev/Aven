#pragma once

#include "aven/core/json.h"

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <utility>
#include <vector>
#include <string>

namespace aven {

// Settings stored in <project>/project.aven.
// A mixer channel: sounds play through one (Project Settings > Audio mixer).
struct AudioBus {
    std::string name;
    float volume = 1.0f;
    bool muted = false;
    float lowpass = 0.0f;   // cut sounds above this many Hz (muffled, underwater); 0 = off
    float echo = 0.0f;      // 0..1 how loud the echo is; 0 = off
    float echoDelay = 0.3f; // seconds between echoes
    bool operator==(const AudioBus&) const = default;
};

// On-screen controls for phones and tablets: a stick (the arrow keys) and buttons that press keys.
struct TouchButton {
    std::string label; // shown on the button
    std::string key;   // the key it presses: "space", "x", "e"...
    bool operator==(const TouchButton&) const = default;
};
enum class TouchMode : int32_t { Auto, Always, Off }; // Auto: once the screen is touched

struct TouchSettings {
    TouchMode mode = TouchMode::Auto;
    bool stick = true; // presses the arrow keys
    std::vector<TouchButton> buttons{{"A", "space"}};
    bool operator==(const TouchSettings&) const = default;
};

// How an exported game presents itself (Build & Share): who made it, its icon, what shows before it
// starts, and how the apps are signed. Nothing secret is kept here: signing passwords are asked for
// when exporting.
struct PublishSettings {
    std::string author;    // you or your studio, e.g. "Sam's Games"
    std::string website;
    std::string copyright; // empty: "(c) <year> <author>"
    std::string bundleId;  // reverse-domain id, e.g. "com.samsgames.obby" (macOS); empty: made from the names
    std::string icon;      // a picture in the project (square, 512 px or more is best); empty: the thumbnail
    bool splash = true;    // a moment of "Made with Aven" when the game starts (skippable)
    bool titleScreen = false; // a start menu with the game's name, Play and Quit
    std::string titleImage;   // the title screen's background; empty: the thumbnail
    // Signing. macOS: your Apple Developer Team ID, the signing identity (empty: the "Developer ID
    // Application" certificate for that team) and a notarytool keychain profile to notarize with.
    std::string appleTeamId;
    std::string appleIdentity;
    std::string notaryProfile;
    // Windows: a .pfx certificate file (or a certificate's SHA-1 thumbprint) and a timestamp server.
    std::string windowsCertificate;
    std::string timestampUrl = "http://timestamp.digicert.com";
    bool operator==(const PublishSettings&) const = default;
};

struct ProjectSettings {
    static constexpr const char* kFileName = "project.aven";

    std::string name = "My Game";
    std::string id; // random, made when the project is first opened: keeps games with the same name apart
    std::string version = "1.0";
    std::string description; // one or two sentences, shown on the game's web page and card
    std::string relay;       // online multiplayer: the relay server ("relay.example.com:4243"), if any
    std::string startScene = "scenes/main.scene";
    int width = 1280;
    int height = 720;
    bool resizable = true;
    bool fullscreen = false;
    bool vsync = true;
    bool pixelPerfect = false; // snap 2D rendering to whole pixels
    bool advancedMode = false; // editor shows advanced components and settings
    std::string templateName;  // which starter template the project came from
    Json inputActions = Json::object();
    TouchSettings touch;
    PublishSettings publish;

    // The folder name for this game's save data: its name plus the start of its id.
    std::string saveFolderName() const;

    // Collision layers: layer 0 is "Default"; `layers` names the others (up to 15).
    // Objects on two layers listed together in `layerIgnores` pass through each other.
    // Audio buses; "Music" and "Effects" always exist (music and play_sound() use them by default).
    std::vector<AudioBus> audioBuses = defaultAudioBuses();
    static std::vector<AudioBus> defaultAudioBuses();

    static constexpr int kMaxLayers = 16;
    std::vector<std::string> layers;
    std::vector<std::pair<std::string, std::string>> layerIgnores;
    std::vector<std::string> layerNames() const;          // "Default" first
    int layerIndex(std::string_view name) const;          // 0 for "", "Default" or an unknown name
    uint32_t collisionMask(int layer) const;              // bit i set = collides with layer i
    bool layersCollide(const std::string& a, const std::string& b) const;
    void setLayersCollide(const std::string& a, const std::string& b, bool collide);

    bool load(const std::filesystem::path& projectDir, std::string* error = nullptr);
    bool save(const std::filesystem::path& projectDir) const;
    Json toJson() const;
    void fromJson(const Json& j);

    static bool isProject(const std::filesystem::path& dir);
};

} // namespace aven
