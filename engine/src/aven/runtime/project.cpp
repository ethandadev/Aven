#include "aven/runtime/project.h"

#include "aven/core/fs.h"

#include <algorithm>

namespace aven {

Json ProjectSettings::toJson() const {
    Json j = Json::object();
    j["aven"] = "project";
    j["name"] = name;
    j["version"] = version;
    if (!description.empty())
        j["description"] = description;
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
    if (!layers.empty()) {
        Json names = Json::array();
        for (auto& l : layers)
            names.push(l);
        j["layers"] = std::move(names);
    }
    if (!layerIgnores.empty()) {
        Json pairs = Json::array();
        for (auto& [a, b] : layerIgnores) {
            Json pair = Json::array();
            pair.push(a);
            pair.push(b);
            pairs.push(std::move(pair));
        }
        j["layers_pass_through"] = std::move(pairs);
    }
    return j;
}

void ProjectSettings::fromJson(const Json& j) {
    name = j["name"].asString(name);
    version = j["version"].asString(version);
    description = j["description"].asString("");
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
    layers.clear();
    for (auto& l : j["layers"].elements())
        if (l.isString() && !l.asString().empty() && l.asString() != "Default" &&
            static_cast<int>(layers.size()) < kMaxLayers - 1)
            layers.push_back(l.asString());
    layerIgnores.clear();
    for (auto& pair : j["layers_pass_through"].elements())
        if (pair.isArray() && pair.size() == 2)
            layerIgnores.emplace_back(pair[0].asString("Default"), pair[1].asString("Default"));
}

std::vector<std::string> ProjectSettings::layerNames() const {
    std::vector<std::string> names{"Default"};
    names.insert(names.end(), layers.begin(), layers.end());
    return names;
}

int ProjectSettings::layerIndex(std::string_view name) const {
    for (size_t i = 0; i < layers.size() && i + 1 < kMaxLayers; ++i)
        if (layers[i] == name)
            return static_cast<int>(i) + 1;
    return 0;
}

uint32_t ProjectSettings::collisionMask(int layer) const {
    uint32_t mask = 0xFFFFFFFFu;
    auto nameOf = [&](int i) { return i == 0 ? std::string("Default") : layers[static_cast<size_t>(i - 1)]; };
    std::string me = layer > 0 && layer <= static_cast<int>(layers.size()) ? nameOf(layer) : "Default";
    auto known = [&](const std::string& n) { return n == "Default" || layerIndex(n) > 0; };
    for (auto& [a, b] : layerIgnores) {
        if (!known(a) || !known(b))
            continue; // a pair naming a layer that was removed
        if (a == me)
            mask &= ~(1u << layerIndex(b));
        if (b == me)
            mask &= ~(1u << layerIndex(a));
    }
    return mask;
}

bool ProjectSettings::layersCollide(const std::string& a, const std::string& b) const {
    return (collisionMask(layerIndex(a)) >> layerIndex(b)) & 1u;
}

void ProjectSettings::setLayersCollide(const std::string& a, const std::string& b, bool collide) {
    std::erase_if(layerIgnores, [&](const auto& p) { return (p.first == a && p.second == b) || (p.first == b && p.second == a); });
    if (!collide)
        layerIgnores.emplace_back(a, b);
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
