#pragma once

// Pathfinding: find_path() and the Navigator behavior. A* on a grid laid over the world, worked
// out lazily: only the cells the search visits are checked, so there's nothing to build and no
// size limit.
//
// 2D (top-down): a cell is blocked when a circle the size of the walker overlaps a collider that
// doesn't move (walls, tilemap tiles). 3D: a ray down finds the ground in each cell; steep ground
// and steps taller than `stepHeight` block the way, so walls and cliffs are avoided without any
// setup.

#include "aven/math/math.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace aven {

class Game;

struct PathOptions {
    bool threeD = false;
    float radius = 0.4f;      // how wide the walker is (keeps paths off walls)
    float cellSize = 0.5f;    // grid spacing
    float stepHeight = 0.45f; // 3D: tallest step it can climb
    float maxSlope = 45.0f;   // 3D: steepest ground it can walk on, in degrees
    int maxCells = 40000;     // gives up after looking at this many cells
};

class Navigation {
public:
    explicit Navigation(Game& game) : game_(game) {}

    // The way from `from` to `to` as points to walk through (the first is `from`), or empty if
    // there's none. If `to` is inside a wall, the path ends at the nearest reachable spot.
    // `reached` (optional) says whether the path gets all the way.
    std::vector<Vec3> findPath(Vec3 from, Vec3 to, const PathOptions& options, bool* reached = nullptr);

    // Forget what's known about the world. Happens by itself when a wall or floor is made or
    // removed, and every few seconds (for walls moved by scripts); refresh_paths() calls it.
    void clear() {
        cells_.clear();
        age_ = 0;
    }
    void update(float dt);

private:
    struct Cell {
        bool walkable = false;
        float height = 0; // 3D: ground height
    };
    Game& game_;
    std::unordered_map<uint64_t, Cell> cells_;
    PathOptions cachedFor_;
    float age_ = 0;
    uint64_t staticChanges_ = 0;
    float probeTop_ = 0; // 3D: where the downward rays start
    uint64_t frame_ = 0, lastSyncFrame_ = ~0ull;

    const Cell& cell(int x, int y, const PathOptions& o);
    bool canStep(const Cell& a, const Cell& b, const PathOptions& o) const;
    bool lineClear(int x0, int y0, int x1, int y1, const PathOptions& o);
};

} // namespace aven
