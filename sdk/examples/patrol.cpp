// A native behavior in C++: walks back and forth, and turns around at walls or at the edge of its
// patrol. C++ uses the same C API; wrap it in classes if you like.

#include "aven.h"

#include <cmath>

namespace {

struct Patrol {
    float speed;    // Inspector
    float distance; // Inspector: how far from the start it walks
    float startX;
    float direction;

    void start(AvenEntity self) {
        startX = static_cast<float>(aven_get(self, "x"));
        direction = 1;
    }

    void update(AvenEntity self, float dt) {
        double x = aven_get(self, "x") + direction * speed * dt;
        if (std::fabs(x - startX) > distance)
            turn(self);
        aven_set(self, "x", x);
    }

    void turn(AvenEntity self) {
        direction = -direction;
        aven_set(self, "flip_x", direction < 0 ? 1 : 0);
    }
};

void setup(AvenModule* module) {
    AvenBehavior* b = aven_behavior(module, "Patrol", sizeof(Patrol));
    aven_number(b, "speed", offsetof(Patrol, speed), 2, "Units per second");
    aven_number(b, "distance", offsetof(Patrol, distance), 3, "How far it walks each way");
    b->on_start = [](AvenEntity self, void* data) { static_cast<Patrol*>(data)->start(self); };
    b->on_update = [](AvenEntity self, void* data, float dt) { static_cast<Patrol*>(data)->update(self, dt); };
    b->on_collide = [](AvenEntity self, void* data, AvenEntity) { static_cast<Patrol*>(data)->turn(self); };
}

} // namespace

AVEN_MODULE(setup)
