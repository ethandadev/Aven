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
            .field(F(grounded), {.runtime = true})
            .field(F(pitch), {.runtime = true});
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
    // ---------------------------------------------------------------- behaviors
    const std::vector<std::string> kAxes{"X", "Y", "Z"};
    {
        using Type = Patrol;
        r.add<Type>("Patrol", "Behaviors", "Walks back and forth. Great for enemies and moving platforms.")
            .field(F(axis), {.tooltip = "X is left/right, Y is up/down, Z is forward/back (3D).", .enumNames = kAxes})
            .field(F(distance), {.tooltip = "How far it goes to each side.", .min = 0.1f, .max = 50})
            .field(F(speed), {.min = 0, .max = 30})
            .field(F(flipSprite), {.tooltip = "Mirror the image to face the way it walks."});
    }
    {
        using Type = Chase;
        r.add<Type>("Chase", "Behaviors", "Moves toward the nearest object with a tag, like the player. Can also run away.")
            .field(F(targetTag), {.tooltip = "Which objects to chase (their Tag)."})
            .field(F(speed), {.min = 0, .max = 30})
            .field(F(sight), {.tooltip = "Only notices targets closer than this.", .min = 0.5f, .max = 100})
            .field(F(stopDistance), {.min = 0, .max = 10})
            .field(F(runAway), {.tooltip = "Move away instead (for scared animals)."})
            .field(F(flipSprite));
    }
    {
        using Type = Spin;
        r.add<Type>("Spin", "Behaviors", "Keeps turning around. Degrees per second for each axis (2D uses Z).")
            .field(F(speed), {.step = 1});
    }
    {
        using Type = Bob;
        r.add<Type>("Bob", "Behaviors", "Floats gently up and down, like a coin or a ghost.")
            .field(F(height), {.min = 0, .max = 5})
            .field(F(speed), {.tooltip = "Bounces per second.", .min = 0.05f, .max = 10});
    }
    {
        using Type = Collectible;
        r.add<Type>("Collectible", "Behaviors", "Picked up when the player touches it: adds to a counter (game.score) and disappears.")
            .field(F(collectorTag), {.tooltip = "Who can pick it up (their Tag)."})
            .field(F(counter), {.tooltip = "The game variable to add to, e.g. score, coins, gems."})
            .field(F(amount))
            .field(F(sound), {.asset = AssetKind::Audio})
            .field(F(sparkle), {.tooltip = "A little burst of particles when collected."});
    }
    {
        using Type = Hazard;
        r.add<Type>("Hazard", "Behaviors", "Hurts what touches it: spikes, lava, enemies. Needs Health on the victim, otherwise the scene restarts.")
            .field(F(victimTag), {.tooltip = "Who gets hurt (their Tag)."})
            .field(F(damage), {.min = 0, .max = 100})
            .field(F(knockback), {.tooltip = "How hard the victim is pushed away.", .min = 0, .max = 30})
            .field(F(vanishOnHit), {.tooltip = "Disappears after hurting something (for bullets and thrown things)."});
    }
    {
        using Type = Health;
        r.add<Type>("Health", "Behaviors", "Hit points. Hazards take them away; when they run out, something happens.")
            .field(F(maxHealth), {.min = 1, .max = 1000})
            .field(F(invincibleTime), {.tooltip = "Seconds of blinking safety after getting hurt.", .min = 0, .max = 10})
            .field(F(whenZero), {.enumNames = {"RestartScene", "Respawn", "Destroy", "Nothing"}})
            .field(F(counter), {.tooltip = "Also keeps game.<counter> up to date, for hearts on screen. Empty = off."})
            .field(F(current), {.runtime = true});
    }
    {
        using Type = Lifetime;
        r.add<Type>("Lifetime", "Behaviors", "Disappears after a while. Good for bullets, effects and pop-up text.")
            .field(F(seconds), {.min = 0.05f, .max = 120})
            .field(F(fadeOut));
    }
    {
        using Type = WrapAround;
        r.add<Type>("WrapAround", "Behaviors", "Going off one edge of the screen brings it back on the other side (2D).", false, true)
            .field(F(margin), {.min = 0, .max = 5});
    }
    {
        using Type = Shooter;
        r.add<Type>("Shooter", "Behaviors", "Fires copies of a prefab when a key is pressed: bullets, lasers, fireballs.")
            .field(F(prefab), {.tooltip = "What to fire. Make one with right-click > Save as Prefab.", .asset = AssetKind::Prefab})
            .field(F(action), {.tooltip = "Input action (see Project Settings) or key name, e.g. fire, space, z."})
            .field(F(bulletSpeed), {.min = 0, .max = 100})
            .field(F(cooldown), {.tooltip = "Seconds between shots.", .min = 0, .max = 5})
            .field(F(aim), {.enumNames = {"Up", "Right", "Facing", "Mouse"}})
            .field(F(offset), {.tooltip = "Where bullets appear, relative to this object."})
            .field(F(sound), {.asset = AssetKind::Audio});
    }
    {
        using Type = PlatformerController;
        r.add<Type>("PlatformerController", "Behaviors", "Run with the arrow keys or A/D, jump with Space. Adds physics automatically.", false, true)
            .field(F(speed), {.min = 0, .max = 30})
            .field(F(jumpPower), {.min = 0, .max = 40})
            .field(F(extraJumps), {.tooltip = "1 allows a double jump.", .min = 0, .max = 5})
            .field(F(coyoteTime), {.tooltip = "A moment after running off a ledge when jumping still works.", .min = 0, .max = 0.5f,
                                   .advanced = true})
            .field(F(flipSprite))
            .field(F(jumpSound), {.asset = AssetKind::Audio});
    }
    {
        using Type = TopDownController;
        r.add<Type>("TopDownController", "Behaviors", "Walk in all directions with the arrow keys or WASD (top-down games).", false, true)
            .field(F(speed), {.min = 0, .max = 30})
            .field(F(faceMovement), {.tooltip = "Turn to face the way it moves."});
    }
    {
        using Type = FollowMouse;
        r.add<Type>("FollowMouse", "Behaviors", "Follows the mouse pointer.")
            .field(F(smoothness), {.tooltip = "0 snaps instantly; higher is smoother.", .min = 0, .max = 30})
            .field(F(onlyWhileHeld), {.tooltip = "Only follow while the mouse button is held."});
    }
    {
        using Type = Draggable;
        r.add<Type>("Draggable", "Behaviors", "Can be picked up and moved with the mouse while playing (puzzles, card games).")
            .field(F(snapToGrid))
            .field(F(gridSize), {.min = 0.05f, .max = 10});
    }
    {
        using Type = Spawner;
        r.add<Type>("Spawner", "Behaviors", "Creates copies of a prefab over and over: enemies, falling rocks, coins.")
            .field(F(prefab), {.asset = AssetKind::Prefab})
            .field(F(interval), {.tooltip = "Seconds between copies.", .min = 0.05f, .max = 60})
            .field(F(maxAlive), {.tooltip = "Stops making more while this many exist.", .min = 1, .max = 500})
            .field(F(randomRange), {.tooltip = "Copies appear up to this far from the spawner (x, y)."});
    }
    {
        using Type = Clickable;
        r.add<Type>("Clickable", "Behaviors", "Clicking it adds to a counter. The heart of every clicker game.")
            .field(F(counter))
            .field(F(amount))
            .field(F(sound), {.asset = AssetKind::Audio})
            .field(F(bounce), {.tooltip = "Squish a little when clicked."});
    }
    {
        using Type = ScoreDisplay;
        r.add<Type>("ScoreDisplay", "Behaviors", "Shows a game counter as text. {} is replaced by the number.")
            .field(F(counter))
            .field(F(format));
    }
    {
        using Type = SceneLink;
        r.add<Type>("SceneLink", "Behaviors", "Goes to another scene: doors, portals, level ends and Start buttons.")
            .field(F(scene), {.asset = AssetKind::Scene})
            .field(F(when), {.enumNames = {"Touch", "Click", "AfterTime"}})
            .field(F(tag), {.tooltip = "For Touch: who has to touch it."})
            .field(F(delay), {.tooltip = "Seconds to wait first (for AfterTime, or a pause after touching).", .min = 0, .max = 60});
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
