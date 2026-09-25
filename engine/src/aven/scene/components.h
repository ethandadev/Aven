#pragma once

// Built-in components. Names deliberately match Unity/Godot/Unreal vocabulary
// (Transform, Camera, Light, RigidBody...) so knowledge transfers to other engines.
// Every field listed in components.cpp is automatically saved, shown in the
// inspector, and readable/writable from EasyScript, blocks and the C API.

#include "aven/core/json.h"
#include "aven/core/uuid.h"
#include "aven/ecs/registry.h"
#include "aven/math/math.h"

#include <string>
#include <vector>

namespace aven {

// Always present on every entity; shown in the inspector header, not as a component.
struct EntityInfo {
    std::string name = "Entity";
    std::string tag;
    bool active = true;
    UUID uuid;
};

struct Hierarchy {
    Entity parent;
    std::vector<Entity> children;
};

struct Transform {
    Vec3 position{0, 0, 0};
    Vec3 rotation{0, 0, 0}; // Euler angles in degrees. For 2D, rotation.z is the angle.
    Vec3 scale{1, 1, 1};

    Mat4 localMatrix() const { return Mat4::trs(position, Quat::fromEuler(rotation), scale); }
};

// Computed every frame from the hierarchy; never saved.
struct WorldTransform {
    Mat4 matrix;
};

// ---------------------------------------------------------------- Rendering

enum class Projection : int32_t { Orthographic, Perspective };

struct Camera {
    Projection projection = Projection::Orthographic;
    float fieldOfView = 60.0f; // degrees, perspective only
    float size = 5.0f;         // half the visible height in world units, orthographic only
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    Color background = Color::fromHex(0x1E2533);
    bool primary = true;
};

enum class Tonemapper : int32_t { None, ACES, Reinhard };

struct PostProcessing {
    float exposure = 1.0f;
    Tonemapper tonemapper = Tonemapper::ACES;
    bool bloom = true;
    float bloomIntensity = 0.6f;
    float bloomThreshold = 1.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    Color tint{1, 1, 1, 1};
    bool vignette = true;
    float vignetteIntensity = 0.3f;
    bool ssao = false;
    float ssaoRadius = 0.5f;
    float ssaoIntensity = 1.0f;
    bool fxaa = true;
};

enum class SkyMode : int32_t { SolidColor, Gradient, Procedural };

// Scene-wide lighting and sky. Put one on any entity (usually "Environment").
struct Environment {
    SkyMode sky = SkyMode::Procedural;
    Color skyTop = Color::fromHex(0x3A6FB5);
    Color skyHorizon = Color::fromHex(0xBFD8F0);
    Color ground = Color::fromHex(0x4A4A45);
    Color ambient = Color::fromHex(0x8899AA);
    float ambientIntensity = 0.4f;
    bool fog = false;
    Color fogColor = Color::fromHex(0xBFD8F0);
    float fogDensity = 0.02f;
};

enum class Shape2D : int32_t { Square, Circle, Triangle, RoundedSquare, Diamond, Star, Heart };

struct SpriteRenderer {
    std::string texture; // image asset; if empty, `shape` is drawn instead
    Shape2D shape = Shape2D::Square;
    Color color{1, 1, 1, 1};
    Vec2 size{1, 1};
    bool flipX = false;
    bool flipY = false;
    int order = 0; // higher draws on top
    bool pixelArt = true;
    int columns = 1; // sprite sheet layout
    int rows = 1;
    int frame = 0;
};

struct SpriteAnimator {
    int firstFrame = 0;
    int lastFrame = 0;
    float fps = 10.0f;
    bool loop = true;
    bool playing = true;
    float time = 0.0f; // runtime, not saved
};

enum class TextAlign : int32_t { Left, Center, Right };

struct TextRenderer {
    std::string text = "Hello!";
    float fontSize = 0.5f; // world units
    Color color{1, 1, 1, 1};
    TextAlign align = TextAlign::Center;
    std::string font; // optional .ttf asset
    int order = 10;
};

enum class MeshShape : int32_t { Cube, Sphere, Plane, Cylinder, Capsule, Cone, Torus, Model };

struct MeshRenderer {
    MeshShape mesh = MeshShape::Cube;
    std::string model; // .gltf/.glb asset when mesh == Model
    Color color{1, 1, 1, 1};
    std::string texture;
    float metallic = 0.0f;
    float roughness = 0.5f;
    Color emission{0, 0, 0, 1};
    float emissionStrength = 0.0f;
    Vec2 tiling{1, 1};
    bool castShadows = true;
    bool unlit = false;
};

enum class LightType : int32_t { Directional, Point, Spot };

struct Light {
    LightType type = LightType::Point;
    Color color{1, 1, 1, 1};
    float intensity = 1.0f;
    float range = 10.0f;
    float spotAngle = 45.0f;
    bool castShadows = true;
};

enum class ModelAnimationMode : int32_t { Loop, Once, PingPong };

struct ModelAnimator {
    std::string clip; // animation name inside the model; empty = first
    float speed = 1.0f;
    bool playing = true;
    ModelAnimationMode mode = ModelAnimationMode::Loop;
    float time = 0.0f; // runtime
};

// ---------------------------------------------------------------- Scripting

// Attach an EasyScript (.es) or block (.blocks) script. `overrides` holds
// per-entity values for the script's top-level variables (shown in the inspector).
struct Script {
    std::string path;
    Json overrides = Json::object();
};

// A script written in C or C++, compiled into a native module (advanced tier).
struct NativeScript {
    std::string className;
};

// ---------------------------------------------------------------- Physics

enum class BodyType : int32_t { Dynamic, Static, Kinematic };

struct RigidBody2D {
    BodyType type = BodyType::Dynamic;
    float gravityScale = 1.0f;
    float mass = 1.0f;
    float linearDamping = 0.0f;
    bool fixedRotation = false;
    bool continuous = false; // fast-moving objects (bullets)
};

struct BoxCollider2D {
    Vec2 size{1, 1};
    Vec2 offset{0, 0};
    float friction = 0.4f;
    float bounciness = 0.0f;
    bool isTrigger = false;
};

struct CircleCollider2D {
    float radius = 0.5f;
    Vec2 offset{0, 0};
    float friction = 0.4f;
    float bounciness = 0.0f;
    bool isTrigger = false;
};

struct RigidBody {
    BodyType type = BodyType::Dynamic;
    float mass = 1.0f;
    float gravityScale = 1.0f;
    float linearDamping = 0.05f;
    bool lockRotation = false;
};

struct BoxCollider {
    Vec3 size{1, 1, 1};
    Vec3 offset{0, 0, 0};
    float friction = 0.5f;
    float bounciness = 0.0f;
    bool isTrigger = false;
};

struct SphereCollider {
    float radius = 0.5f;
    Vec3 offset{0, 0, 0};
    float friction = 0.5f;
    float bounciness = 0.0f;
    bool isTrigger = false;
};

// Moves with walk/jump input and collides with the world without being a
// full physics body. The easiest way to make a 3D player.
struct CharacterController {
    float height = 1.8f;
    float radius = 0.4f;
    float speed = 6.0f;
    float jumpHeight = 1.5f;
    float gravity = 20.0f;
    bool useInput = true; // WASD/arrows + space handled automatically
    bool firstPerson = false;
    float mouseSensitivity = 0.15f;
    Vec3 velocity{0, 0, 0}; // runtime
    bool grounded = false;  // runtime
};

// ---------------------------------------------------------------- Audio

struct AudioSource {
    std::string clip;
    float volume = 1.0f;
    float pitch = 1.0f;
    bool loop = false;
    bool playOnStart = false;
    bool spatial = false;
    float range = 20.0f;
};

struct AudioListener {};

// ---------------------------------------------------------------- Effects & gameplay helpers

enum class EmitterShape : int32_t { Point, Circle, Sphere, Box };

struct ParticleEmitter {
    bool emitting = true;
    float rate = 20.0f;
    int burst = 0; // particles emitted at once when started
    int maxParticles = 500;
    float lifetime = 1.5f;
    float speed = 3.0f;
    float spread = 30.0f; // cone angle in degrees
    Vec3 direction{0, 1, 0};
    Vec3 gravity{0, -2, 0};
    EmitterShape shapeType = EmitterShape::Point;
    float shapeSize = 0.2f;
    Color startColor{1, 0.8f, 0.3f, 1};
    Color endColor{1, 0.2f, 0.1f, 0};
    float startSize = 0.3f;
    float endSize = 0.0f;
    std::string texture;
    bool additive = true;
    bool worldSpace = true;
};

struct CameraFollow {
    UUID target;
    Vec3 offset{0, 0, 0};
    float smoothness = 5.0f; // 0 = snap instantly
    bool followX = true;
    bool followY = true;
    bool followZ = false;
    bool lookAtTarget = false;
};

// ---------------------------------------------------------------- UI (screen space)

enum class Anchor : int32_t { TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight };

// Places an entity on screen instead of in the world. Positions are in pixels of
// a 1280x720 reference screen, scaled to the real window size.
struct UIElement {
    Anchor anchor = Anchor::Center;
    Vec2 offset{0, 0};
    Vec2 size{200, 60};
    int order = 0;
};

struct UIText {
    std::string text = "Text";
    float fontSize = 32.0f;
    Color color{1, 1, 1, 1};
    TextAlign align = TextAlign::Center;
    std::string font;
};

struct UIImage {
    std::string texture;
    Shape2D shape = Shape2D::RoundedSquare;
    Color color{1, 1, 1, 1};
};

struct UIButton {
    std::string text = "Button";
    float fontSize = 28.0f;
    Color normalColor = Color::fromHex(0x3B82F6);
    Color hoverColor = Color::fromHex(0x60A5FA);
    Color pressedColor = Color::fromHex(0x1D4ED8);
    Color textColor{1, 1, 1, 1};
    bool hovered = false; // runtime
    bool pressed = false; // runtime
};

// Marks an entity as an instance of a prefab asset.
struct PrefabInstance {
    std::string path;
};

// ---------------------------------------------------------------- runtime-only (never saved)

// Hides an entity's renderers without disabling its scripts (self.visible = False).
struct Hidden {};

struct Particle {
    Vec3 position;
    Vec3 velocity;
    float age = 0;
    float life = 1;
    float spin = 0;
    float rotation = 0;
};

struct ParticleState {
    std::vector<Particle> particles;
    float emitAccumulator = 0;
    bool wasEmitting = false;
    uint32_t seed = 1;
};

} // namespace aven
