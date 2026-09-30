// A native behavior in C++: walks back and forth, and turns around at walls or at the edge of its
// patrol. C++ uses the same C API; wrap it in classes if you like.

#include "rynax.h"

#include <cmath>

namespace {

struct Patrol {
    float speed;    // Inspector
    float distance; // Inspector: how far from the start it walks
    float startX;
    float direction;

    void start(RynaxEntity self) {
        startX = static_cast<float>(rynax_get(self, "x"));
        direction = 1;
    }

    void update(RynaxEntity self, float dt) {
        double x = rynax_get(self, "x") + direction * speed * dt;
        if (std::fabs(x - startX) > distance)
            turn(self);
        rynax_set(self, "x", x);
    }

    void turn(RynaxEntity self) {
        direction = -direction;
        rynax_set(self, "flip_x", direction < 0 ? 1 : 0);
    }
};

void setup(RynaxModule* module) {
    RynaxBehavior* b = rynax_behavior(module, "Patrol", sizeof(Patrol));
    rynax_number(b, "speed", offsetof(Patrol, speed), 2, "Units per second");
    rynax_number(b, "distance", offsetof(Patrol, distance), 3, "How far it walks each way");
    b->on_start = [](RynaxEntity self, void* data) { static_cast<Patrol*>(data)->start(self); };
    b->on_update = [](RynaxEntity self, void* data, float dt) { static_cast<Patrol*>(data)->update(self, dt); };
    b->on_collide = [](RynaxEntity self, void* data, RynaxEntity) { static_cast<Patrol*>(data)->turn(self); };
}

} // namespace

RYNAX_MODULE(setup)
