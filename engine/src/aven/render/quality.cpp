#include "aven/render/quality.h"

#include <cctype>

namespace aven {

RenderQuality RenderQuality::preset(GraphicsQuality level) {
    RenderQuality q;
    q.level = level;
    switch (level) {
    case GraphicsQuality::Low:
        q.sunShadows = q.lampShadows = false;
        q.shadowMapSize = 256; // (not drawn into; kept small because shaders still bind it)
        q.maxLights = 2;
        q.ssao = q.bloom = q.fxaa = false;
        q.renderScale = 0.75f;
        break;
    case GraphicsQuality::Medium:
        q.lampShadows = false;
        q.shadowMapSize = 1024;
        q.maxLights = 4;
        q.ssao = false;
        break;
    case GraphicsQuality::High: break;
    case GraphicsQuality::Ultra: q.shadowMapSize = 4096; break;
    }
    return q;
}

const char* qualityName(GraphicsQuality q) {
    switch (q) {
    case GraphicsQuality::Low: return "Low";
    case GraphicsQuality::Medium: return "Medium";
    case GraphicsQuality::High: return "High";
    case GraphicsQuality::Ultra: return "Ultra";
    }
    return "High";
}

bool parseQuality(std::string_view name, GraphicsQuality& out) {
    std::string lower;
    for (char c : name)
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (GraphicsQuality q : {GraphicsQuality::Low, GraphicsQuality::Medium, GraphicsQuality::High, GraphicsQuality::Ultra}) {
        std::string n = qualityName(q);
        for (char& c : n)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower == n) {
            out = q;
            return true;
        }
    }
    return false;
}

} // namespace aven
