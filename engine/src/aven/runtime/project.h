#pragma once

#include "aven/core/json.h"

#include <filesystem>
#include <string>

namespace aven {

// Settings stored in <project>/project.aven.
struct ProjectSettings {
    static constexpr const char* kFileName = "project.aven";

    std::string name = "My Game";
    std::string version = "1.0";
    std::string description; // one or two sentences, shown on the game's web page and card
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

    bool load(const std::filesystem::path& projectDir, std::string* error = nullptr);
    bool save(const std::filesystem::path& projectDir) const;
    Json toJson() const;
    void fromJson(const Json& j);

    static bool isProject(const std::filesystem::path& dir);
};

} // namespace aven
