#include "aven/render/renderer3d.h"

#include "aven/core/log.h"
#include "aven/render/mesh.h"
#include "aven/render/model.h"
#include "aven/render/scene_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace aven {

namespace {

// ---------------------------------------------------------------- shaders

const char* kFrameBlock = R"(
layout(std140) uniform Frame {
    mat4 u_view;
    mat4 u_proj;
    mat4 u_view_proj;
    vec4 u_camera_pos;     // w: time
    vec4 u_ambient;        // rgb: ambient color * intensity, w: sky mode
    vec4 u_sky_top;        // w: ambient intensity
    vec4 u_sky_horizon;
    vec4 u_ground;
    vec4 u_fog;            // rgb: color, w: density (0 = off)
    vec4 u_sun_dir;        // xyz: direction toward the sun, w: has sun
    vec4 u_sun_color;      // rgb: color * intensity, w: casts shadows
    vec4 u_cascade_splits; // xyz: far distance of each cascade
    vec4 u_shadow_params;  // x: cascade texel, y: local texel, z: normal bias, w: light count
    mat4 u_cascade[3];
    vec4 u_light_pos[8];   // xyz, w: range
    vec4 u_light_dir[8];   // xyz: direction the light points, w: type (0 dir, 1 point, 2 spot)
    vec4 u_light_color[8];
    vec4 u_light_spot[8];  // x: cos outer, y: cos inner, z: first shadow tile (-1 = none)
    mat4 u_local_shadow[16];
};
)";

const char* kObjectBlock = R"(
layout(std140) uniform Object {
    mat4 u_model;
    mat4 u_normal_matrix;
    vec4 u_color;
    vec4 u_emission;
    vec4 u_params; // metallic, roughness, unlit, has texture
    vec4 u_extra;  // tiling x, tiling y, skinned, 0
};
layout(std140) uniform Skin { mat4 u_joints[128]; };
)";

const char* kMeshVS = R"(
in vec3 a_position;
in vec3 a_normal;
in vec2 a_uv;
in vec4 a_joints;
in vec4 a_weights;
out vec3 v_world;
out vec3 v_normal;
out vec2 v_uv;
out float v_view_depth;
void main() {
    mat4 skin = mat4(1.0);
    if (u_extra.z > 0.5) {
        skin = a_weights.x * u_joints[int(a_joints.x)] + a_weights.y * u_joints[int(a_joints.y)] +
               a_weights.z * u_joints[int(a_joints.z)] + a_weights.w * u_joints[int(a_joints.w)];
    }
    vec4 world = u_model * skin * vec4(a_position, 1.0);
    v_world = world.xyz;
    v_normal = mat3(u_normal_matrix) * (mat3(skin) * a_normal);
    v_uv = a_uv * u_extra.xy;
    vec4 view = u_view * world;
    v_view_depth = -view.z;
    gl_Position = u_proj * view;
}
)";

const char* kSkyFunctions = R"(
vec3 to_linear(vec3 c) { return pow(max(c, vec3(0.0)), vec3(2.2)); }
vec3 sky_color(vec3 dir) {
    vec3 top = to_linear(u_sky_top.rgb), horizon = to_linear(u_sky_horizon.rgb), ground = to_linear(u_ground.rgb);
    float y = dir.y;
    vec3 c = y > 0.0 ? mix(horizon, top, pow(clamp(y, 0.0, 1.0), 0.5))
                     : mix(horizon, ground, pow(clamp(-y, 0.0, 1.0), 0.35));
    if (u_ambient.w > 1.5 && u_sun_dir.w > 0.5) {
        float s = max(dot(dir, normalize(u_sun_dir.xyz)), 0.0);
        c += u_sun_color.rgb * (pow(s, 1500.0) * 30.0 + pow(s, 16.0) * 0.18);
    }
    return c;
}
vec3 apply_fog(vec3 color, float dist) {
    if (u_fog.w <= 0.0) return color;
    float f = 1.0 - exp(-pow(dist * u_fog.w, 2.0));
    return mix(color, to_linear(u_fog.rgb), clamp(f, 0.0, 1.0));
}
)";

const char* kPbrFS = R"(
uniform sampler2D u_albedo;
uniform sampler2DShadow u_csm;
uniform sampler2DShadow u_local;
in vec3 v_world;
in vec3 v_normal;
in vec2 v_uv;
in float v_view_depth;
out vec4 frag_color;
out vec4 frag_normal;
const float PI = 3.14159265;

float pcf(sampler2DShadow tex, vec3 coord, float texel) {
    float s = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            s += texture(tex, vec3(coord.xy + vec2(x, y) * texel, coord.z));
    return s / 9.0;
}

float sun_shadow(vec3 p, vec3 n) {
    if (u_sun_color.w < 0.5) return 1.0;
    int c = 0;
    if (v_view_depth > u_cascade_splits.x) c = 1;
    if (v_view_depth > u_cascade_splits.y) c = 2;
    if (v_view_depth > u_cascade_splits.z) return 1.0;
    vec4 sc = u_cascade[c] * vec4(p + n * u_shadow_params.z * float(c + 1), 1.0);
    vec3 coord = sc.xyz / sc.w;
    float fade = smoothstep(u_cascade_splits.z * 0.85, u_cascade_splits.z, v_view_depth);
    return mix(pcf(u_csm, coord - vec3(0.0, 0.0, 0.0004), u_shadow_params.x), 1.0, fade);
}

float local_shadow(int i, vec3 p, vec3 n) {
    float tile = u_light_spot[i].z;
    if (tile < 0.0) return 1.0;
    int index = int(tile + 0.5);
    if (u_light_dir[i].w < 1.5) {
        vec3 d = p - u_light_pos[i].xyz;
        vec3 a = abs(d);
        int face = (a.x > a.y && a.x > a.z) ? (d.x > 0.0 ? 0 : 1) : (a.y > a.z ? (d.y > 0.0 ? 2 : 3) : (d.z > 0.0 ? 4 : 5));
        index += face;
    }
    vec4 sc = u_local_shadow[index] * vec4(p + n * 0.03, 1.0);
    vec3 coord = sc.xyz / sc.w;
    return pcf(u_local, coord - vec3(0.0, 0.0, 0.0006), u_shadow_params.y);
}

float d_ggx(float nh, float a) {
    float a2 = a * a;
    float d = nh * nh * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}
float g_smith(float nv, float nl, float rough) {
    float k = (rough + 1.0) * (rough + 1.0) / 8.0;
    return (nv / (nv * (1.0 - k) + k)) * (nl / (nl * (1.0 - k) + k));
}
vec3 f_schlick(float c, vec3 f0) { return f0 + (1.0 - f0) * pow(1.0 - c, 5.0); }
vec3 f_schlick_rough(float c, vec3 f0, float r) { return f0 + (max(vec3(1.0 - r), f0) - f0) * pow(1.0 - c, 5.0); }

vec3 brdf(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float rough, vec3 F0) {
    vec3 H = normalize(V + L);
    float nl = max(dot(N, L), 0.0), nv = max(dot(N, V), 1e-4), nh = max(dot(N, H), 0.0);
    if (nl <= 0.0) return vec3(0.0);
    vec3 F = f_schlick(max(dot(H, V), 0.0), F0);
    vec3 spec = d_ggx(nh, rough * rough) * g_smith(nv, nl, rough) * F / (4.0 * nv * nl + 1e-4);
    vec3 kd = (1.0 - F) * (1.0 - metallic);
    return (kd * albedo / PI + spec) * nl;
}

void main() {
    vec4 base = u_color;
    if (u_params.w > 0.5) base *= texture(u_albedo, v_uv);
    vec3 albedo = to_linear(base.rgb);
    vec3 N = normalize(v_normal);
    if (!gl_FrontFacing) N = -N;
    frag_normal = vec4(normalize((u_view * vec4(N, 0.0)).xyz) * 0.5 + 0.5, 1.0);
    float dist = length(u_camera_pos.xyz - v_world);
    if (u_params.z > 0.5) {
        frag_color = vec4(apply_fog(albedo + u_emission.rgb, dist), base.a);
        return;
    }
    vec3 V = normalize(u_camera_pos.xyz - v_world);
    float metallic = clamp(u_params.x, 0.0, 1.0);
    float rough = clamp(u_params.y, 0.045, 1.0);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 color = vec3(0.0);
    if (u_sun_dir.w > 0.5)
        color += brdf(N, V, normalize(u_sun_dir.xyz), albedo, metallic, rough, F0) * u_sun_color.rgb *
                 sun_shadow(v_world, N);
    int count = int(u_shadow_params.w + 0.5);
    for (int i = 0; i < 8; ++i) {
        if (i >= count) break;
        float type = u_light_dir[i].w;
        vec3 L;
        float atten = 1.0;
        if (type < 0.5) {
            L = normalize(-u_light_dir[i].xyz);
        } else {
            vec3 to_light = u_light_pos[i].xyz - v_world;
            float d = length(to_light);
            L = to_light / max(d, 1e-4);
            float range = max(u_light_pos[i].w, 0.01);
            float falloff = clamp(1.0 - pow(d / range, 4.0), 0.0, 1.0);
            atten = falloff * falloff / (d * d + 1.0);
            if (type > 1.5)
                atten *= smoothstep(u_light_spot[i].x, u_light_spot[i].y, dot(-L, normalize(u_light_dir[i].xyz)));
            if (atten > 0.0)
                atten *= local_shadow(i, v_world, N);
        }
        color += brdf(N, V, L, albedo, metallic, rough, F0) * u_light_color[i].rgb * atten;
    }

    // Ambient light from the sky: a soft hemisphere for diffuse and blurred sky reflections.
    float nv = max(dot(N, V), 0.0);
    vec3 ks = f_schlick_rough(nv, F0, rough);
    vec3 kd = (1.0 - ks) * (1.0 - metallic);
    vec3 hemi = mix(to_linear(u_ground.rgb), mix(to_linear(u_sky_horizon.rgb), to_linear(u_sky_top.rgb), 0.6),
                    N.y * 0.5 + 0.5);
    vec3 irradiance = u_ambient.rgb + hemi * u_sky_top.w;
    vec3 R = reflect(-V, N);
    vec3 reflection = mix(sky_color(R), hemi, rough * rough) * u_sky_top.w;
    color += kd * albedo * irradiance + ks * reflection * (1.0 - rough * 0.5);
    color += u_emission.rgb;
    frag_color = vec4(apply_fog(color, dist), base.a);
}
)";

const char* kDepthFS = R"(
void main() {}
)";

const char* kSkyVS = R"(
out vec2 v_ndc;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    v_ndc = p * 2.0 - 1.0;
    gl_Position = vec4(v_ndc, 1.0, 1.0);
}
)";

const char* kSkyFS = R"(
in vec2 v_ndc;
out vec4 frag_color;
out vec4 frag_normal;
void main() {
    vec4 far_point = inverse(u_view_proj) * vec4(v_ndc, 1.0, 1.0);
    vec3 dir = normalize(far_point.xyz / far_point.w - u_camera_pos.xyz);
    vec3 c = sky_color(dir);
    if (u_fog.w > 0.0) c = mix(c, to_linear(u_fog.rgb), clamp(1.0 - dir.y * 4.0, 0.0, 1.0) * clamp(u_fog.w * 20.0, 0.0, 1.0));
    frag_color = vec4(c, 1.0);
    frag_normal = vec4(0.5, 0.5, 1.0, 1.0);
}
)";

constexpr int kAtlasSize = 2048;
constexpr int kCascadeTile = 1024; // 2x2 cascades in the sun atlas
constexpr int kLocalTile = 512;    // 4x4 tiles in the local-light atlas
constexpr int kMaxLights = 8;

struct FrameUniforms {
    Mat4 view, proj, viewProj;
    float cameraPos[4];
    float ambient[4];
    float skyTop[4];
    float skyHorizon[4];
    float ground[4];
    float fog[4];
    float sunDir[4];
    float sunColor[4];
    float cascadeSplits[4];
    float shadowParams[4];
    Mat4 cascade[3];
    float lightPos[kMaxLights][4];
    float lightDir[kMaxLights][4];
    float lightColor[kMaxLights][4];
    float lightSpot[kMaxLights][4];
    Mat4 localShadow[16];
};

struct ObjectUniforms {
    Mat4 model, normalMatrix;
    float color[4];
    float emission[4];
    float params[4];
    float extra[4];
};

void set4(float* dst, float a, float b, float c, float d) {
    dst[0] = a;
    dst[1] = b;
    dst[2] = c;
    dst[3] = d;
}

// Maps clip space into one tile of a shadow atlas (x, y in 0..1 atlas UVs; z in 0..1 depth).
Mat4 tileMatrix(int tileX, int tileY, float tileScale) {
    Mat4 m;
    m.m[0] = 0.5f * tileScale;
    m.m[5] = 0.5f * tileScale;
    m.m[10] = 0.5f;
    m.m[12] = (tileX + 0.5f) * tileScale;
    m.m[13] = (tileY + 0.5f) * tileScale;
    m.m[14] = 0.5f;
    return m;
}

struct Plane {
    Vec3 n;
    float d;
};

std::array<Plane, 6> frustumPlanes(const Mat4& vp) {
    std::array<Plane, 6> planes;
    auto row = [&](int r) { return Vec4(vp.at(r, 0), vp.at(r, 1), vp.at(r, 2), vp.at(r, 3)); };
    Vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    Vec4 p[6] = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r3 + r2, r3 - r2};
    for (int i = 0; i < 6; ++i) {
        float len = length(p[i].xyz());
        planes[i] = {p[i].xyz() / len, p[i].w / len};
    }
    return planes;
}

bool boxVisible(const std::array<Plane, 6>& planes, Vec3 mn, Vec3 mx) {
    for (auto& pl : planes) {
        Vec3 positive{pl.n.x >= 0 ? mx.x : mn.x, pl.n.y >= 0 ? mx.y : mn.y, pl.n.z >= 0 ? mx.z : mn.z};
        if (dot(pl.n, positive) + pl.d < 0)
            return false;
    }
    return true;
}

void transformBounds(const Mat4& m, Vec3 mn, Vec3 mx, Vec3& outMin, Vec3& outMax) {
    outMin = Vec3(1e30f);
    outMax = Vec3(-1e30f);
    for (int c = 0; c < 8; ++c) {
        Vec3 p{(c & 1) ? mx.x : mn.x, (c & 2) ? mx.y : mn.y, (c & 4) ? mx.z : mn.z};
        Vec3 w = transformPoint(m, p);
        outMin = min(outMin, w);
        outMax = max(outMax, w);
    }
}

} // namespace

// ---------------------------------------------------------------- implementation

struct Renderer3D::Impl {
    rhi::Device* device = nullptr;
    Assets* assets = nullptr;
    rhi::ShaderHandle pbrShader, depthShader, skyShader;
    rhi::PipelineHandle opaque, opaqueDouble, transparent, depth, depthDouble, sky;
    MeshData primitiveData[7];
    GpuMesh primitives[7];
    std::unordered_map<std::string, std::unique_ptr<Model>> models;
    std::unordered_set<std::string> failedModels;
    rhi::TextureHandle csmTexture, localTexture;
    rhi::FramebufferHandle csmFb, localFb;

    struct Item {
        Entity entity;
        const GpuMesh* mesh = nullptr;
        Mat4 world;
        Vec3 boundsMin, boundsMax;
        Color color;
        Vec3 emission;
        float metallic = 0, roughness = 0.5f;
        bool unlit = false;
        rhi::TextureHandle texture;
        Vec2 tiling{1, 1};
        int skin = -1;
        bool castShadows = true;
        bool doubleSided = false;
        bool transparent = false;
        float depth = 0;
    };
    std::vector<Item> items;
    std::vector<std::vector<Mat4>> skins;
    std::array<Mat4, 128> skinBuffer{};
    FrameUniforms frame{};
    std::vector<size_t> transparentOrder;
    uint32_t drawn = 0;

    bool init(rhi::Device* d, Assets* a) {
        device = d;
        assets = a;
        std::string common = std::string(kFrameBlock) + kObjectBlock;
        rhi::ShaderDesc pbr;
        pbr.vertex = common + kMeshVS;
        pbr.fragment = common + kSkyFunctions + kPbrFS;
        pbr.attributes = {"a_position", "a_normal", "a_uv", "a_joints", "a_weights"};
        pbr.uniformBlocks = {"Frame", "Object", "Skin"};
        pbr.textures = {"u_albedo", "u_csm", "u_local"};
        pbr.outputs = {"frag_color", "frag_normal"};
        pbr.label = "pbr";
        pbrShader = device->createShader(pbr);

        rhi::ShaderDesc depthDesc = pbr;
        depthDesc.fragment = kDepthFS;
        depthDesc.textures = {};
        depthDesc.outputs = {};
        depthDesc.label = "shadow depth";
        depthShader = device->createShader(depthDesc);

        rhi::ShaderDesc skyDesc;
        skyDesc.vertex = kSkyVS;
        skyDesc.fragment = std::string(kFrameBlock) + kSkyFunctions + kSkyFS;
        skyDesc.uniformBlocks = {"Frame"};
        skyDesc.outputs = {"frag_color", "frag_normal"};
        skyDesc.label = "sky";
        skyShader = device->createShader(skyDesc);
        if (!pbrShader || !depthShader || !skyShader)
            return false;

        rhi::PipelineDesc p;
        p.shader = pbrShader;
        p.layout = meshVertexLayout();
        p.cull = rhi::CullMode::Back;
        p.depthTest = true;
        p.depthWrite = true;
        p.colorTargets = 2;
        p.label = "opaque";
        opaque = device->createPipeline(p);
        p.cull = rhi::CullMode::None;
        opaqueDouble = device->createPipeline(p);
        p.depthWrite = false;
        p.blend = rhi::BlendMode::Alpha;
        p.label = "transparent";
        transparent = device->createPipeline(p);

        rhi::PipelineDesc dp;
        dp.shader = depthShader;
        dp.layout = meshVertexLayout();
        dp.cull = rhi::CullMode::Back;
        dp.depthTest = true;
        dp.depthWrite = true;
        dp.colorWrite = false;
        dp.depthBias = 4.0f;
        dp.slopeBias = 2.5f;
        dp.label = "shadow";
        depth = device->createPipeline(dp);
        dp.cull = rhi::CullMode::None;
        depthDouble = device->createPipeline(dp);

        rhi::PipelineDesc sp;
        sp.shader = skyShader;
        sp.label = "sky";
        sky = device->createPipeline(sp);

        for (int i = 0; i < 7; ++i) {
            primitiveData[i] = makePrimitive(static_cast<MeshShape>(i));
            primitives[i].upload(*device, primitiveData[i]);
        }

        auto shadowAtlas = [&](rhi::TextureHandle& tex, rhi::FramebufferHandle& fb, const char* label) {
            rhi::TextureDesc td;
            td.width = kAtlasSize;
            td.height = kAtlasSize;
            td.format = rhi::PixelFormat::Depth24;
            td.depthCompare = true;
            td.filter = rhi::Filter::Linear;
            td.renderTarget = true;
            td.label = label;
            tex = device->createTexture(td);
            rhi::FramebufferDesc fd;
            fd.depth = tex;
            fd.label = label;
            fb = device->createFramebuffer(fd);
        };
        shadowAtlas(csmTexture, csmFb, "sun shadows");
        shadowAtlas(localTexture, localFb, "light shadows");
        return true;
    }

    void shutdown() {
        if (!device)
            return;
        for (auto& p : primitives)
            p.release(*device);
        models.clear();
        for (auto pl : {opaque, opaqueDouble, transparent, depth, depthDouble, sky})
            device->destroy(pl);
        for (auto s : {pbrShader, depthShader, skyShader})
            device->destroy(s);
        device->destroy(csmFb);
        device->destroy(localFb);
        device->destroy(csmTexture);
        device->destroy(localTexture);
        device = nullptr;
    }

    Model* loadModel(const std::string& path) {
        if (path.empty() || failedModels.count(path))
            return nullptr;
        auto it = models.find(path);
        if (it != models.end())
            return it->second.get();
        auto m = std::make_unique<Model>();
        std::string error;
        if (!m->load(device, assets->resolve(path), &error)) {
            Log::warn("Couldn't load the model '", path, "': ", error);
            failedModels.insert(path);
            return nullptr;
        }
        Model* raw = m.get();
        models[path] = std::move(m);
        return raw;
    }

    static float clipTime(const AnimationClip& clip, const ModelAnimator& anim) {
        if (clip.duration <= 0)
            return 0;
        switch (anim.mode) {
        case ModelAnimationMode::Loop: return std::fmod(std::max(anim.time, 0.0f), clip.duration);
        case ModelAnimationMode::Once: return std::min(anim.time, clip.duration);
        case ModelAnimationMode::PingPong: {
            float t = std::fmod(std::max(anim.time, 0.0f), clip.duration * 2);
            return t > clip.duration ? clip.duration * 2 - t : t;
        }
        }
        return 0;
    }

    void gather(Scene& scene) {
        items.clear();
        skins.clear();
        auto& reg = scene.registry();
        scene.walk([&](Entity e, int) {
            if (!reg.get<EntityInfo>(e).active)
                return false;
            const MeshRenderer* mr = reg.tryGet<MeshRenderer>(e);
            if (!mr || reg.has<Hidden>(e))
                return true;
            const Mat4& world = reg.get<WorldTransform>(e).matrix;
            Item base;
            base.entity = e;
            base.color = mr->color;
            base.emission = mr->emission.rgb() * mr->emissionStrength;
            base.metallic = mr->metallic;
            base.roughness = mr->roughness;
            base.unlit = mr->unlit;
            base.tiling = mr->tiling;
            base.castShadows = mr->castShadows;
            base.transparent = mr->color.a < 0.999f;
            if (!mr->texture.empty())
                base.texture = assets->texture(mr->texture).handle;

            if (mr->mesh != MeshShape::Model) {
                Item it = base;
                it.mesh = &primitives[static_cast<int>(mr->mesh)];
                it.world = world;
                it.doubleSided = mr->mesh == MeshShape::Plane;
                transformBounds(world, it.mesh->boundsMin, it.mesh->boundsMax, it.boundsMin, it.boundsMax);
                items.push_back(it);
                return true;
            }
            Model* model = loadModel(mr->model);
            if (!model)
                return true;
            const AnimationClip* clip = nullptr;
            float time = 0;
            if (const ModelAnimator* anim = reg.tryGet<ModelAnimator>(e)) {
                clip = model->findClip(anim->clip);
                if (clip)
                    time = clipTime(*clip, *anim);
            }
            std::vector<Mat4> pose;
            model->computePose(clip, time, pose);
            for (size_t ni = 0; ni < model->nodes.size(); ++ni) {
                const ModelNode& node = model->nodes[ni];
                int skinIndex = -1;
                if (node.skin >= 0 && static_cast<size_t>(node.skin) < model->skins.size()) {
                    const ModelSkin& skin = model->skins[static_cast<size_t>(node.skin)];
                    std::vector<Mat4> palette(std::min<size_t>(skin.joints.size(), 128));
                    for (size_t j = 0; j < palette.size(); ++j)
                        palette[j] = pose[static_cast<size_t>(skin.joints[j])] * skin.inverseBind[j];
                    skins.push_back(std::move(palette));
                    skinIndex = static_cast<int>(skins.size() - 1);
                }
                for (int pi : node.parts) {
                    const ModelPart& part = model->parts[static_cast<size_t>(pi)];
                    Item it = base;
                    it.mesh = &part.gpu;
                    bool skinned = part.skinned && skinIndex >= 0;
                    it.world = skinned ? world : world * pose[ni];
                    it.skin = skinned ? skinIndex : -1;
                    if (part.material >= 0) {
                        const ModelMaterial& mat = model->materials[static_cast<size_t>(part.material)];
                        it.color = {mat.baseColor.r * mr->color.r, mat.baseColor.g * mr->color.g,
                                    mat.baseColor.b * mr->color.b, mat.baseColor.a * mr->color.a};
                        if (!it.texture)
                            it.texture = mat.baseTexture;
                        it.metallic = mat.metallic;
                        it.roughness = mat.roughness;
                        it.emission = it.emission + mat.emissive;
                        it.doubleSided = mat.doubleSided;
                        it.transparent = mat.transparent || it.color.a < 0.999f;
                    }
                    // Skinned meshes move far from their rest pose; use generous bounds.
                    Vec3 mn = skinned ? model->boundsMin - Vec3(1) : part.gpu.boundsMin;
                    Vec3 mx = skinned ? model->boundsMax + Vec3(1) : part.gpu.boundsMax;
                    transformBounds(it.world, mn, mx, it.boundsMin, it.boundsMax);
                    items.push_back(it);
                }
            }
            return true;
        });
    }

    void drawItem(const Item& it, bool shadowPass) {
        ObjectUniforms o;
        o.model = it.world;
        o.normalMatrix = transpose(inverse(it.world));
        set4(o.color, it.color.r, it.color.g, it.color.b, it.color.a);
        set4(o.emission, it.emission.x, it.emission.y, it.emission.z, 0);
        set4(o.params, it.metallic, it.roughness, it.unlit ? 1.0f : 0.0f, it.texture ? 1.0f : 0.0f);
        set4(o.extra, it.tiling.x, it.tiling.y, it.skin >= 0 ? 1.0f : 0.0f, 0);
        rhi::Bindings b;
        b.vertexBuffer = it.mesh->vertexBuffer;
        b.indexBuffer = it.mesh->indexBuffer;
        if (!shadowPass) {
            b.textures[0] = it.texture ? it.texture : assets->white().handle;
            b.textures[1] = csmTexture;
            b.textures[2] = localTexture;
        }
        device->applyBindings(b);
        device->applyUniforms(1, &o, sizeof o);
        if (it.skin >= 0) {
            // The bound range must cover the whole declared block (128 matrices).
            auto& palette = skins[static_cast<size_t>(it.skin)];
            std::copy(palette.begin(), palette.end(), skinBuffer.begin());
            device->applyUniforms(2, skinBuffer.data(), skinBuffer.size() * sizeof(Mat4));
        }
        device->drawIndexed(0, it.mesh->indexCount);
    }

    void bindIdentitySkin() {
        skinBuffer.fill(Mat4{});
        device->applyUniforms(2, skinBuffer.data(), skinBuffer.size() * sizeof(Mat4));
    }

    void renderShadowTile(rhi::FramebufferHandle, int x, int y, int size, const Mat4& viewProj) {
        device->setViewport(x, y, size, size);
        bindIdentitySkin();
        FrameUniforms f = frame;
        f.view = Mat4{};
        f.proj = viewProj;
        f.viewProj = viewProj;
        device->applyUniforms(0, &f, sizeof f);
        auto planes = frustumPlanes(viewProj);
        for (auto& it : items) {
            if (!it.castShadows || it.transparent || !boxVisible(planes, it.boundsMin, it.boundsMax))
                continue;
            device->applyPipeline(it.doubleSided ? depthDouble : depth);
            drawItem(it, true);
        }
    }

    void setupLights(Scene& scene, const CameraView& camera) {
        auto& reg = scene.registry();
        Entity sun;
        std::vector<Entity> others;
        scene.walk([&](Entity e, int) {
            if (!reg.get<EntityInfo>(e).active)
                return false;
            if (const Light* l = reg.tryGet<Light>(e)) {
                if (l->type == LightType::Directional && !sun)
                    sun = e;
                else
                    others.push_back(e);
            }
            return true;
        });
        Vec3 camPos = camera.position;
        std::sort(others.begin(), others.end(), [&](Entity a, Entity b) {
            const Light& la = reg.get<Light>(a);
            const Light& lb = reg.get<Light>(b);
            float da = la.type == LightType::Directional ? 0 : length(scene.worldPosition(a) - camPos) - la.range;
            float db = lb.type == LightType::Directional ? 0 : length(scene.worldPosition(b) - camPos) - lb.range;
            return da < db;
        });
        if (others.size() > kMaxLights)
            others.resize(kMaxLights);

        set4(frame.sunDir, 0, 1, 0, 0);
        set4(frame.sunColor, 0, 0, 0, 0);
        if (sun) {
            const Light& l = reg.get<Light>(sun);
            Vec3 dir = normalize(transformDirection(scene.worldMatrix(sun), {0, 0, -1}));
            set4(frame.sunDir, -dir.x, -dir.y, -dir.z, 1);
            Vec3 c = pow3(l.color.rgb()) * l.intensity * 3.0f;
            set4(frame.sunColor, c.x, c.y, c.z, l.castShadows ? 1.0f : 0.0f);
        }

        // Shadow tiles for spot (1 tile) and point (6 tiles) lights.
        int nextTile = 0;
        int count = 0;
        struct Pending {
            int light;
            int tile;
            Mat4 matrices[6];
            int faces;
        };
        std::vector<Pending> pending;
        for (Entity e : others) {
            const Light& l = reg.get<Light>(e);
            Mat4 w = scene.worldMatrix(e);
            Vec3 pos{w.m[12], w.m[13], w.m[14]};
            Vec3 dir = normalize(transformDirection(w, {0, 0, -1}));
            float type = l.type == LightType::Directional ? 0.0f : l.type == LightType::Point ? 1.0f : 2.0f;
            set4(frame.lightPos[count], pos.x, pos.y, pos.z, l.range);
            set4(frame.lightDir[count], dir.x, dir.y, dir.z, type);
            Vec3 c = pow3(l.color.rgb()) * l.intensity * (type == 0 ? 3.0f : 12.0f);
            set4(frame.lightColor[count], c.x, c.y, c.z, 0);
            float outer = std::cos(radians(clamp(l.spotAngle, 1.0f, 170.0f) * 0.5f));
            float inner = std::cos(radians(clamp(l.spotAngle, 1.0f, 170.0f) * 0.4f));
            set4(frame.lightSpot[count], outer, inner, -1, 0);
            int faces = type == 2.0f ? 1 : type == 1.0f ? 6 : 0;
            if (l.castShadows && faces > 0 && nextTile + faces <= 16) {
                Pending p{count, nextTile, {}, faces};
                if (faces == 1) {
                    Vec3 up = std::abs(dir.y) > 0.99f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
                    Mat4 view = Mat4::lookAt(pos, pos + dir, up);
                    Mat4 proj = Mat4::perspective(radians(std::min(l.spotAngle + 8.0f, 170.0f)), 1, 0.05f, std::max(l.range, 0.1f));
                    p.matrices[0] = proj * view;
                } else {
                    const Vec3 dirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                    const Vec3 ups[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};
                    Mat4 proj = Mat4::perspective(radians(92.0f), 1, 0.05f, std::max(l.range, 0.1f));
                    for (int f = 0; f < 6; ++f)
                        p.matrices[f] = proj * Mat4::lookAt(pos, pos + dirs[f], ups[f]);
                }
                frame.lightSpot[count][2] = static_cast<float>(nextTile);
                for (int f = 0; f < faces; ++f) {
                    int tile = nextTile + f;
                    frame.localShadow[tile] = tileMatrix(tile % 4, tile / 4, 0.25f) * p.matrices[f];
                }
                nextTile += faces;
                pending.push_back(p);
            }
            ++count;
        }
        frame.shadowParams[3] = static_cast<float>(count);
        frame.shadowParams[1] = 1.0f / kAtlasSize;

        if (!pending.empty()) {
            rhi::PassDesc pass;
            pass.framebuffer = localFb;
            pass.width = kAtlasSize;
            pass.height = kAtlasSize;
            pass.clearColor = false;
            pass.label = "light shadows";
            device->beginPass(pass);
            for (auto& p : pending)
                for (int f = 0; f < p.faces; ++f) {
                    int tile = p.tile + f;
                    renderShadowTile(localFb, (tile % 4) * kLocalTile, (tile / 4) * kLocalTile, kLocalTile, p.matrices[f]);
                }
            device->endPass();
        }

        if (sun && frame.sunColor[3] > 0.5f)
            renderCascades(Vec3(frame.sunDir[0], frame.sunDir[1], frame.sunDir[2]), camera);
    }

    static Vec3 pow3(Vec3 c) { return {std::pow(c.x, 2.2f), std::pow(c.y, 2.2f), std::pow(c.z, 2.2f)}; }

    void renderCascades(Vec3 toSun, const CameraView& camera) {
        float nearD = std::max(camera.orthographic ? 0.0f : camera.nearClip, 0.01f);
        float farD = std::min(camera.farClip, 70.0f);
        float splits[4] = {nearD, 0, 0, farD};
        for (int i = 1; i < 3; ++i) {
            float f = static_cast<float>(i) / 3.0f;
            float logSplit = nearD * std::pow(farD / nearD, f);
            float uniform = nearD + (farD - nearD) * f;
            splits[i] = lerp(uniform, logSplit, 0.85f);
        }
        set4(frame.cascadeSplits, splits[1], splits[2], splits[3], 3);
        frame.shadowParams[0] = 1.0f / kAtlasSize;
        frame.shadowParams[2] = 0.02f;

        Mat4 camWorld = inverse(camera.view);
        float tanHalf = std::tan(radians(camera.fieldOfView) * 0.5f);
        rhi::PassDesc pass;
        pass.framebuffer = csmFb;
        pass.width = kAtlasSize;
        pass.height = kAtlasSize;
        pass.clearColor = false;
        pass.label = "sun shadows";
        device->beginPass(pass);
        for (int c = 0; c < 3; ++c) {
            Vec3 corners[8];
            for (int k = 0; k < 8; ++k) {
                float d = (k & 4) ? splits[c + 1] : splits[c];
                float hh = camera.orthographic ? camera.orthoSize : d * tanHalf;
                float hw = hh * camera.aspect;
                Vec3 local{(k & 1) ? hw : -hw, (k & 2) ? hh : -hh, -d};
                corners[k] = transformPoint(camWorld, local);
            }
            Vec3 center;
            for (auto& p : corners)
                center += p;
            center = center / 8.0f;
            float radius = 0;
            for (auto& p : corners)
                radius = std::max(radius, length(p - center));
            radius = std::ceil(radius * 16.0f) / 16.0f;
            Vec3 up = std::abs(toSun.y) > 0.99f ? Vec3(0, 0, 1) : Vec3(0, 1, 0);
            float back = radius + 60.0f;
            Mat4 view = Mat4::lookAt(center + toSun * back, center, up);
            Mat4 proj = Mat4::orthographic(-radius, radius, -radius, radius, 0.1f, back + radius + 10.0f);
            // Snap to whole shadow texels so shadows don't shimmer as the camera moves.
            Mat4 vp = proj * view;
            Vec4 origin = vp * Vec4(0, 0, 0, 1);
            float texels = kCascadeTile * 0.5f;
            float ox = origin.x * texels, oy = origin.y * texels;
            Mat4 snap = Mat4::translation({(std::round(ox) - ox) / texels, (std::round(oy) - oy) / texels, 0});
            vp = snap * vp;
            frame.cascade[c] = tileMatrix(c % 2, c / 2, 0.5f) * vp;
            renderShadowTile(csmFb, (c % 2) * kCascadeTile, (c / 2) * kCascadeTile, kCascadeTile, vp);
        }
        device->endPass();
    }

    void setupEnvironment(Scene& scene, const CameraView& camera) {
        Environment env;
        auto& reg = scene.registry();
        for (Entity e : reg.entitiesWith<Environment>())
            if (scene.isActive(e)) {
                env = reg.get<Environment>(e);
                break;
            }
        frame.view = camera.view;
        frame.proj = camera.projection;
        frame.viewProj = camera.viewProjection;
        set4(frame.cameraPos, camera.position.x, camera.position.y, camera.position.z, 0);
        Vec3 amb = pow3(env.ambient.rgb()) * env.ambientIntensity * 0.5f;
        set4(frame.ambient, amb.x, amb.y, amb.z, static_cast<float>(env.sky));
        set4(frame.skyTop, env.skyTop.r, env.skyTop.g, env.skyTop.b, env.ambientIntensity);
        set4(frame.skyHorizon, env.skyHorizon.r, env.skyHorizon.g, env.skyHorizon.b, 0);
        set4(frame.ground, env.ground.r, env.ground.g, env.ground.b, 0);
        set4(frame.fog, env.fogColor.r, env.fogColor.g, env.fogColor.b, env.fog ? env.fogDensity : 0.0f);
        set4(frame.cascadeSplits, 0, 0, 0, 0);
        std::memset(frame.shadowParams, 0, sizeof frame.shadowParams);
    }
};

Renderer3D::Renderer3D() : impl_(std::make_unique<Impl>()) {}
Renderer3D::~Renderer3D() = default;

bool Renderer3D::init(rhi::Device* device, Assets* assets) { return impl_->init(device, assets); }
void Renderer3D::shutdown() { impl_->shutdown(); }

bool Renderer3D::hasContent(const Scene& scene) const {
    return scene.registry().count<MeshRenderer>() > 0;
}

void Renderer3D::prepare(Scene& scene, const CameraView& camera) {
    impl_->gather(scene);
    impl_->setupEnvironment(scene, camera);
    impl_->setupLights(scene, camera);
}

void Renderer3D::drawSky(Scene& scene, const CameraView&) {
    (void)scene;
    if (impl_->frame.ambient[3] < 0.5f)
        return; // solid color: the pass was cleared with the camera's background
    impl_->device->applyPipeline(impl_->sky);
    impl_->device->applyBindings({});
    impl_->device->applyUniforms(0, &impl_->frame, sizeof impl_->frame);
    impl_->device->draw(0, 3);
}

void Renderer3D::drawOpaque(Scene&, const CameraView& camera) {
    auto& d = *impl_->device;
    d.applyUniforms(0, &impl_->frame, sizeof impl_->frame);
    impl_->bindIdentitySkin();
    auto planes = frustumPlanes(camera.viewProjection);
    impl_->drawn = 0;
    impl_->transparentOrder.clear();
    for (size_t i = 0; i < impl_->items.size(); ++i) {
        auto& it = impl_->items[i];
        if (!boxVisible(planes, it.boundsMin, it.boundsMax))
            continue;
        if (it.transparent) {
            it.depth = transformPoint(camera.view, (it.boundsMin + it.boundsMax) * 0.5f).z;
            impl_->transparentOrder.push_back(i);
            continue;
        }
        d.applyPipeline(it.doubleSided ? impl_->opaqueDouble : impl_->opaque);
        impl_->drawItem(it, false);
        ++impl_->drawn;
    }
}

void Renderer3D::drawTransparent(Scene&, const CameraView&) {
    if (impl_->transparentOrder.empty())
        return;
    auto& d = *impl_->device;
    auto& order = impl_->transparentOrder;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return impl_->items[a].depth < impl_->items[b].depth; });
    d.applyUniforms(0, &impl_->frame, sizeof impl_->frame);
    impl_->bindIdentitySkin();
    d.applyPipeline(impl_->transparent);
    for (size_t i : order) {
        impl_->drawItem(impl_->items[i], false);
        ++impl_->drawn;
    }
}

Model* Renderer3D::model(const std::string& path) { return impl_->loadModel(path); }

Entity Renderer3D::raycast(Scene& scene, Vec3 origin, Vec3 direction, float* distance) {
    impl_->gather(scene);
    Entity best;
    float bestDist = 1e30f;
    for (auto& it : impl_->items) {
        if (!it.mesh || !it.mesh->cpu)
            continue;
        // Quick reject with the bounding box, then test the triangles in local space.
        Vec3 inv{1.0f / (direction.x == 0 ? 1e-9f : direction.x), 1.0f / (direction.y == 0 ? 1e-9f : direction.y),
                 1.0f / (direction.z == 0 ? 1e-9f : direction.z)};
        Vec3 t1 = (it.boundsMin - origin) * inv, t2 = (it.boundsMax - origin) * inv;
        Vec3 tmin = min(t1, t2), tmax = max(t1, t2);
        float enter = std::max({tmin.x, tmin.y, tmin.z}), exit = std::min({tmax.x, tmax.y, tmax.z});
        if (exit < std::max(enter, 0.0f) || enter > bestDist)
            continue;
        Mat4 invWorld = inverse(it.world);
        Vec3 lo = transformPoint(invWorld, origin);
        Vec3 ld = transformDirection(invWorld, direction);
        float scale = length(ld);
        float t = it.skin >= 0 ? std::max(enter, 0.0f) * scale : it.mesh->cpu->raycast(lo, ld / scale);
        if (t < 0)
            continue;
        float worldDist = t / scale;
        if (worldDist < bestDist) {
            bestDist = worldDist;
            best = it.entity;
        }
    }
    if (distance)
        *distance = bestDist;
    return best;
}

bool Renderer3D::worldBounds(Scene& scene, Entity e, Vec3& mn, Vec3& mx) {
    impl_->gather(scene);
    bool any = false;
    for (auto& it : impl_->items) {
        if (it.entity != e)
            continue;
        mn = any ? min(mn, it.boundsMin) : it.boundsMin;
        mx = any ? max(mx, it.boundsMax) : it.boundsMax;
        any = true;
    }
    return any;
}

uint32_t Renderer3D::drawnObjects() const { return impl_->drawn; }

} // namespace aven
