/* A native module used by tests/test_native.cpp: exercises the C API from real C code. */

#include "aven.h"

#include <string.h>

typedef struct {
    float speed;
    int32_t bounces;
    int32_t loud;
    float total;
} Mover;

static void mover_start(AvenEntity self, void* data) {
    (void)self;
    (void)data;
    aven_game_set("started", aven_game_get("started", 0) + 1);
}

static void mover_update(AvenEntity self, void* data, float dt) {
    Mover* m = (Mover*)data;
    aven_set(self, "x", aven_get(self, "x") + m->speed * dt);
    m->total += dt;
}

static void mover_collide(AvenEntity self, void* data, AvenEntity other) {
    (void)self;
    (void)data;
    if (aven_exists(other))
        aven_game_set("hits", aven_game_get("hits", 0) + 1);
}

static void mover_message(AvenEntity self, void* data, const char* message, double value) {
    (void)self;
    Mover* m = (Mover*)data;
    if (strcmp(message, "boost") == 0)
        m->speed += (float)value;
}

static void finder_start(AvenEntity self, void* data) {
    (void)data;
    AvenEntity target = aven_find("Target");
    if (target)
        aven_set(target, "y", 5);
    aven_game_set("target_is_goal", strcmp(aven_tag(target), "goal") == 0);
    AvenEntity found[4];
    aven_game_set("goals", aven_find_all("goal", found, 4));
    aven_set_text(self, "name", "Finder (done)");
    aven_set(self, "nonsense_property", 1); /* reports an error, but carries on */
    aven_send(0, "hello", 7);             /* everyone, EasyScript included */
}

static void setup(AvenModule* module) {
    AvenBehavior* mover = aven_behavior(module, "Mover", sizeof(Mover));
    aven_number(mover, "speed", offsetof(Mover, speed), 2, "Units per second");
    aven_integer(mover, "bounces", offsetof(Mover, bounces), 3, NULL);
    aven_flag(mover, "loud", offsetof(Mover, loud), 1, NULL);
    mover->on_start = mover_start;
    mover->on_update = mover_update;
    mover->on_collide = mover_collide;
    mover->on_message = mover_message;

    AvenBehavior* finder = aven_behavior(module, "Finder", 0);
    finder->on_start = finder_start;
}

AVEN_MODULE(setup)
