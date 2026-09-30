/*
 * Native behaviors for this game, written in C.
 *
 * To use one: select an object, Add Component > NativeScript, and pick the behavior.
 * After changing this file, press Build in Tools > Native Code; Rynax reloads it for you.
 *
 * include/rynax.h lists everything you can call. The names match EasyScript, so
 *     self.y += 1                  is    rynax_set(self, "y", rynax_get(self, "y") + 1);
 *     game.coins += 1              is    rynax_game_set("coins", rynax_game_get("coins", 0) + 1);
 */

#include "rynax.h"

#include <math.h>
#include <string.h>

/* ---- Spinner: turns around and around. -------------------------------------------------- */

typedef struct {
    float speed; /* degrees per second (set in the Inspector) */
} Spinner;

static void spinner_update(RynaxEntity self, void* data, float dt) {
    Spinner* s = (Spinner*)data;
    rynax_set(self, "angle", rynax_get(self, "angle") + s->speed * dt);
}

/* ---- Bobber: floats up and down, like a coin waiting to be picked up. ------------------- */

typedef struct {
    float height; /* set in the Inspector */
    float speed;
    float start_y; /* remembered by the behavior itself */
    float time;
} Bobber;

static void bobber_start(RynaxEntity self, void* data) {
    Bobber* b = (Bobber*)data;
    b->start_y = (float)rynax_get(self, "y");
}

static void bobber_update(RynaxEntity self, void* data, float dt) {
    Bobber* b = (Bobber*)data;
    b->time += dt;
    rynax_set(self, "y", b->start_y + sinf(b->time * b->speed) * b->height);
}

/* ---- Collector: picks up the coins it touches (objects tagged "coin"). ------------------ */

static void collector_collide(RynaxEntity self, void* data, RynaxEntity other) {
    (void)self;
    (void)data;
    if (strcmp(rynax_tag(other), "coin") == 0) {
        rynax_game_set("coins", rynax_game_get("coins", 0) + 1);
        rynax_destroy(other);
    }
}

/* ---- Tell Rynax what's in this module. --------------------------------------------------- */

static void setup(RynaxModule* module) {
    RynaxBehavior* spinner = rynax_behavior(module, "Spinner", sizeof(Spinner));
    rynax_number(spinner, "speed", offsetof(Spinner, speed), 90, "Degrees per second");
    spinner->on_update = spinner_update;

    RynaxBehavior* bobber = rynax_behavior(module, "Bobber", sizeof(Bobber));
    rynax_number(bobber, "height", offsetof(Bobber, height), 0.25, "How far up and down it floats");
    rynax_number(bobber, "speed", offsetof(Bobber, speed), 3, "How fast it floats");
    bobber->on_start = bobber_start;
    bobber->on_update = bobber_update;

    RynaxBehavior* collector = rynax_behavior(module, "Collector", 0);
    collector->on_collide = collector_collide;
}

RYNAX_MODULE(setup)
