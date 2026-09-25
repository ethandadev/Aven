/*
 * Native behaviors for this game, written in C.
 *
 * To use one: select an object, Add Component > NativeScript, and pick the behavior.
 * After changing this file, press Build in Tools > Native Code; Aven reloads it for you.
 *
 * include/aven.h lists everything you can call. The names match EasyScript, so
 *     self.y += 1                  is    aven_set(self, "y", aven_get(self, "y") + 1);
 *     game.coins += 1              is    aven_game_set("coins", aven_game_get("coins", 0) + 1);
 */

#include "aven.h"

#include <math.h>
#include <string.h>

/* ---- Spinner: turns around and around. -------------------------------------------------- */

typedef struct {
    float speed; /* degrees per second (set in the Inspector) */
} Spinner;

static void spinner_update(AvenEntity self, void* data, float dt) {
    Spinner* s = (Spinner*)data;
    aven_set(self, "angle", aven_get(self, "angle") + s->speed * dt);
}

/* ---- Bobber: floats up and down, like a coin waiting to be picked up. ------------------- */

typedef struct {
    float height; /* set in the Inspector */
    float speed;
    float start_y; /* remembered by the behavior itself */
    float time;
} Bobber;

static void bobber_start(AvenEntity self, void* data) {
    Bobber* b = (Bobber*)data;
    b->start_y = (float)aven_get(self, "y");
}

static void bobber_update(AvenEntity self, void* data, float dt) {
    Bobber* b = (Bobber*)data;
    b->time += dt;
    aven_set(self, "y", b->start_y + sinf(b->time * b->speed) * b->height);
}

/* ---- Collector: picks up the coins it touches (objects tagged "coin"). ------------------ */

static void collector_collide(AvenEntity self, void* data, AvenEntity other) {
    (void)self;
    (void)data;
    if (strcmp(aven_tag(other), "coin") == 0) {
        aven_game_set("coins", aven_game_get("coins", 0) + 1);
        aven_destroy(other);
    }
}

/* ---- Tell Aven what's in this module. --------------------------------------------------- */

static void setup(AvenModule* module) {
    AvenBehavior* spinner = aven_behavior(module, "Spinner", sizeof(Spinner));
    aven_number(spinner, "speed", offsetof(Spinner, speed), 90, "Degrees per second");
    spinner->on_update = spinner_update;

    AvenBehavior* bobber = aven_behavior(module, "Bobber", sizeof(Bobber));
    aven_number(bobber, "height", offsetof(Bobber, height), 0.25, "How far up and down it floats");
    aven_number(bobber, "speed", offsetof(Bobber, speed), 3, "How fast it floats");
    bobber->on_start = bobber_start;
    bobber->on_update = bobber_update;

    AvenBehavior* collector = aven_behavior(module, "Collector", 0);
    collector->on_collide = collector_collide;
}

AVEN_MODULE(setup)
