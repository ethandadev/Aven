/* A module written for Aven (Rynax's old name), with the old header: it must keep loading. */

#include "aven.h"

typedef struct {
    float speed;
} Old;

static void old_start(AvenEntity self, void* data) {
    (void)self;
    aven_game_set("old_speed", ((Old*)data)->speed);
}

static void setup(AvenModule* module) {
    AvenBehavior* b = aven_behavior(module, "OldTimer", sizeof(Old));
    aven_number(b, "speed", offsetof(Old, speed), 3.5, "made with aven.h");
    b->on_start = old_start;
}

AVEN_MODULE(setup)
