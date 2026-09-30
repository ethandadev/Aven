/*
 * rynax.h - the Rynax C API for native modules.
 *
 * A native module is a shared library (.dll on Windows, .so on Linux, .dylib on macOS) with
 * behaviors written in C or C++. Put an object's behavior in a NativeScript component and it runs
 * just like an EasyScript script: on_start, on_update, on_collide and the rest. The functions here
 * use the same names as EasyScript, so
 *
 *     self.x += speed * dt                          (EasyScript)
 *     rynax_set(self, "x", rynax_get(self, "x") + s->speed * dt);    (C)
 *
 * do the same thing. Each module (one library, built from one file in native/src) ends with
 * RYNAX_MODULE(setup):
 *
 *     #include "rynax.h"
 *
 *     typedef struct { float speed; } Spinner;      // each object gets its own copy
 *
 *     static void spin(RynaxEntity self, void* data, float dt) {
 *         Spinner* s = (Spinner*)data;
 *         rynax_set(self, "angle", rynax_get(self, "angle") + s->speed * dt);
 *     }
 *
 *     static void setup(RynaxModule* module) {
 *         RynaxBehavior* b = rynax_behavior(module, "Spinner", sizeof(Spinner));
 *         rynax_number(b, "speed", offsetof(Spinner, speed), 90, "Degrees per second");
 *         b->on_update = spin;
 *     }
 *
 *     RYNAX_MODULE(setup)
 *
 * Numbers declared with rynax_number / rynax_integer / rynax_flag show up in the Inspector.
 *
 * Text returned by the API (rynax_name, rynax_get_text...) stays valid until a few more API calls
 * have been made; copy it if you need to keep it.
 *
 * This header is plain C99 and has no dependencies, so other languages can bind to it too.
 */

#ifndef RYNAX_H
#define RYNAX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RYNAX_API_VERSION 2 /* 2: parents and children, and calls with text arguments */

#if defined(_WIN32)
#define RYNAX_EXPORT __declspec(dllexport)
#define RYNAX_PRIVATE
#else
#define RYNAX_EXPORT __attribute__((visibility("default")))
#define RYNAX_PRIVATE __attribute__((visibility("hidden"))) /* each module keeps its own copy */
#endif

/* An object in the game. 0 means "no object". Handles of destroyed objects are never reused. */
typedef uint64_t RynaxEntity;

typedef struct RynaxModule RynaxModule; /* the module being set up (owned by the engine) */

/* What a behavior does. Leave events you don't need as NULL. `data` is this object's own copy of
 * the behavior's data (data_size bytes, zeroed, then filled with the property values). */
typedef struct RynaxBehavior {
    void (*on_start)(RynaxEntity self, void* data);
    void (*on_update)(RynaxEntity self, void* data, float dt);
    void (*on_fixed_update)(RynaxEntity self, void* data, float dt);
    void (*on_collide)(RynaxEntity self, void* data, RynaxEntity other);
    void (*on_collide_end)(RynaxEntity self, void* data, RynaxEntity other);
    void (*on_trigger)(RynaxEntity self, void* data, RynaxEntity other);
    void (*on_trigger_exit)(RynaxEntity self, void* data, RynaxEntity other);
    void (*on_click)(RynaxEntity self, void* data);
    void (*on_key_pressed)(RynaxEntity self, void* data, const char* key);
    void (*on_message)(RynaxEntity self, void* data, const char* message, double value);
    void (*on_destroy)(RynaxEntity self, void* data);
    void* reserved[8]; /* room for new events without breaking modules built today */
} RynaxBehavior;

/* One argument for rynax_call_with: a number, or (when `text` isn't NULL) a text.
 * Make them with rynax_number_arg(3.5) and rynax_text_arg("jump"). */
typedef struct RynaxArg {
    const char* text;
    double number;
} RynaxArg;

typedef enum RynaxPropertyType {
    RYNAX_PROPERTY_NUMBER = 0,  /* float */
    RYNAX_PROPERTY_INTEGER = 1, /* int32_t */
    RYNAX_PROPERTY_FLAG = 2     /* int32_t, 0 or 1 (shown as a checkbox) */
} RynaxPropertyType;

/* Every engine function, in one table. Use the rynax_* wrappers below instead of calling it. */
typedef struct RynaxApi {
    uint32_t version; /* the RYNAX_API_VERSION the engine speaks */
    uint32_t size;    /* sizeof(RynaxApi) in the engine; newer engines only ever add to the end */

    /* Setting up (only inside your setup function) */
    RynaxBehavior* (*add_behavior)(RynaxModule* module, const char* name, size_t data_size);
    void (*add_property)(RynaxBehavior* behavior, const char* name, RynaxPropertyType type, size_t offset,
                         double default_value, const char* tooltip);

    /* The console */
    void (*print)(const char* text);
    void (*warn)(const char* text);
    void (*error)(const char* text);

    /* Objects */
    RynaxEntity (*find)(const char* name);
    int (*find_all)(const char* tag, RynaxEntity* out, int max);
    RynaxEntity (*spawn)(const char* prefab, float x, float y, float z);
    void (*destroy)(RynaxEntity e);
    int (*exists)(RynaxEntity e);
    const char* (*name)(RynaxEntity e);
    const char* (*tag)(RynaxEntity e);

    /* Properties, by their EasyScript names: "x", "y", "z", "angle", "scale_x", "velocity_x",
     * "velocity_y", "alpha", "on_ground", "frame"... (true/false come back as 1/0). */
    double (*get)(RynaxEntity e, const char* property);
    void (*set)(RynaxEntity e, const char* property, double value);
    const char* (*get_text)(RynaxEntity e, const char* property); /* "text", "image", "tag", "name" */
    void (*set_text)(RynaxEntity e, const char* property, const char* value);
    /* Calls an EasyScript method with number arguments, e.g. "apply_impulse", "play_animation". */
    double (*call)(RynaxEntity e, const char* method, const double* args, int count);

    /* Input (key names as in EasyScript: "left", "a", "space", "enter"...) */
    int (*key_down)(const char* key);
    int (*key_pressed)(const char* key);
    int (*key_released)(const char* key);
    int (*mouse_down)(void);
    double (*mouse_x)(void); /* in the world */
    double (*mouse_y)(void);

    /* Shared game values: game.score in EasyScript is rynax_game_get("score", 0) here. */
    double (*game_get)(const char* name, double fallback);
    void (*game_set)(const char* name, double value);

    /* Everything else */
    void (*send)(RynaxEntity target, const char* message, double value); /* target 0: everyone */
    void (*play_sound)(const char* path);
    void (*load_scene)(const char* path);
    double (*time)(void);
    double (*delta_time)(void);
    double (*random)(double low, double high);

    /* ---- Added in API version 2 ---- */

    /* The family: an object's parent (0 when it's at the top), its children, and the first object
     * called `name` anywhere below it (children, their children...). */
    RynaxEntity (*parent)(RynaxEntity e);
    int (*children)(RynaxEntity e, RynaxEntity* out, int max);
    RynaxEntity (*find_child)(RynaxEntity e, const char* name);
    /* Calls an EasyScript method with numbers and texts (see RynaxArg). call_with returns a number
     * result (1/0 for true/false); call_text returns a text result ("" if there isn't one). */
    double (*call_with)(RynaxEntity e, const char* method, const RynaxArg* args, int count);
    const char* (*call_text)(RynaxEntity e, const char* method, const RynaxArg* args, int count);
} RynaxApi;

/* Set by RYNAX_MODULE when the engine loads the module. */
extern RYNAX_PRIVATE const RynaxApi* rynax_api;

/* Your module's entry point, made by RYNAX_MODULE. Returns the API version it was built for.
 * (Declared here, inside extern "C", so C++ modules export it under this exact name.) */
typedef int (*RynaxModuleEntry)(const RynaxApi* api, RynaxModule* module);
RYNAX_EXPORT int rynax_module_entry(const RynaxApi* api, RynaxModule* module);

#define RYNAX_MODULE(setup_function)                                                           \
    RYNAX_PRIVATE const RynaxApi* rynax_api = 0;                                                 \
    RYNAX_EXPORT int rynax_module_entry(const RynaxApi* api, RynaxModule* module) {               \
        if (!api || api->version < RYNAX_API_VERSION)                                          \
            return 0;                                                                         \
        rynax_api = api;                                                                       \
        setup_function(module);                                                               \
        return RYNAX_API_VERSION;                                                              \
    }

/* ---- Friendly wrappers ---------------------------------------------------------------- */

static inline RynaxBehavior* rynax_behavior(RynaxModule* m, const char* name, size_t data_size) {
    return rynax_api->add_behavior(m, name, data_size);
}
static inline void rynax_number(RynaxBehavior* b, const char* name, size_t offset, double value, const char* tooltip) {
    rynax_api->add_property(b, name, RYNAX_PROPERTY_NUMBER, offset, value, tooltip);
}
static inline void rynax_integer(RynaxBehavior* b, const char* name, size_t offset, int value, const char* tooltip) {
    rynax_api->add_property(b, name, RYNAX_PROPERTY_INTEGER, offset, value, tooltip);
}
static inline void rynax_flag(RynaxBehavior* b, const char* name, size_t offset, int value, const char* tooltip) {
    rynax_api->add_property(b, name, RYNAX_PROPERTY_FLAG, offset, value ? 1 : 0, tooltip);
}

static inline void rynax_print(const char* text) { rynax_api->print(text); }
static inline void rynax_warn(const char* text) { rynax_api->warn(text); }
static inline void rynax_error(const char* text) { rynax_api->error(text); }

static inline RynaxEntity rynax_find(const char* name) { return rynax_api->find(name); }
static inline int rynax_find_all(const char* tag, RynaxEntity* out, int max) { return rynax_api->find_all(tag, out, max); }
static inline RynaxEntity rynax_spawn(const char* prefab, float x, float y, float z) { return rynax_api->spawn(prefab, x, y, z); }
static inline void rynax_destroy(RynaxEntity e) { rynax_api->destroy(e); }
static inline int rynax_exists(RynaxEntity e) { return rynax_api->exists(e); }
static inline const char* rynax_name(RynaxEntity e) { return rynax_api->name(e); }
static inline const char* rynax_tag(RynaxEntity e) { return rynax_api->tag(e); }

static inline double rynax_get(RynaxEntity e, const char* property) { return rynax_api->get(e, property); }
static inline void rynax_set(RynaxEntity e, const char* property, double value) { rynax_api->set(e, property, value); }
static inline const char* rynax_get_text(RynaxEntity e, const char* property) { return rynax_api->get_text(e, property); }
static inline void rynax_set_text(RynaxEntity e, const char* property, const char* value) {
    rynax_api->set_text(e, property, value);
}
static inline double rynax_call(RynaxEntity e, const char* method, const double* args, int count) {
    return rynax_api->call(e, method, args, count);
}

static inline int rynax_key_down(const char* key) { return rynax_api->key_down(key); }
static inline int rynax_key_pressed(const char* key) { return rynax_api->key_pressed(key); }
static inline int rynax_key_released(const char* key) { return rynax_api->key_released(key); }
static inline int rynax_mouse_down(void) { return rynax_api->mouse_down(); }
static inline double rynax_mouse_x(void) { return rynax_api->mouse_x(); }
static inline double rynax_mouse_y(void) { return rynax_api->mouse_y(); }

static inline double rynax_game_get(const char* name, double fallback) { return rynax_api->game_get(name, fallback); }
static inline void rynax_game_set(const char* name, double value) { rynax_api->game_set(name, value); }

static inline void rynax_send(RynaxEntity target, const char* message, double value) { rynax_api->send(target, message, value); }
static inline void rynax_play_sound(const char* path) { rynax_api->play_sound(path); }
static inline void rynax_load_scene(const char* path) { rynax_api->load_scene(path); }
static inline double rynax_time(void) { return rynax_api->time(); }
static inline double rynax_delta_time(void) { return rynax_api->delta_time(); }
static inline double rynax_random(double low, double high) { return rynax_api->random(low, high); }

/* API version 2 */
static inline RynaxEntity rynax_parent(RynaxEntity e) { return rynax_api->parent(e); }
static inline int rynax_children(RynaxEntity e, RynaxEntity* out, int max) { return rynax_api->children(e, out, max); }
static inline RynaxEntity rynax_find_child(RynaxEntity e, const char* name) { return rynax_api->find_child(e, name); }
static inline RynaxArg rynax_number_arg(double number) {
    RynaxArg a;
    a.text = 0;
    a.number = number;
    return a;
}
static inline RynaxArg rynax_text_arg(const char* text) {
    RynaxArg a;
    a.text = text ? text : "";
    a.number = 0;
    return a;
}
static inline double rynax_call_with(RynaxEntity e, const char* method, const RynaxArg* args, int count) {
    return rynax_api->call_with(e, method, args, count);
}
static inline const char* rynax_call_text(RynaxEntity e, const char* method, const RynaxArg* args, int count) {
    return rynax_api->call_text(e, method, args, count);
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RYNAX_H */
