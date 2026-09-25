/*
 * aven.h - the Aven C API for native modules.
 *
 * A native module is a shared library (.dll on Windows, .so on Linux, .dylib on macOS) with
 * behaviors written in C or C++. Put an object's behavior in a NativeScript component and it runs
 * just like an EasyScript script: on_start, on_update, on_collide and the rest. The functions here
 * use the same names as EasyScript, so
 *
 *     self.x += speed * dt                          (EasyScript)
 *     aven_set(self, "x", aven_get(self, "x") + s->speed * dt);    (C)
 *
 * do the same thing. Exactly one file of a module ends with AVEN_MODULE(setup):
 *
 *     #include "aven.h"
 *
 *     typedef struct { float speed; } Spinner;      // each object gets its own copy
 *
 *     static void spin(AvenEntity self, void* data, float dt) {
 *         Spinner* s = (Spinner*)data;
 *         aven_set(self, "angle", aven_get(self, "angle") + s->speed * dt);
 *     }
 *
 *     static void setup(AvenModule* module) {
 *         AvenBehavior* b = aven_behavior(module, "Spinner", sizeof(Spinner));
 *         aven_number(b, "speed", offsetof(Spinner, speed), 90, "Degrees per second");
 *         b->on_update = spin;
 *     }
 *
 *     AVEN_MODULE(setup)
 *
 * Numbers declared with aven_number / aven_integer / aven_flag show up in the Inspector.
 *
 * Text returned by the API (aven_name, aven_get_text...) stays valid until a few more API calls
 * have been made; copy it if you need to keep it.
 *
 * This header is plain C99 and has no dependencies, so other languages can bind to it too.
 */

#ifndef AVEN_H
#define AVEN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AVEN_API_VERSION 1

#if defined(_WIN32)
#define AVEN_EXPORT __declspec(dllexport)
#define AVEN_PRIVATE
#else
#define AVEN_EXPORT __attribute__((visibility("default")))
#define AVEN_PRIVATE __attribute__((visibility("hidden"))) /* each module keeps its own copy */
#endif

/* An object in the game. 0 means "no object". Handles of destroyed objects are never reused. */
typedef uint64_t AvenEntity;

typedef struct AvenModule AvenModule; /* the module being set up (owned by the engine) */

/* What a behavior does. Leave events you don't need as NULL. `data` is this object's own copy of
 * the behavior's data (data_size bytes, zeroed, then filled with the property values). */
typedef struct AvenBehavior {
    void (*on_start)(AvenEntity self, void* data);
    void (*on_update)(AvenEntity self, void* data, float dt);
    void (*on_fixed_update)(AvenEntity self, void* data, float dt);
    void (*on_collide)(AvenEntity self, void* data, AvenEntity other);
    void (*on_collide_end)(AvenEntity self, void* data, AvenEntity other);
    void (*on_trigger)(AvenEntity self, void* data, AvenEntity other);
    void (*on_trigger_exit)(AvenEntity self, void* data, AvenEntity other);
    void (*on_click)(AvenEntity self, void* data);
    void (*on_key_pressed)(AvenEntity self, void* data, const char* key);
    void (*on_message)(AvenEntity self, void* data, const char* message, double value);
    void (*on_destroy)(AvenEntity self, void* data);
    void* reserved[8]; /* room for new events without breaking modules built today */
} AvenBehavior;

typedef enum AvenPropertyType {
    AVEN_PROPERTY_NUMBER = 0,  /* float */
    AVEN_PROPERTY_INTEGER = 1, /* int32_t */
    AVEN_PROPERTY_FLAG = 2     /* int32_t, 0 or 1 (shown as a checkbox) */
} AvenPropertyType;

/* Every engine function, in one table. Use the aven_* wrappers below instead of calling it. */
typedef struct AvenApi {
    uint32_t version; /* the AVEN_API_VERSION the engine speaks */
    uint32_t size;    /* sizeof(AvenApi) in the engine; newer engines only ever add to the end */

    /* Setting up (only inside your setup function) */
    AvenBehavior* (*add_behavior)(AvenModule* module, const char* name, size_t data_size);
    void (*add_property)(AvenBehavior* behavior, const char* name, AvenPropertyType type, size_t offset,
                         double default_value, const char* tooltip);

    /* The console */
    void (*print)(const char* text);
    void (*warn)(const char* text);
    void (*error)(const char* text);

    /* Objects */
    AvenEntity (*find)(const char* name);
    int (*find_all)(const char* tag, AvenEntity* out, int max);
    AvenEntity (*spawn)(const char* prefab, float x, float y, float z);
    void (*destroy)(AvenEntity e);
    int (*exists)(AvenEntity e);
    const char* (*name)(AvenEntity e);
    const char* (*tag)(AvenEntity e);

    /* Properties, by their EasyScript names: "x", "y", "z", "angle", "scale_x", "velocity_x",
     * "velocity_y", "alpha", "on_ground", "frame"... (true/false come back as 1/0). */
    double (*get)(AvenEntity e, const char* property);
    void (*set)(AvenEntity e, const char* property, double value);
    const char* (*get_text)(AvenEntity e, const char* property); /* "text", "image", "tag", "name" */
    void (*set_text)(AvenEntity e, const char* property, const char* value);
    /* Calls an EasyScript method with number arguments, e.g. "apply_impulse", "play_animation". */
    double (*call)(AvenEntity e, const char* method, const double* args, int count);

    /* Input (key names as in EasyScript: "left", "a", "space", "enter"...) */
    int (*key_down)(const char* key);
    int (*key_pressed)(const char* key);
    int (*key_released)(const char* key);
    int (*mouse_down)(void);
    double (*mouse_x)(void); /* in the world */
    double (*mouse_y)(void);

    /* Shared game values: game.score in EasyScript is aven_game_get("score", 0) here. */
    double (*game_get)(const char* name, double fallback);
    void (*game_set)(const char* name, double value);

    /* Everything else */
    void (*send)(AvenEntity target, const char* message, double value); /* target 0: everyone */
    void (*play_sound)(const char* path);
    void (*load_scene)(const char* path);
    double (*time)(void);
    double (*delta_time)(void);
    double (*random)(double low, double high);
} AvenApi;

/* Set by AVEN_MODULE when the engine loads the module. */
extern AVEN_PRIVATE const AvenApi* aven_api;

/* Your module's entry point, made by AVEN_MODULE. Returns the API version it was built for.
 * (Declared here, inside extern "C", so C++ modules export it under this exact name.) */
typedef int (*AvenModuleEntry)(const AvenApi* api, AvenModule* module);
AVEN_EXPORT int aven_module_entry(const AvenApi* api, AvenModule* module);

#define AVEN_MODULE(setup_function)                                                           \
    AVEN_PRIVATE const AvenApi* aven_api = 0;                                                 \
    AVEN_EXPORT int aven_module_entry(const AvenApi* api, AvenModule* module) {               \
        if (!api || api->version < AVEN_API_VERSION)                                          \
            return 0;                                                                         \
        aven_api = api;                                                                       \
        setup_function(module);                                                               \
        return AVEN_API_VERSION;                                                              \
    }

/* ---- Friendly wrappers ---------------------------------------------------------------- */

static inline AvenBehavior* aven_behavior(AvenModule* m, const char* name, size_t data_size) {
    return aven_api->add_behavior(m, name, data_size);
}
static inline void aven_number(AvenBehavior* b, const char* name, size_t offset, double value, const char* tooltip) {
    aven_api->add_property(b, name, AVEN_PROPERTY_NUMBER, offset, value, tooltip);
}
static inline void aven_integer(AvenBehavior* b, const char* name, size_t offset, int value, const char* tooltip) {
    aven_api->add_property(b, name, AVEN_PROPERTY_INTEGER, offset, value, tooltip);
}
static inline void aven_flag(AvenBehavior* b, const char* name, size_t offset, int value, const char* tooltip) {
    aven_api->add_property(b, name, AVEN_PROPERTY_FLAG, offset, value ? 1 : 0, tooltip);
}

static inline void aven_print(const char* text) { aven_api->print(text); }
static inline void aven_warn(const char* text) { aven_api->warn(text); }
static inline void aven_error(const char* text) { aven_api->error(text); }

static inline AvenEntity aven_find(const char* name) { return aven_api->find(name); }
static inline int aven_find_all(const char* tag, AvenEntity* out, int max) { return aven_api->find_all(tag, out, max); }
static inline AvenEntity aven_spawn(const char* prefab, float x, float y, float z) { return aven_api->spawn(prefab, x, y, z); }
static inline void aven_destroy(AvenEntity e) { aven_api->destroy(e); }
static inline int aven_exists(AvenEntity e) { return aven_api->exists(e); }
static inline const char* aven_name(AvenEntity e) { return aven_api->name(e); }
static inline const char* aven_tag(AvenEntity e) { return aven_api->tag(e); }

static inline double aven_get(AvenEntity e, const char* property) { return aven_api->get(e, property); }
static inline void aven_set(AvenEntity e, const char* property, double value) { aven_api->set(e, property, value); }
static inline const char* aven_get_text(AvenEntity e, const char* property) { return aven_api->get_text(e, property); }
static inline void aven_set_text(AvenEntity e, const char* property, const char* value) {
    aven_api->set_text(e, property, value);
}
static inline double aven_call(AvenEntity e, const char* method, const double* args, int count) {
    return aven_api->call(e, method, args, count);
}

static inline int aven_key_down(const char* key) { return aven_api->key_down(key); }
static inline int aven_key_pressed(const char* key) { return aven_api->key_pressed(key); }
static inline int aven_key_released(const char* key) { return aven_api->key_released(key); }
static inline int aven_mouse_down(void) { return aven_api->mouse_down(); }
static inline double aven_mouse_x(void) { return aven_api->mouse_x(); }
static inline double aven_mouse_y(void) { return aven_api->mouse_y(); }

static inline double aven_game_get(const char* name, double fallback) { return aven_api->game_get(name, fallback); }
static inline void aven_game_set(const char* name, double value) { aven_api->game_set(name, value); }

static inline void aven_send(AvenEntity target, const char* message, double value) { aven_api->send(target, message, value); }
static inline void aven_play_sound(const char* path) { aven_api->play_sound(path); }
static inline void aven_load_scene(const char* path) { aven_api->load_scene(path); }
static inline double aven_time(void) { return aven_api->time(); }
static inline double aven_delta_time(void) { return aven_api->delta_time(); }
static inline double aven_random(double low, double high) { return aven_api->random(low, high); }

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AVEN_H */
