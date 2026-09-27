#include "aven/runtime/project.h"

#include "aven/core/fs.h"

#include <algorithm>

namespace aven {

std::string ProjectSettings::saveFolderName() const {
    return id.empty() ? name : name + " " + id.substr(0, 8);
}

Json ProjectSettings::toJson() const {
    Json j = Json::object();
    j["aven"] = "project";
    j["name"] = name;
    if (!id.empty())
        j["id"] = id;
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
    if (audioBuses != defaultAudioBuses()) {
        Json buses = Json::array();
        for (auto& b : audioBuses) {
            Json j2 = Json::object();
            j2["name"] = b.name;
            j2["volume"] = b.volume;
            if (b.muted)
                j2["muted"] = true;
            if (b.lowpass > 0)
                j2["lowpass"] = b.lowpass;
            if (b.echo > 0) {
                j2["echo"] = b.echo;
                j2["echo_delay"] = b.echoDelay;
            }
            buses.push(std::move(j2));
        }
        j["audio_buses"] = std::move(buses);
    }
    if (!layers.empty()) {
        Json names = Json::array();
        for (auto& l : layers)
            names.push(l);
        j["layers"] = std::move(names);
    }
    if (!(touch == TouchSettings{})) {
        Json t = Json::object();
        t["mode"] = touch.mode == TouchMode::Always ? "always" : touch.mode == TouchMode::Off ? "off" : "auto";
        t["stick"] = touch.stick;
        Json buttons = Json::array();
        for (auto& b : touch.buttons) {
            Json bj = Json::object();
            bj["label"] = b.label;
            bj["key"] = b.key;
            buttons.push(std::move(bj));
        }
        t["buttons"] = std::move(buttons);
        j["touch"] = std::move(t);
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
    id = j["id"].asString("");
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
    touch = TouchSettings{};
    if (const Json& t = j["touch"]; t.isObject()) {
        std::string mode = t["mode"].asString("auto");
        touch.mode = mode == "always" ? TouchMode::Always : mode == "off" ? TouchMode::Off : TouchMode::Auto;
        touch.stick = t["stick"].asBool(true);
        if (t["buttons"].isArray()) {
            touch.buttons.clear();
            for (auto& b : t["buttons"].elements())
                if (touch.buttons.size() < 4)
                    touch.buttons.push_back({b["label"].asString(""), b["key"].asString("space")});
        }
    }
    audioBuses = defaultAudioBuses();
    if (j["audio_buses"].isArray()) {
        audioBuses.clear();
        for (auto& b : j["audio_buses"].elements()) {
            AudioBus bus;
            bus.name = b["name"].asString("");
            if (bus.name.empty())
                continue;
            bus.volume = b["volume"].asFloat(1.0f);
            bus.muted = b["muted"].asBool(false);
            bus.lowpass = b["lowpass"].asFloat(0.0f);
            bus.echo = b["echo"].asFloat(0.0f);
            bus.echoDelay = b["echo_delay"].asFloat(0.3f);
            audioBuses.push_back(bus);
        }
        for (const char* needed : {"Music", "Effects"})
            if (std::none_of(audioBuses.begin(), audioBuses.end(), [&](const AudioBus& b) { return b.name == needed; }))
                audioBuses.push_back({needed});
    }
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

std::vector<AudioBus> ProjectSettings::defaultAudioBuses() { return {{"Music"}, {"Effects"}, {"Voice"}}; }

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
