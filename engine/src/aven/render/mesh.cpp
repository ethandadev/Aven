#include "aven/render/mesh.h"

#include <cmath>
#include <cstddef>

namespace aven {

void MeshData::computeBounds() {
    if (vertices.empty()) {
        boundsMin = boundsMax = {};
        return;
    }
    boundsMin = boundsMax = vertices[0].position;
    for (auto& v : vertices) {
        boundsMin = min(boundsMin, v.position);
        boundsMax = max(boundsMax, v.position);
    }
}

float MeshData::raycast(Vec3 o, Vec3 d) const {
    float best = -1;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        // Möller–Trumbore, two-sided.
        Vec3 a = vertices[indices[i]].position, b = vertices[indices[i + 1]].position, c = vertices[indices[i + 2]].position;
        Vec3 e1 = b - a, e2 = c - a;
        Vec3 p = cross(d, e2);
        float det = dot(e1, p);
        if (std::abs(det) < 1e-9f)
            continue;
        float inv = 1.0f / det;
        Vec3 t = o - a;
        float u = dot(t, p) * inv;
        if (u < 0 || u > 1)
            continue;
        Vec3 q = cross(t, e1);
        float v = dot(d, q) * inv;
        if (v < 0 || u + v > 1)
            continue;
        float dist = dot(e2, q) * inv;
        if (dist > 1e-5f && (best < 0 || dist < best))
            best = dist;
    }
    return best;
}

void GpuMesh::upload(rhi::Device& device, const MeshData& data) {
    rhi::BufferDesc vb;
    vb.type = rhi::BufferType::Vertex;
    vb.data = data.vertices.data();
    vb.size = data.vertices.size() * sizeof(MeshVertex);
    vb.label = "mesh vertices";
    vertexBuffer = device.createBuffer(vb);
    rhi::BufferDesc ib;
    ib.type = rhi::BufferType::Index;
    ib.data = data.indices.data();
    ib.size = data.indices.size() * sizeof(uint32_t);
    ib.label = "mesh indices";
    indexBuffer = device.createBuffer(ib);
    indexCount = static_cast<uint32_t>(data.indices.size());
    boundsMin = data.boundsMin;
    boundsMax = data.boundsMax;
    cpu = &data;
}

void GpuMesh::release(rhi::Device& device) {
    if (vertexBuffer)
        device.destroy(vertexBuffer);
    if (indexBuffer)
        device.destroy(indexBuffer);
    vertexBuffer = {};
    indexBuffer = {};
    indexCount = 0;
}

rhi::VertexLayout meshVertexLayout() {
    rhi::VertexLayout l;
    l.stride = sizeof(MeshVertex);
    l.attributes = {
        {rhi::VertexFormat::Float3, offsetof(MeshVertex, position)},
        {rhi::VertexFormat::Float3, offsetof(MeshVertex, normal)},
        {rhi::VertexFormat::Float2, offsetof(MeshVertex, uv)},
        {rhi::VertexFormat::Float4, offsetof(MeshVertex, joints)},
        {rhi::VertexFormat::Float4, offsetof(MeshVertex, weights)},
    };
    return l;
}

namespace {

MeshVertex vert(Vec3 p, Vec3 n, Vec2 uv) {
    MeshVertex v;
    v.position = p;
    v.normal = n;
    v.uv = uv;
    return v;
}

// Adds a quad from four corners (counter-clockwise when viewed from the front).
void quad(MeshData& m, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 n) {
    uint32_t base = static_cast<uint32_t>(m.vertices.size());
    m.vertices.push_back(vert(a, n, {0, 0}));
    m.vertices.push_back(vert(b, n, {1, 0}));
    m.vertices.push_back(vert(c, n, {1, 1}));
    m.vertices.push_back(vert(d, n, {0, 1}));
    m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

void revolve(MeshData& m, int segments, const std::vector<Vec2>& profile, const std::vector<Vec2>& normals) {
    // Surface of revolution around Y: profile points are (radius, height).
    uint32_t base = static_cast<uint32_t>(m.vertices.size());
    int rows = static_cast<int>(profile.size());
    for (int r = 0; r < rows; ++r)
        for (int s = 0; s <= segments; ++s) {
            float a = 2 * kPi * s / segments;
            float ca = std::cos(a), sa = std::sin(a);
            Vec3 p{profile[r].x * ca, profile[r].y, profile[r].x * -sa};
            Vec3 n = normalize(Vec3(normals[r].x * ca, normals[r].y, normals[r].x * -sa));
            m.vertices.push_back(vert(p, n, {static_cast<float>(s) / segments, static_cast<float>(r) / (rows - 1)}));
        }
    for (int r = 0; r + 1 < rows; ++r)
        for (int s = 0; s < segments; ++s) {
            uint32_t i0 = base + static_cast<uint32_t>(r * (segments + 1) + s);
            uint32_t i1 = i0 + 1;
            uint32_t i2 = i0 + static_cast<uint32_t>(segments + 1);
            uint32_t i3 = i2 + 1;
            m.indices.insert(m.indices.end(), {i0, i1, i3, i0, i3, i2});
        }
}

void disc(MeshData& m, int segments, float radius, float y, bool up) {
    uint32_t center = static_cast<uint32_t>(m.vertices.size());
    Vec3 n{0, up ? 1.0f : -1.0f, 0};
    m.vertices.push_back(vert({0, y, 0}, n, {0.5f, 0.5f}));
    for (int s = 0; s <= segments; ++s) {
        float a = 2 * kPi * s / segments;
        m.vertices.push_back(vert({radius * std::cos(a), y, -radius * std::sin(a)}, n,
                                  {0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a)}));
    }
    for (int s = 0; s < segments; ++s) {
        uint32_t a = center + 1 + static_cast<uint32_t>(s), b = a + 1;
        if (up)
            m.indices.insert(m.indices.end(), {center, a, b});
        else
            m.indices.insert(m.indices.end(), {center, b, a});
    }
}

} // namespace

MeshData makeCube() {
    MeshData m;
    float h = 0.5f;
    quad(m, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}, {0, 0, 1});      // front (+Z)
    quad(m, {h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}, {0, 0, -1}); // back
    quad(m, {h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}, {1, 0, 0});      // right
    quad(m, {-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}, {-1, 0, 0}); // left
    quad(m, {-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}, {0, 1, 0});      // top
    quad(m, {-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}, {0, -1, 0}); // bottom
    m.computeBounds();
    return m;
}

MeshData makePlane() {
    MeshData m;
    float h = 0.5f;
    quad(m, {-h, 0, h}, {h, 0, h}, {h, 0, -h}, {-h, 0, -h}, {0, 1, 0});
    m.computeBounds();
    return m;
}

MeshData makeSphere(int segments, int rings) {
    MeshData m;
    std::vector<Vec2> profile, normals;
    for (int r = 0; r <= rings; ++r) {
        float a = kPi * r / rings - kPi * 0.5f;
        Vec2 p{0.5f * std::cos(a), 0.5f * std::sin(a)};
        profile.push_back(p);
        normals.push_back(p * 2.0f);
    }
    revolve(m, segments, profile, normals);
    m.computeBounds();
    return m;
}

MeshData makeCylinder(int segments) {
    MeshData m;
    revolve(m, segments, {{0.5f, -0.5f}, {0.5f, 0.5f}}, {{1, 0}, {1, 0}});
    disc(m, segments, 0.5f, 0.5f, true);
    disc(m, segments, 0.5f, -0.5f, false);
    m.computeBounds();
    return m;
}

MeshData makeCone(int segments) {
    MeshData m;
    float slope = 0.5f / 1.0f;
    Vec2 n = normalize(Vec2(1.0f, slope));
    revolve(m, segments, {{0.5f, -0.5f}, {0.0001f, 0.5f}}, {n, n});
    disc(m, segments, 0.5f, -0.5f, false);
    m.computeBounds();
    return m;
}

MeshData makeCapsule(int segments, int rings) {
    // Height 2, radius 0.5 (like Unity's capsule).
    MeshData m;
    std::vector<Vec2> profile, normals;
    for (int r = 0; r <= rings; ++r) {
        float a = -kPi * 0.5f + (kPi * 0.5f) * r / rings;
        profile.push_back({0.5f * std::cos(a), -0.5f + 0.5f * std::sin(a)});
        normals.push_back({std::cos(a), std::sin(a)});
    }
    for (int r = 0; r <= rings; ++r) {
        float a = (kPi * 0.5f) * r / rings;
        profile.push_back({0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a)});
        normals.push_back({std::cos(a), std::sin(a)});
    }
    revolve(m, segments, profile, normals);
    m.computeBounds();
    return m;
}

MeshData makeTorus(int segments, int sides) {
    MeshData m;
    float R = 0.35f, r = 0.15f;
    for (int i = 0; i <= segments; ++i) {
        float u = 2 * kPi * i / segments;
        for (int j = 0; j <= sides; ++j) {
            float v = 2 * kPi * j / sides;
            Vec3 center{R * std::cos(u), 0, -R * std::sin(u)};
            Vec3 n{std::cos(v) * std::cos(u), std::sin(v), -std::cos(v) * std::sin(u)};
            m.vertices.push_back(vert(center + n * r, n, {static_cast<float>(i) / segments, static_cast<float>(j) / sides}));
        }
    }
    for (int i = 0; i < segments; ++i)
        for (int j = 0; j < sides; ++j) {
            uint32_t a = static_cast<uint32_t>(i * (sides + 1) + j), b = a + static_cast<uint32_t>(sides + 1);
            m.indices.insert(m.indices.end(), {a, b, b + 1, a, b + 1, a + 1});
        }
    m.computeBounds();
    return m;
}

MeshData makePrimitive(MeshShape shape) {
    switch (shape) {
    case MeshShape::Cube: return makeCube();
    case MeshShape::Sphere: return makeSphere();
    case MeshShape::Plane: return makePlane();
    case MeshShape::Cylinder: return makeCylinder();
    case MeshShape::Capsule: return makeCapsule();
    case MeshShape::Cone: return makeCone();
    case MeshShape::Torus: return makeTorus();
    case MeshShape::Model: return makeCube();
    }
    return makeCube();
}

} // namespace aven
