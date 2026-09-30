#pragma once

// Terrain helpers shared by the renderer, physics, scripts and the editor's sculpt tools.
// Positions here are in the terrain's own space: x and z from -size/2 to size/2, y up.

#include "rynax/scene/components.h"

namespace rynax {

struct MeshData;

// Makes the height and paint grids the right size for `resolution` (new ground is flat and
// painted with the first layer), keeping what's there when it can.
void terrainEnsure(Terrain& t);
// Ground height (in the terrain's space) under a point, blended between grid points.
float terrainHeightAt(const Terrain& t, float x, float z);
Vec3 terrainNormalAt(const Terrain& t, float x, float z);
// Where a ray (in the terrain's space) meets the ground.
bool terrainRaycast(const Terrain& t, Vec3 origin, Vec3 direction, float maxDistance, Vec3& hit);
// The triangle mesh; `weights` carry the paint.
void terrainBuildMesh(const Terrain& t, MeshData& mesh);

enum class TerrainTool : int32_t { Raise, Lower, Smooth, Flatten, Paint };
// One dab of a brush at (x, z), with a soft edge. `strength` 0..1 per second; `dt` scales it.
// Flatten pulls toward `level` (0..1); Paint adds `layer`.
void terrainBrush(Terrain& t, TerrainTool tool, float x, float z, float radius, float strength, float dt, int layer,
                  float level);
// Rolling hills from noise, `amount` 0..1.
void terrainGenerateHills(Terrain& t, float amount, uint32_t seed);
// Paints by shape: layer 1 on slopes, layer 2 on steep ground, layer 3 up high (when there are
// that many layers), layer 1 everywhere else.
void terrainAutoPaint(Terrain& t);

} // namespace rynax
