#pragma once

#include "aven/render/mesh.h"
#include "aven/render/rhi.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace aven {

struct ModelMaterial {
    Color baseColor{1, 1, 1, 1};
    rhi::TextureHandle baseTexture;
    float metallic = 0.0f;
    float roughness = 0.6f;
    Vec3 emissive{0, 0, 0};
    bool doubleSided = false;
    bool transparent = false;
};

struct ModelPart {
    MeshData data;
    GpuMesh gpu;
    int material = -1;
    bool skinned = false;
};

struct ModelNode {
    std::string name;
    int parent = -1;
    Vec3 translation{0, 0, 0};
    Quat rotation;
    Vec3 scale{1, 1, 1};
    std::vector<int> parts;
    int skin = -1;
};

struct ModelSkin {
    std::vector<int> joints; // node indices
    std::vector<Mat4> inverseBind;
};

struct AnimationChannel {
    int node = -1;
    int path = 0; // 0 translation, 1 rotation, 2 scale
    bool step = false;
    std::vector<float> times;
    std::vector<Vec4> values;
};

struct AnimationClip {
    std::string name;
    float duration = 0;
    std::vector<AnimationChannel> channels;
};

// A 3D model loaded from a glTF 2.0 file (.gltf or .glb), including its
// materials, skeleton and animations.
class Model {
public:
    ~Model();
    bool load(rhi::Device* device, const std::filesystem::path& path, std::string* error);
    void release();

    std::vector<ModelNode> nodes;
    std::vector<ModelPart> parts;
    std::vector<ModelMaterial> materials;
    std::vector<ModelSkin> skins;
    std::vector<AnimationClip> animations;
    Vec3 boundsMin, boundsMax;

    const AnimationClip* findClip(const std::string& name) const;
    // Node transforms relative to the model root, with an optional animation applied.
    void computePose(const AnimationClip* clip, float time, std::vector<Mat4>& nodeMatrices) const;

private:
    rhi::Device* device_ = nullptr;
    std::vector<rhi::TextureHandle> textures_;
};

} // namespace aven
