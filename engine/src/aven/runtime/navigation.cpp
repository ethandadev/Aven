#include "aven/runtime/navigation.h"

#include "aven/runtime/game.h"
#include "aven/runtime/systems.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace aven {

namespace {

uint64_t key(int x, int y) { return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(y); }

bool sameOptions(const PathOptions& a, const PathOptions& b) {
    return a.threeD == b.threeD && a.radius == b.radius && a.cellSize == b.cellSize && a.stepHeight == b.stepHeight &&
           a.maxSlope == b.maxSlope;
}

} // namespace

void Navigation::update(float dt) {
    ++frame_;
    // Walls can move or be destroyed: look again now and then.
    age_ += dt;
    if (age_ > 3.0f) {
        age_ = 0;
        cells_.clear();
    }
}

const Navigation::Cell& Navigation::cell(int x, int y, const PathOptions& o) {
    auto it = cells_.find(key(x, y));
    if (it != cells_.end())
        return it->second;
    Cell c;
    float cx = (static_cast<float>(x) + 0.5f) * o.cellSize, cy = (static_cast<float>(y) + 0.5f) * o.cellSize;
    if (!o.threeD) {
        c.walkable = !game_.physics2D().blockedAt({cx, cy}, o.radius);
    } else {
        Physics3D& physics = game_.physics3D();
        float minUp = std::cos(o.maxSlope * 3.14159265f / 180.0f);
        auto ground = [&](float px, float pz, float& h, bool& flat) {
            RayHit hit;
            if (!physics.raycastStatic({px, probeTop_, pz}, {0, -1, 0}, 200.0f, hit))
                return false;
            h = hit.point.y;
            flat = hit.distance > 0.001f && hit.normal.y >= minUp; // a ray that starts inside a wall: not ground
            return true;
        };
        bool flat = false;
        if (ground(cx, cy, c.height, flat) && flat) {
            c.walkable = true;
            // Room to stand: nothing just overhead...
            RayHit above;
            if (physics.raycastStatic({cx, c.height + 0.1f, cy}, {0, 1, 0}, 1.5f, above))
                c.walkable = false;
            // ...and no wall or drop within the walker's radius.
            const float r = o.radius;
            const float offsets[4][2] = {{r, 0}, {-r, 0}, {0, r}, {0, -r}};
            for (auto& off : offsets) {
                float h = 0;
                bool f = false;
                if (!c.walkable)
                    break;
                if (!ground(cx + off[0], cy + off[1], h, f) || std::abs(h - c.height) > o.stepHeight)
                    c.walkable = false;
            }
        }
    }
    return cells_.emplace(key(x, y), c).first->second;
}

bool Navigation::canStep(const Cell& a, const Cell& b, const PathOptions& o) const {
    return a.walkable && b.walkable && (!o.threeD || std::abs(a.height - b.height) <= o.stepHeight);
}

// Can it walk straight from one cell to the other? Samples the line at quarter-cell steps.
bool Navigation::lineClear(int x0, int y0, int x1, int y1, const PathOptions& o) {
    float dx = static_cast<float>(x1 - x0), dy = static_cast<float>(y1 - y0);
    int steps = static_cast<int>(std::ceil(std::max(std::abs(dx), std::abs(dy)) * 4.0f));
    int px = x0, py = y0;
    for (int i = 1; i <= steps; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(steps);
        int cx = static_cast<int>(std::floor(static_cast<float>(x0) + 0.5f + dx * t));
        int cy = static_cast<int>(std::floor(static_cast<float>(y0) + 0.5f + dy * t));
        if (cx == px && cy == py)
            continue;
        // Moving diagonally between cells must not cut a corner.
        if (cx != px && cy != py && (!cell(cx, py, o).walkable || !cell(px, cy, o).walkable))
            return false;
        if (!canStep(cell(px, py, o), cell(cx, cy, o), o))
            return false;
        px = cx;
        py = cy;
    }
    return true;
}

std::vector<Vec3> Navigation::findPath(Vec3 from, Vec3 to, const PathOptions& o, bool* reached) {
    if (reached)
        *reached = false;
    if (!sameOptions(o, cachedFor_)) {
        cells_.clear();
        cachedFor_ = o;
    }
    // Colliders made this frame (or before the first physics step) count too.
    if (lastSyncFrame_ != frame_) {
        lastSyncFrame_ = frame_;
        if (o.threeD)
            game_.physics3D().sync();
        else
            game_.physics2D().sync();
    }
    // Plane coordinates: x/y in 2D, x/z in 3D.
    auto plane = [&](Vec3 p) { return o.threeD ? Vec2{p.x, p.z} : Vec2{p.x, p.y}; };
    if (o.threeD) {
        float top = std::max(from.y, to.y) + 2.0f;
        if (std::abs(top - probeTop_) > 1.0f) {
            cells_.clear();
            probeTop_ = top;
        }
    }
    const float cs = std::max(0.05f, o.cellSize);
    auto toCell = [&](Vec2 p) { return std::pair<int, int>{static_cast<int>(std::floor(p.x / cs)), static_cast<int>(std::floor(p.y / cs))}; };
    auto [sx, sy] = toCell(plane(from));
    auto [tx, ty] = toCell(plane(to));
    bool exactTarget = cell(tx, ty, o).walkable;
    if (!exactTarget) {
        // The target is in a wall (or off the ground): aim for the nearest cell it could stand on.
        bool found = false;
        for (int r = 1; r <= 8 && !found; ++r)
            for (int dy = -r; dy <= r && !found; ++dy)
                for (int dx = -r; dx <= r && !found; ++dx)
                    if ((std::abs(dx) == r || std::abs(dy) == r) && cell(tx + dx, ty + dy, o).walkable) {
                        tx += dx;
                        ty += dy;
                        found = true;
                    }
    }

    // A*: 8 directions, no cutting corners.
    struct Node {
        float f;
        uint64_t k;
        bool operator>(const Node& n) const { return f > n.f; }
    };
    auto h = [&](int x, int y) {
        float dx = static_cast<float>(std::abs(x - tx)), dy = static_cast<float>(std::abs(y - ty));
        return (dx + dy) + (1.41421356f - 2.0f) * std::min(dx, dy);
    };
    auto unkey = [](uint64_t k) { return std::pair<int, int>{static_cast<int>(static_cast<uint32_t>(k >> 32)), static_cast<int>(static_cast<uint32_t>(k))}; };
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    std::unordered_map<uint64_t, float> g;
    std::unordered_map<uint64_t, uint64_t> cameFrom;
    uint64_t start = key(sx, sy), goal = key(tx, ty), best = start;
    float bestH = h(sx, sy);
    g[start] = 0;
    open.push({bestH, start});
    int visited = 0;
    bool done = false;
    while (!open.empty() && visited < o.maxCells) {
        Node n = open.top();
        open.pop();
        auto [x, y] = unkey(n.k);
        float gn = g[n.k];
        if (n.f > gn + h(x, y) + 1e-4f)
            continue; // an old entry
        ++visited;
        if (n.k == goal) {
            done = true;
            best = goal;
            break;
        }
        if (h(x, y) < bestH) {
            bestH = h(x, y);
            best = n.k;
        }
        const Cell here = cell(x, y, o);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy)
                    continue;
                const Cell& next = cell(x + dx, y + dy, o);
                // The start may overlap a wall a little (it's standing next to one): it can still leave.
                bool fromStart = n.k == start;
                if (!(fromStart ? next.walkable && (!o.threeD || std::abs(here.height - next.height) <= o.stepHeight || !here.walkable)
                                : canStep(here, next, o)))
                    continue;
                if (dx && dy && (!cell(x + dx, y, o).walkable || !cell(x, y + dy, o).walkable))
                    continue;
                float cost = dx && dy ? 1.41421356f : 1.0f;
                uint64_t nk = key(x + dx, y + dy);
                float tentative = gn + cost;
                auto it = g.find(nk);
                if (it == g.end() || tentative < it->second - 1e-5f) {
                    g[nk] = tentative;
                    cameFrom[nk] = n.k;
                    open.push({tentative + h(x + dx, y + dy), nk});
                }
            }
    }
    if (best == start && !done)
        return {};
    std::vector<uint64_t> cells{best};
    while (cells.back() != start)
        cells.push_back(cameFrom[cells.back()]);
    std::reverse(cells.begin(), cells.end());

    // Straighten it: from each point, jump to the furthest one in plain sight.
    std::vector<uint64_t> kept{cells.front()};
    size_t i = 0;
    while (i + 1 < cells.size()) {
        size_t j = cells.size() - 1;
        auto [ax, ay] = unkey(cells[i]);
        while (j > i + 1) {
            auto [bx, by] = unkey(cells[j]);
            if (lineClear(ax, ay, bx, by, o))
                break;
            --j;
        }
        kept.push_back(cells[j]);
        i = j;
    }

    // Back to world points. In 3D, points keep the walker's height above the ground.
    float lift = 0;
    if (o.threeD) {
        const Cell& s = cell(sx, sy, o);
        lift = s.walkable ? from.y - s.height : 0.0f;
    }
    std::vector<Vec3> path{from};
    for (size_t k = 1; k < kept.size(); ++k) {
        auto [x, y] = unkey(kept[k]);
        float px = (static_cast<float>(x) + 0.5f) * cs, py = (static_cast<float>(y) + 0.5f) * cs;
        bool last = k + 1 == kept.size();
        if (last && done && exactTarget) {
            path.push_back(to);
        } else if (o.threeD) {
            path.push_back({px, cell(x, y, o).height + lift, py});
        } else {
            path.push_back({px, py, from.z});
        }
    }
    if (path.size() == 1 && done)
        path.push_back(exactTarget ? to : path.front()); // already there
    if (reached)
        *reached = done;
    return path;
}

} // namespace aven
