#pragma once

#include "aven/math/math.h"
#include "aven/render/rhi.h"
#include "aven/scene/components.h"

#include <cstdint>
#include <vector>

namespace aven {

struct MeshVertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
    float joints[4] = {0, 0, 0, 0};
    float weights[4] = {0, 0, 0, 0};
};

// Triangle mesh kept on the CPU (for picking and bounds) and on the GPU.
struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    Vec3 boundsMin{0, 0, 0}, boundsMax{0, 0, 0};

    void computeBounds();
    // Ray test in the mesh's local space. Returns the distance along the ray, or -1.
    float raycast(Vec3 origin, Vec3 direction) const;
};

struct GpuMesh {
    rhi::BufferHandle vertexBuffer;
    rhi::BufferHandle indexBuffer;
    uint32_t indexCount = 0;
    Vec3 boundsMin, boundsMax;
    const MeshData* cpu = nullptr;

    void upload(rhi::Device& device, const MeshData& data);
    void release(rhi::Device& device);
};

rhi::VertexLayout meshVertexLayout();

MeshData makeCube();
MeshData makeSphere(int segments = 32, int rings = 16);
MeshData makePlane(); // 1x1, facing +Y
MeshData makeCylinder(int segments = 32);
MeshData makeCapsule(int segments = 24, int rings = 8);
MeshData makeCone(int segments = 32);
MeshData makeTorus(int segments = 40, int sides = 16);
MeshData makePrimitive(MeshShape shape);

} // namespace aven
