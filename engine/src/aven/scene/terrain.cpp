#include "aven/scene/terrain.h"

#include "aven/render/mesh.h"

#include <algorithm>
#include <cmath>

namespace aven {

namespace {

int res(const Terrain& t) { return std::clamp(t.resolution, 2, 1025); }

// Grid coordinates (fractional) of a point in the terrain's space.
void toGrid(const Terrain& t, float x, float z, float& gx, float& gz) {
    int n = res(t);
    gx = (x / std::max(t.size.x, 0.01f) + 0.5f) * static_cast<float>(n - 1);
    gz = (z / std::max(t.size.y, 0.01f) + 0.5f) * static_cast<float>(n - 1);
}

float gridHeight(const Terrain& t, int ix, int iz) {
    int n = res(t);
    ix = std::clamp(ix, 0, n - 1);
    iz = std::clamp(iz, 0, n - 1);
    size_t i = static_cast<size_t>(iz) * n + ix;
    return i < t.heights.size() ? t.heights[i] : 0.0f;
}

float hash(int x, int y, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

float valueNoise(float x, float y, uint32_t seed) {
    int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    float fx = x - static_cast<float>(ix), fy = y - static_cast<float>(iy);
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    float a = hash(ix, iy, seed), b = hash(ix + 1, iy, seed), c = hash(ix, iy + 1, seed), d = hash(ix + 1, iy + 1, seed);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

} // namespace

void terrainEnsure(Terrain& t) {
    t.resolution = std::clamp(t.resolution, 9, 513);
    int n = t.resolution;
    size_t count = static_cast<size_t>(n) * n;
    if (t.layers.empty())
        t.layers.push_back({"", {0.36f, 0.62f, 0.3f, 1}, 4.0f});
    if (t.layers.size() > 4)
        t.layers.resize(4);
    if (t.heights.size() != count) {
        // A different resolution: resample what's there.
        int old = static_cast<int>(std::lround(std::sqrt(static_cast<double>(t.heights.size()))));
        std::vector<float> h(count, 0.0f);
        if (old >= 2 && static_cast<size_t>(old) * old == t.heights.size()) {
            Terrain src = t;
            src.resolution = old;
            for (int z = 0; z < n; ++z)
                for (int x = 0; x < n; ++x) {
                    float fx = static_cast<float>(x) / static_cast<float>(n - 1) * static_cast<float>(old - 1);
                    float fz = static_cast<float>(z) / static_cast<float>(n - 1) * static_cast<float>(old - 1);
                    int x0 = static_cast<int>(fx), z0 = static_cast<int>(fz);
                    float ax = fx - static_cast<float>(x0), az = fz - static_cast<float>(z0);
                    float a = gridHeight(src, x0, z0), b = gridHeight(src, x0 + 1, z0);
                    float c = gridHeight(src, x0, z0 + 1), d = gridHeight(src, x0 + 1, z0 + 1);
                    h[static_cast<size_t>(z) * n + x] = (a + (b - a) * ax) * (1 - az) + (c + (d - c) * ax) * az;
                }
        }
        t.heights = std::move(h);
        ++t.revision;
    }
    if (t.splat.size() != count * 4) {
        int old = static_cast<int>(std::lround(std::sqrt(static_cast<double>(t.splat.size() / 4))));
        std::vector<uint8_t> s(count * 4, 0);
        for (size_t i = 0; i < count; ++i)
            s[i * 4] = 255;
        if (old >= 2 && static_cast<size_t>(old) * old * 4 == t.splat.size())
            for (int z = 0; z < n; ++z)
                for (int x = 0; x < n; ++x) {
                    int ox = x * (old - 1) / (n - 1), oz = z * (old - 1) / (n - 1);
                    for (int c = 0; c < 4; ++c)
                        s[(static_cast<size_t>(z) * n + x) * 4 + c] = t.splat[(static_cast<size_t>(oz) * old + ox) * 4 + c];
                }
        t.splat = std::move(s);
        ++t.revision;
    }
}

float terrainHeightAt(const Terrain& t, float x, float z) {
    float gx, gz;
    toGrid(t, x, z, gx, gz);
    int n = res(t);
    gx = std::clamp(gx, 0.0f, static_cast<float>(n - 1));
    gz = std::clamp(gz, 0.0f, static_cast<float>(n - 1));
    int x0 = std::min(static_cast<int>(gx), n - 2), z0 = std::min(static_cast<int>(gz), n - 2);
    float ax = gx - static_cast<float>(x0), az = gz - static_cast<float>(z0);
    float a = gridHeight(t, x0, z0), b = gridHeight(t, x0 + 1, z0), c = gridHeight(t, x0, z0 + 1), d = gridHeight(t, x0 + 1, z0 + 1);
    // The same two triangles per cell as the mesh, so things sit exactly on it.
    float h = ax + az <= 1.0f ? a + (b - a) * ax + (c - a) * az : d + (c - d) * (1 - ax) + (b - d) * (1 - az);
    return h * t.maxHeight;
}

Vec3 terrainNormalAt(const Terrain& t, float x, float z) {
    int n = res(t);
    float sx = t.size.x / static_cast<float>(n - 1), sz = t.size.y / static_cast<float>(n - 1);
    float hl = terrainHeightAt(t, x - sx, z), hr = terrainHeightAt(t, x + sx, z);
    float hd = terrainHeightAt(t, x, z - sz), hu = terrainHeightAt(t, x, z + sz);
    return normalize(Vec3{(hl - hr) / (2 * sx), 1.0f, (hd - hu) / (2 * sz)});
}

bool terrainRaycast(const Terrain& t, Vec3 origin, Vec3 direction, float maxDistance, Vec3& hit) {
    if (lengthSquared(direction) < 1e-12f)
        return false;
    Vec3 d = normalize(direction);
    int n = res(t);
    float step = 0.5f * std::min(t.size.x, t.size.y) / static_cast<float>(n - 1);
    step = std::max(step, 0.02f);
    float hx = t.size.x * 0.5f, hz = t.size.y * 0.5f;
    auto inside = [&](Vec3 p) { return p.x >= -hx && p.x <= hx && p.z >= -hz && p.z <= hz; };
    auto above = [&](Vec3 p) { return p.y > terrainHeightAt(t, p.x, p.z); };
    // Skip ahead to where the ray enters the terrain's box.
    float start = 0, end = maxDistance;
    float lo[3] = {-hx, -1.0f, -hz}, hi[3] = {hx, t.maxHeight + 1.0f, hz};
    float o[3] = {origin.x, origin.y, origin.z}, dv[3] = {d.x, d.y, d.z};
    for (int a = 0; a < 3; ++a) {
        if (std::abs(dv[a]) < 1e-9f) {
            if (o[a] < lo[a] || o[a] > hi[a])
                return false;
            continue;
        }
        float t0 = (lo[a] - o[a]) / dv[a], t1 = (hi[a] - o[a]) / dv[a];
        if (t0 > t1)
            std::swap(t0, t1);
        start = std::max(start, t0);
        end = std::min(end, t1);
    }
    if (start > end)
        return false;
    Vec3 prev = origin + d * start;
    bool wasAbove = !inside(prev) || above(prev);
    for (float s = start + step; s <= end + step; s += step) {
        Vec3 p = origin + d * std::min(s, end);
        if (inside(p)) {
            bool up = above(p);
            if (wasAbove && !up) {
                // Narrow it down between the last two samples.
                Vec3 a = prev, b = p;
                for (int i = 0; i < 16; ++i) {
                    Vec3 m = (a + b) * 0.5f;
                    (above(m) ? a : b) = m;
                }
                hit = (a + b) * 0.5f;
                return true;
            }
            wasAbove = up;
        } else {
            wasAbove = true;
        }
        prev = p;
    }
    return false;
}

void terrainBuildMesh(const Terrain& t, MeshData& mesh) {
    int n = res(t);
    mesh.vertices.assign(static_cast<size_t>(n) * n, MeshVertex{});
    mesh.indices.clear();
    mesh.indices.reserve(static_cast<size_t>(n - 1) * (n - 1) * 6);
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            size_t i = static_cast<size_t>(z) * n + x;
            float px = (static_cast<float>(x) / static_cast<float>(n - 1) - 0.5f) * t.size.x;
            float pz = (static_cast<float>(z) / static_cast<float>(n - 1) - 0.5f) * t.size.y;
            MeshVertex& v = mesh.vertices[i];
            v.position = {px, gridHeight(t, x, z) * t.maxHeight, pz};
            v.normal = terrainNormalAt(t, px, pz);
            v.uv = {static_cast<float>(x) / static_cast<float>(n - 1), static_cast<float>(z) / static_cast<float>(n - 1)};
            float w[4] = {1, 0, 0, 0}, sum = 0;
            if (i * 4 + 3 < t.splat.size())
                for (int c = 0; c < 4; ++c)
                    sum += (w[c] = static_cast<float>(t.splat[i * 4 + c]) / 255.0f);
            for (int c = 0; c < 4; ++c)
                v.weights[c] = sum > 0 ? w[c] / sum : (c == 0 ? 1.0f : 0.0f);
        }
    for (int z = 0; z + 1 < n; ++z)
        for (int x = 0; x + 1 < n; ++x) {
            uint32_t a = static_cast<uint32_t>(z * n + x), b = a + 1, c = a + static_cast<uint32_t>(n), d = c + 1;
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d}); // counter-clockwise from above
        }
    mesh.computeBounds();
}

void terrainBrush(Terrain& t, TerrainTool tool, float x, float z, float radius, float strength, float dt, int layer,
                  float level) {
    terrainEnsure(t);
    int n = t.resolution;
    radius = std::max(radius, 0.01f);
    float gx, gz;
    toGrid(t, x, z, gx, gz);
    float cellX = t.size.x / static_cast<float>(n - 1), cellZ = t.size.y / static_cast<float>(n - 1);
    int rx = static_cast<int>(std::ceil(radius / cellX)) + 1, rz = static_cast<int>(std::ceil(radius / cellZ)) + 1;
    int cx = static_cast<int>(std::lround(gx)), cz = static_cast<int>(std::lround(gz));
    std::vector<float> before;
    if (tool == TerrainTool::Smooth)
        before = t.heights;
    layer = std::clamp(layer, 0, 3);
    for (int iz = std::max(0, cz - rz); iz <= std::min(n - 1, cz + rz); ++iz)
        for (int ix = std::max(0, cx - rx); ix <= std::min(n - 1, cx + rx); ++ix) {
            float px = (static_cast<float>(ix) / static_cast<float>(n - 1) - 0.5f) * t.size.x;
            float pz = (static_cast<float>(iz) / static_cast<float>(n - 1) - 0.5f) * t.size.y;
            float dist = std::sqrt((px - x) * (px - x) + (pz - z) * (pz - z));
            if (dist > radius)
                continue;
            float f = 1.0f - (dist / radius) * (dist / radius);
            f *= f; // soft edge
            size_t i = static_cast<size_t>(iz) * n + ix;
            float& h = t.heights[i];
            float amount = strength * dt * f;
            switch (tool) {
            case TerrainTool::Raise: h = std::min(1.0f, h + amount * 0.5f); break;
            case TerrainTool::Lower: h = std::max(0.0f, h - amount * 0.5f); break;
            case TerrainTool::Flatten: h += (level - h) * std::min(1.0f, amount * 6.0f); break;
            case TerrainTool::Smooth: {
                float sum = 0;
                int count = 0;
                for (int oz = -1; oz <= 1; ++oz)
                    for (int ox = -1; ox <= 1; ++ox) {
                        int sx = std::clamp(ix + ox, 0, n - 1), sz = std::clamp(iz + oz, 0, n - 1);
                        sum += before[static_cast<size_t>(sz) * n + sx];
                        ++count;
                    }
                h += (sum / static_cast<float>(count) - h) * std::min(1.0f, amount * 8.0f);
                break;
            }
            case TerrainTool::Paint: {
                float w[4];
                for (int c = 0; c < 4; ++c)
                    w[c] = static_cast<float>(t.splat[i * 4 + c]) / 255.0f;
                float target = std::min(1.0f, w[layer] + amount * 4.0f);
                float others = 0;
                for (int c = 0; c < 4; ++c)
                    if (c != layer)
                        others += w[c];
                for (int c = 0; c < 4; ++c)
                    w[c] = c == layer ? target : (others > 1e-5f ? w[c] / others * (1.0f - target) : 0.0f);
                for (int c = 0; c < 4; ++c)
                    t.splat[i * 4 + c] = static_cast<uint8_t>(std::lround(std::clamp(w[c], 0.0f, 1.0f) * 255.0f));
                break;
            }
            }
        }
    ++t.revision;
}

void terrainAutoPaint(Terrain& t) {
    terrainEnsure(t);
    int n = t.resolution;
    size_t layers = t.layers.size();
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            float px = (static_cast<float>(x) / static_cast<float>(n - 1) - 0.5f) * t.size.x;
            float pz = (static_cast<float>(z) / static_cast<float>(n - 1) - 0.5f) * t.size.y;
            float slope = 1.0f - terrainNormalAt(t, px, pz).y;
            float height = t.heights[static_cast<size_t>(z) * n + x];
            auto ramp = [](float v, float a, float b) { return std::clamp((v - a) / (b - a), 0.0f, 1.0f); };
            float w[4] = {1, 0, 0, 0};
            if (layers > 1)
                w[1] = ramp(slope, 0.06f, 0.14f);
            if (layers > 2)
                w[2] = ramp(slope, 0.2f, 0.32f);
            if (layers > 3)
                w[3] = ramp(height, 0.65f, 0.8f);
            // Later layers cover earlier ones.
            float left = 1.0f, out[4] = {0, 0, 0, 0};
            for (int c = 3; c >= 1; --c) {
                out[c] = w[c] * left;
                left -= out[c];
            }
            out[0] = left;
            size_t i = static_cast<size_t>(z) * n + x;
            for (int c = 0; c < 4; ++c)
                t.splat[i * 4 + c] = static_cast<uint8_t>(std::lround(out[c] * 255.0f));
        }
    ++t.revision;
}

void terrainGenerateHills(Terrain& t, float amount, uint32_t seed) {
    terrainEnsure(t);
    int n = t.resolution;
    // Features about 20 m across, whatever the resolution.
    float scaleX = t.size.x / 20.0f, scaleZ = t.size.y / 20.0f;
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            float u = static_cast<float>(x) / static_cast<float>(n - 1) * scaleX;
            float v = static_cast<float>(z) / static_cast<float>(n - 1) * scaleZ;
            float h = 0, amp = 0.5f, freq = 1.0f;
            for (int o = 0; o < 5; ++o) {
                h += valueNoise(u * freq, v * freq, seed + static_cast<uint32_t>(o) * 17u) * amp;
                amp *= 0.5f;
                freq *= 2.0f;
            }
            t.heights[static_cast<size_t>(z) * n + x] = std::clamp(h * amount, 0.0f, 1.0f);
        }
    ++t.revision;
}

} // namespace aven
