#pragma once

// Graphics quality presets, like the Low / Medium / High / Ultra of most games. High is what Rynax
// always drew; the others trade detail for speed (Low runs on almost anything) or the other way.
//
//                 Low     Medium   High    Ultra
//   sun shadows   off     on       on      on
//   lamp shadows  off     off      on      on
//   shadow map    -       1024     2048    4096
//   lights/pixel  2       4        8       8
//   SSAO          off     off      on*     on*      (* if the camera's post-processing asks for it)
//   bloom         off     on*      on*     on*
//   anti-alias    off     on*      on*     on*
//   render scale  75%     100%     100%    100%

#include <string>
#include <string_view>

namespace rynax {

enum class GraphicsQuality { Low, Medium, High, Ultra };

struct RenderQuality {
    GraphicsQuality level = GraphicsQuality::High;
    bool sunShadows = true;
    bool lampShadows = true; // point and spot lights
    int shadowMapSize = 2048;
    int maxLights = 8; // point, spot and extra directional lights lighting each pixel
    bool ssao = true;
    bool bloom = true;
    bool fxaa = true;
    float renderScale = 1.0f; // of the window's pixels (the picture is stretched back to fill it)

    static RenderQuality preset(GraphicsQuality level);
};

const char* qualityName(GraphicsQuality q); // "Low", "Medium", "High", "Ultra"
// "low", "Medium", "ULTRA"... false (leaving `out` alone) for anything else.
bool parseQuality(std::string_view name, GraphicsQuality& out);

} // namespace rynax
