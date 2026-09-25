#include "aven/runtime/project.h"

#include "aven/core/fs.h"

namespace aven {

Json ProjectSettings::toJson() const {
    Json j = Json::object();
    j["aven"] = "project";
    j["name"] = name;
    j["version"] = version;
    j["start_scene"] = startScene;
    Json w = Json::object();
    w["width"] = width;
    w["height"] = height;
    w["resizable"] = resizable;
    w["fullscreen"] = fullscreen;
    w["vsync"] = vsync;
    w["pixel_perfect"] = pixelPerfect;
    j["window"] = std::move(w);
    j["advanced_mode"] = advancedMode;
    if (!templateName.empty())
        j["template"] = templateName;
    if (inputActions.size())
        j["input"] = inputActions;
    return j;
}

void ProjectSettings::fromJson(const Json& j) {
    name = j["name"].asString(name);
    version = j["version"].asString(version);
    startScene = j["start_scene"].asString(startScene);
    const Json& w = j["window"];
    width = w["width"].asInt(width);
    height = w["height"].asInt(height);
    resizable = w["resizable"].asBool(resizable);
    fullscreen = w["fullscreen"].asBool(fullscreen);
    vsync = w["vsync"].asBool(vsync);
    pixelPerfect = w["pixel_perfect"].asBool(pixelPerfect);
    advancedMode = j["advanced_mode"].asBool(advancedMode);
    templateName = j["template"].asString();
    inputActions = j["input"].isObject() ? j["input"] : Json::object();
}

bool ProjectSettings::load(const std::filesystem::path& dir, std::string* error) {
    auto text = fs::readText(dir / kFileName);
    if (!text) {
        if (error)
            *error = "No " + std::string(kFileName) + " file in " + dir.string();
        return false;
    }
    std::string parseError;
    Json j = Json::parse(*text, &parseError);
    if (!parseError.empty()) {
        if (error)
            *error = std::string(kFileName) + ": " + parseError;
        return false;
    }
    fromJson(j);
    return true;
}

bool ProjectSettings::save(const std::filesystem::path& dir) const {
    return fs::writeText(dir / kFileName, toJson().dump(2) + "\n");
}

bool ProjectSettings::isProject(const std::filesystem::path& dir) {
    return fs::exists(dir / kFileName);
}

} // namespace aven
