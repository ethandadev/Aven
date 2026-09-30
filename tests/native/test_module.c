/* A native module used by tests/test_native.cpp: exercises the C API from real C code. */

#include "rynax.h"

#include <string.h>

typedef struct {
    float speed;
    int32_t bounces;
    int32_t loud;
    float total;
} Mover;

static void mover_start(RynaxEntity self, void* data) {
    (void)self;
    (void)data;
    rynax_game_set("started", rynax_game_get("started", 0) + 1);
}

static void mover_update(RynaxEntity self, void* data, float dt) {
    Mover* m = (Mover*)data;
    rynax_set(self, "x", rynax_get(self, "x") + m->speed * dt);
    m->total += dt;
}

static void mover_collide(RynaxEntity self, void* data, RynaxEntity other) {
    (void)self;
    (void)data;
    if (rynax_exists(other))
        rynax_game_set("hits", rynax_game_get("hits", 0) + 1);
}

static void mover_message(RynaxEntity self, void* data, const char* message, double value) {
    (void)self;
    Mover* m = (Mover*)data;
    if (strcmp(message, "boost") == 0)
        m->speed += (float)value;
}

static void finder_start(RynaxEntity self, void* data) {
    (void)data;
    RynaxEntity target = rynax_find("Target");
    if (target)
        rynax_set(target, "y", 5);
    rynax_game_set("target_is_goal", strcmp(rynax_tag(target), "goal") == 0);
    RynaxEntity found[4];
    rynax_game_set("goals", rynax_find_all("goal", found, 4));
    rynax_set_text(self, "name", "Finder (done)");
    rynax_set(self, "nonsense_property", 1); /* reports an error, but carries on */
    rynax_send(0, "hello", 7);             /* everyone, EasyScript included */
}

/* Destroys its own object in the middle of an update, then keeps using its data: that data must
   stay valid until the frame is over. */
typedef struct {
    int32_t ticks;
    float after;
} Fuse;

static void fuse_update(RynaxEntity self, void* data, float dt) {
    Fuse* f = (Fuse*)data;
    (void)dt;
    if (++f->ticks == 2) {
        rynax_destroy(self);
        f->after = 1;
        rynax_game_set("fuse_done", f->after + (float)f->ticks);
    }
}

static void fuse_destroyed(RynaxEntity self, void* data) {
    (void)self;
    ((Fuse*)data)->after = 2;
    rynax_game_set("fuse_destroyed", 1);
}

/* API version 2: parents and children, and calls with text arguments. */
static void family_start(RynaxEntity self, void* data) {
    (void)data;
    rynax_game_set("family_parent_ok", rynax_parent(self) == rynax_find("Home"));
    rynax_game_set("family_top_has_no_parent", rynax_parent(rynax_find("Home")) == 0);
    RynaxEntity kids[1];
    rynax_game_set("family_children", rynax_children(self, kids, 1)); /* all of them, though only one fits */
    RynaxEntity grandchild = rynax_find_child(self, "Grandchild");
    if (grandchild)
        rynax_set(grandchild, "x", 7);
    rynax_game_set("family_missing_child", rynax_find_child(self, "Nobody") == 0);
    RynaxArg name = rynax_text_arg("C");
    rynax_game_set("family_greeting_ok", strcmp(rynax_call_text(grandchild, "greet", &name, 1), "hi C") == 0);
    RynaxArg args[2] = {rynax_number_arg(2), rynax_number_arg(3)};
    rynax_game_set("family_sum", rynax_call_with(grandchild, "add", args, 2));
}

static void setup(RynaxModule* module) {
    RynaxBehavior* mover = rynax_behavior(module, "Mover", sizeof(Mover));
    rynax_number(mover, "speed", offsetof(Mover, speed), 2, "Units per second");
    rynax_integer(mover, "bounces", offsetof(Mover, bounces), 3, NULL);
    rynax_flag(mover, "loud", offsetof(Mover, loud), 1, NULL);
    mover->on_start = mover_start;
    mover->on_update = mover_update;
    mover->on_collide = mover_collide;
    mover->on_message = mover_message;

    RynaxBehavior* finder = rynax_behavior(module, "Finder", 0);
    finder->on_start = finder_start;

    RynaxBehavior* fuse = rynax_behavior(module, "Fuse", sizeof(Fuse));
    fuse->on_update = fuse_update;
    fuse->on_destroy = fuse_destroyed;

    RynaxBehavior* family = rynax_behavior(module, "Family", 0);
    family->on_start = family_start;
}

RYNAX_MODULE(setup)
