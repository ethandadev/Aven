#include "aven/scene/components.h"
#include "aven/scene/reflection.h"

namespace aven {

#define F(member) #member, &Type::member

namespace {

struct Registrar {
    std::vector<ComponentInfo> list;

    template <class T>
    ComponentBuilder<T> add(const char* name, const char* category, const char* description, bool advanced = false,
                            bool is2D = false, bool is3D = false) {
        ComponentInfo info;
        info.name = name;
        info.scriptName = toSnakeCase(name);
        info.category = category;
        info.description = description;
        info.advanced = advanced;
        info.is2D = is2D;
        info.is3D = is3D;
        list.push_back(std::move(info));
        return ComponentBuilder<T>(list.back());
    }
};

const std::vector<std::string> kBodyTypes{"Dynamic", "Static", "Kinematic"};
const std::vector<std::string> kShapes2D{"Square", "Circle", "Triangle", "RoundedSquare", "Diamond", "Star", "Heart"};
const std::vector<std::string> kAlign{"Left", "Center", "Right"};

std::vector<ComponentInfo> buildRegistry() {
    Registrar r;
    // Note: `list` must not reallocate while a builder is alive, so reserve up front.
    r.list.reserve(64);

    {
        using Type = Transform;
        r.add<Type>("Transform", "Basics", "Position, rotation and size of the object.")
            .field(F(position), {.step = 0.05f})
            .field(F(rotation), {.tooltip = "Rotation in degrees. For 2D games, Z is the spin angle.", .step = 0.5f})
            .field(F(scale), {.step = 0.02f});
        r.list.back().removable = false;
    }
    {
        using Type = Camera;
        r.add<Type>("Camera", "Rendering", "Shows the game world. The primary camera is what players see.")
            .field(F(projection), {.tooltip = "Orthographic for 2D games, Perspective for 3D games.",
                                   .enumNames = {"Orthographic", "Perspective"}})
            .field(F(fieldOfView), {.min = 10, .max = 150, .step = 1})
            .field(F(size), {.tooltip = "How much of the world is visible (orthographic).", .min = 0.5f, .max = 50})
            .field(F(nearClip), {.advanced = true})
            .field(F(farClip), {.advanced = true})
            .field(F(background))
            .field(F(primary));
    }
    {
        using Type = PostProcessing;
        r.add<Type>("PostProcessing", "Rendering", "Screen effects: bloom, color grading, vignette, SSAO and anti-aliasing. Add to a camera.", true)
            .field(F(exposure), {.min = 0.1f, .max = 5})
            .field(F(tonemapper), {.enumNames = {"None", "ACES", "Reinhard"}})
            .field(F(bloom))
            .field(F(bloomIntensity), {.min = 0, .max = 3})
            .field(F(bloomThreshold), {.min = 0, .max = 5})
            .field(F(contrast), {.min = 0.5f, .max = 2})
            .field(F(saturation), {.min = 0, .max = 2})
            .field(F(tint))
            .field(F(vignette))
            .field(F(vignetteIntensity), {.min = 0, .max = 1})
            .field(F(ssao), {.label = "SSAO", .tooltip = "Screen-space ambient occlusion: soft contact shadows (3D)."})
            .field(F(ssaoRadius), {.label = "SSAO Radius", .min = 0.05f, .max = 3})
            .field(F(ssaoIntensity), {.label = "SSAO Intensity", .min = 0, .max = 4})
            .field(F(fxaa), {.label = "FXAA", .tooltip = "Smooths jagged edges."});
    }
    {
        using Type = Environment;
        r.add<Type>("Environment", "Rendering", "Sky, ambient light and fog for the whole scene.")
            .field(F(sky), {.enumNames = {"SolidColor", "Gradient", "Procedural"}})
            .field(F(skyTop))
            .field(F(skyHorizon))
            .field(F(ground))
            .field(F(ambient))
            .field(F(ambientIntensity), {.min = 0, .max = 2})
            .field(F(fog))
            .field(F(fogColor))
            .field(F(fogDensity), {.min = 0, .max = 0.3f, .step = 0.001f});
    }
    {
        using Type = SpriteRenderer;
        r.add<Type>("SpriteRenderer", "Rendering 2D", "Draws an image or a simple shape.", false, true)
            .field(F(texture), {.label = "Image", .asset = AssetKind::Image})
            .field(F(shape), {.tooltip = "Drawn when no image is set.", .enumNames = kShapes2D})
            .field(F(color))
            .field(F(size))
            .field(F(flipX))
            .field(F(flipY))
            .field(F(order), {.tooltip = "Higher numbers are drawn on top."})
            .field(F(pixelArt), {.tooltip = "Keep pixels sharp instead of smoothing them."})
            .field(F(columns), {.tooltip = "Sprite sheet columns.", .advanced = true})
            .field(F(rows), {.tooltip = "Sprite sheet rows.", .advanced = true})
            .field(F(frame), {.advanced = true});
    }
    {
        using Type = SpriteAnimator;
        r.add<Type>("SpriteAnimator", "Rendering 2D", "Plays frames from a sprite sheet (set columns/rows on the SpriteRenderer).", false, true)
            .field(F(firstFrame))
            .field(F(lastFrame))
            .field(F(fps), {.label = "FPS", .min = 1, .max = 60})
            .field(F(loop))
            .field(F(playing))
            .field(F(time), {.runtime = true});
    }
    {
        using Type = TextRenderer;
        r.add<Type>("TextRenderer", "Rendering 2D", "Draws text in the game world.")
            .field(F(text), {.multiline = true})
            .field(F(fontSize), {.min = 0.05f, .max = 5})
            .field(F(color))
            .field(F(align), {.enumNames = kAlign})
            .field(F(font), {.asset = AssetKind::Font, .advanced = true})
            .field(F(order));
    }
    {
        using Type = MeshRenderer;
        r.add<Type>("MeshRenderer", "Rendering 3D", "Draws a 3D shape or model with a material.", false, false, true)
            .field(F(mesh), {.enumNames = {"Cube", "Sphere", "Plane", "Cylinder", "Capsule", "Cone", "Torus", "Model"}})
            .field(F(model), {.tooltip = "A .gltf or .glb file, used when Mesh is Model.", .asset = AssetKind::Model})
            .field(F(color))
            .field(F(texture), {.asset = AssetKind::Image})
            .field(F(metallic), {.min = 0, .max = 1})
            .field(F(roughness), {.min = 0, .max = 1})
            .field(F(emission), {.advanced = true})
            .field(F(emissionStrength), {.min = 0, .max = 20, .advanced = true})
            .field(F(tiling), {.advanced = true})
            .field(F(castShadows))
            .field(F(unlit), {.advanced = true});
    }
    {
        using Type = Light;
        r.add<Type>("Light", "Rendering 3D", "Lights up 3D objects. Directional lights act like the sun.", false, false, true)
            .field(F(type), {.enumNames = {"Directional", "Point", "Spot"}})
            .field(F(color))
            .field(F(intensity), {.min = 0, .max = 10})
            .field(F(range), {.min = 0.5f, .max = 100})
            .field(F(spotAngle), {.min = 1, .max = 170})
            .field(F(castShadows));
    }
    {
        using Type = ModelAnimator;
        r.add<Type>("ModelAnimator", "Rendering 3D", "Plays animations stored in a 3D model.", true, false, true)
            .field(F(clip))
            .field(F(speed), {.min = 0, .max = 3})
            .field(F(playing))
            .field(F(mode), {.enumNames = {"Loop", "Once", "PingPong"}})
            .field(F(time), {.runtime = true});
    }
    {
        using Type = Script;
        r.add<Type>("Script", "Scripting", "Gives the object behavior using blocks or EasyScript.")
            .field(F(path), {.label = "Script", .asset = AssetKind::Script});
        auto& info = r.list.back();
        info.saveExtra = [](const void* c, Json& j) {
            auto& s = *static_cast<const Script*>(c);
            if (s.overrides.size())
                j["overrides"] = s.overrides;
        };
        info.loadExtra = [](void* c, const Json& j) {
            auto& s = *static_cast<Script*>(c);
            s.overrides = j["overrides"].isObject() ? j["overrides"] : Json::object();
        };
    }
    {
        using Type = NativeScript;
        r.add<Type>("NativeScript", "Scripting", "Behavior written in C or C++ inside a native module.", true)
            .field(F(className));
    }
    {
        using Type = RigidBody2D;
        r.add<Type>("RigidBody2D", "Physics 2D", "Makes the object fall, bounce and collide.", false, true)
            .field(F(type), {.tooltip = "Dynamic moves with physics, Static never moves, Kinematic is moved by scripts.",
                             .enumNames = kBodyTypes})
            .field(F(gravityScale), {.min = -5, .max = 5})
            .field(F(mass), {.min = 0.01f, .max = 100})
            .field(F(linearDamping), {.min = 0, .max = 10, .advanced = true})
            .field(F(fixedRotation), {.tooltip = "Stop the object from tipping over (great for players)."})
            .field(F(continuous), {.tooltip = "Better collisions for very fast objects like bullets.", .advanced = true});
    }
    {
        using Type = BoxCollider2D;
        r.add<Type>("BoxCollider2D", "Physics 2D", "A rectangle shape used for collisions.", false, true)
            .field(F(size))
            .field(F(offset))
            .field(F(friction), {.min = 0, .max = 1})
            .field(F(bounciness), {.min = 0, .max = 1})
            .field(F(isTrigger), {.tooltip = "Triggers detect overlaps without blocking movement."});
    }
    {
        using Type = CircleCollider2D;
        r.add<Type>("CircleCollider2D", "Physics 2D", "A circle shape used for collisions.", false, true)
            .field(F(radius), {.min = 0.01f, .max = 20})
            .field(F(offset))
            .field(F(friction), {.min = 0, .max = 1})
            .field(F(bounciness), {.min = 0, .max = 1})
            .field(F(isTrigger));
    }
    {
        using Type = RigidBody;
        r.add<Type>("RigidBody", "Physics 3D", "Makes a 3D object fall, bounce and collide.", false, false, true)
            .field(F(type), {.enumNames = kBodyTypes})
            .field(F(mass), {.min = 0.01f, .max = 100})
            .field(F(gravityScale), {.min = -5, .max = 5})
            .field(F(linearDamping), {.min = 0, .max = 10, .advanced = true})
            .field(F(lockRotation));
    }
    {
        using Type = BoxCollider;
        r.add<Type>("BoxCollider", "Physics 3D", "A box shape used for 3D collisions.", false, false, true)
            .field(F(size))
            .field(F(offset))
            .field(F(friction), {.min = 0, .max = 1})
            .field(F(bounciness), {.min = 0, .max = 1})
            .field(F(isTrigger));
    }
    {
        using Type = SphereCollider;
        r.add<Type>("SphereCollider", "Physics 3D", "A ball shape used for 3D collisions.", false, false, true)
            .field(F(radius), {.min = 0.01f, .max = 50})
            .field(F(offset))
            .field(F(friction), {.min = 0, .max = 1})
            .field(F(bounciness), {.min = 0, .max = 1})
            .field(F(isTrigger));
    }
    {
        using Type = CharacterController;
        r.add<Type>("CharacterController", "Physics 3D", "A ready-made 3D player: walks, jumps and bumps into walls.", false, false, true)
            .field(F(height), {.min = 0.2f, .max = 5})
            .field(F(radius), {.min = 0.1f, .max = 2})
            .field(F(speed), {.min = 0, .max = 30})
            .field(F(jumpHeight), {.min = 0, .max = 10})
            .field(F(gravity), {.min = 0, .max = 60})
            .field(F(useInput), {.tooltip = "Move with WASD/arrow keys and jump with Space automatically."})
            .field(F(firstPerson), {.tooltip = "Look around with the mouse; the camera becomes the eyes."})
            .field(F(mouseSensitivity), {.min = 0.01f, .max = 1, .advanced = true})
            .field(F(velocity), {.runtime = true})
            .field(F(grounded), {.runtime = true});
    }
    {
        using Type = AudioSource;
        r.add<Type>("AudioSource", "Audio", "Plays a sound or music.")
            .field(F(clip), {.asset = AssetKind::Audio})
            .field(F(volume), {.min = 0, .max = 1})
            .field(F(pitch), {.min = 0.1f, .max = 3})
            .field(F(loop))
            .field(F(playOnStart))
            .field(F(spatial), {.tooltip = "Sound gets quieter further from the listener."})
            .field(F(range), {.min = 1, .max = 200});
    }
    {
        using Type = AudioListener;
        r.add<Type>("AudioListener", "Audio", "The 'ears' for 3D sound. Usually on the camera.", true);
    }
    {
        using Type = ParticleEmitter;
        r.add<Type>("ParticleEmitter", "Effects", "Sprays particles: fire, smoke, sparkles, explosions.")
            .field(F(emitting))
            .field(F(rate), {.min = 0, .max = 500})
            .field(F(burst), {.tooltip = "Particles released all at once when emission starts."})
            .field(F(maxParticles), {.advanced = true})
            .field(F(lifetime), {.min = 0.05f, .max = 10})
            .field(F(speed), {.min = 0, .max = 30})
            .field(F(spread), {.min = 0, .max = 180})
            .field(F(direction))
            .field(F(gravity))
            .field(F(shapeType), {.label = "Shape", .enumNames = {"Point", "Circle", "Sphere", "Box"}})
            .field(F(shapeSize), {.min = 0, .max = 10})
            .field(F(startColor))
            .field(F(endColor))
            .field(F(startSize), {.min = 0, .max = 5})
            .field(F(endSize), {.min = 0, .max = 5})
            .field(F(texture), {.asset = AssetKind::Image, .advanced = true})
            .field(F(additive), {.tooltip = "Glowing look (good for fire and magic)."})
            .field(F(worldSpace), {.advanced = true});
    }
    {
        using Type = CameraFollow;
        r.add<Type>("CameraFollow", "Gameplay", "Makes this object (usually the camera) follow a target smoothly.")
            .field(F(target))
            .field(F(offset))
            .field(F(smoothness), {.min = 0, .max = 20})
            .field(F(followX))
            .field(F(followY))
            .field(F(followZ))
            .field(F(lookAtTarget));
    }
    {
        using Type = UIElement;
        r.add<Type>("UIElement", "UI", "Pins this object to the screen (for menus, scores and buttons).")
            .field(F(anchor), {.enumNames = {"TopLeft", "Top", "TopRight", "Left", "Center", "Right", "BottomLeft",
                                             "Bottom", "BottomRight"}})
            .field(F(offset), {.step = 1})
            .field(F(size), {.step = 1})
            .field(F(order));
    }
    {
        using Type = UIText;
        r.add<Type>("UIText", "UI", "Text on the screen, like a score or a title.")
            .field(F(text), {.multiline = true})
            .field(F(fontSize), {.min = 8, .max = 200, .step = 1})
            .field(F(color))
            .field(F(align), {.enumNames = kAlign})
            .field(F(font), {.asset = AssetKind::Font, .advanced = true});
    }
    {
        using Type = UIImage;
        r.add<Type>("UIImage", "UI", "An image or colored panel on the screen.")
            .field(F(texture), {.label = "Image", .asset = AssetKind::Image})
            .field(F(shape), {.enumNames = kShapes2D})
            .field(F(color));
    }
    {
        using Type = UIButton;
        r.add<Type>("UIButton", "UI", "A clickable button. Scripts on this object get on_click().")
            .field(F(text))
            .field(F(fontSize), {.min = 8, .max = 200, .step = 1})
            .field(F(normalColor))
            .field(F(hoverColor))
            .field(F(pressedColor))
            .field(F(textColor))
            .field(F(hovered), {.runtime = true})
            .field(F(pressed), {.runtime = true});
    }
    {
        using Type = PrefabInstance;
        r.add<Type>("PrefabInstance", "Basics", "Links this object to the prefab it was created from.", true)
            .field(F(path), {.label = "Prefab", .asset = AssetKind::Prefab});
    }
    return std::move(r.list);
}

} // namespace

#undef F

const std::vector<ComponentInfo>& ComponentRegistry::all() {
    static const std::vector<ComponentInfo> registry = buildRegistry();
    return registry;
}

} // namespace aven
