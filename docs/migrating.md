# Moving to other engines

Everything you learn in Aven has a counterpart in the big engines. This page maps the ideas.
To map your own code, open the **Code Ladder** on a script and pick an engine: it translates
the script and notes where that engine works differently.

## The big ideas

| Aven | Unity | Godot | Roblox | Unreal |
|---|---|---|---|---|
| Object (in the Hierarchy) | GameObject | Node | Instance (Part, Model...) | Actor |
| Component | Component | Child node / node type | Child instance | Component |
| Scene (`.scene`) | Scene | Scene (`.tscn`) | Place | Level (map) |
| Prefab (`.prefab`) | Prefab | Packed scene | Model in ReplicatedStorage | Blueprint class |
| Script / EasyScript | C# `MonoBehaviour` | GDScript | Script / LocalScript (Luau) | C++ class or Blueprint |
| Behavior (settings, no code) | Ready-made components and assets | Built-in nodes | Studio plugins, free models | Blueprint components |
| Blocks | Visual Scripting | none built in | none built in | Blueprints |
| Native C (`aven.h`) | Native plugins | GDExtension | none | C++ modules |
| Tag (`other.tag`) | Tag | Groups | CollectionService tags | Actor tags |
| `game.score` | A static class / ScriptableObject | An autoload singleton | Values in ReplicatedStorage, leaderstats | GameInstance / GameState |
| `broadcast` / `send` | Events, `SendMessage` | Signals | BindableEvents | Delegates / Event dispatchers |
| Inspector | Inspector | Inspector | Properties | Details panel |
| Play-and-edit | Play mode (changes are lost) | Remote scene tree | Play/Run with Studio | Play in Editor, Simulate |

## Events

| Aven (EasyScript) | Unity (C#) | Godot (GDScript) | Roblox (Luau) | Unreal (C++) |
|---|---|---|---|---|
| `on_start()` | `Start()` | `_ready()` | code at the top of the script | `BeginPlay()` |
| `on_update(dt)` | `Update()` + `Time.deltaTime` | `_process(delta)` | `RunService.Heartbeat` | `Tick(DeltaTime)` |
| `on_fixed_update(dt)` | `FixedUpdate()` | `_physics_process(delta)` | `RunService.Stepped` | substepping / physics tick |
| `on_collide(other)` | `OnCollisionEnter2D` | `body_entered` signal | `Touched` event | `OnComponentHit` |
| `on_trigger(other)` | `OnTriggerEnter2D` | `Area2D.body_entered` | `Touched` on a non-colliding part | `NotifyActorBeginOverlap` |
| `on_click()` | `OnMouseDown()` / UI Button | `input_event` / Button `pressed` | `ClickDetector.MouseClick` | `OnClicked` |
| `on_destroy()` | `OnDestroy()` | `_exit_tree()` | `Destroying` event | `EndPlay()` |
| `wait(1)` | coroutine `yield return new WaitForSeconds(1)` | `await get_tree().create_timer(1).timeout` | `task.wait(1)` | Timer / latent action |

## Common lines

| EasyScript | Unity | Godot | Roblox | Unreal |
|---|---|---|---|---|
| `self.x += 2 * dt` | `transform.position += new Vector3(2 * Time.deltaTime, 0, 0);` | `position.x += 2 * delta` | `part.Position += Vector3.new(2 * dt, 0, 0)` | `AddActorWorldOffset(FVector(2 * DeltaTime, 0, 0));` |
| `self.destroy()` | `Destroy(gameObject);` | `queue_free()` | `part:Destroy()` | `Destroy();` |
| `find("Player")` | `GameObject.Find("Player")` | `get_node("/root/Main/Player")` | `workspace:FindFirstChild("Player")` | `UGameplayStatics::GetActorOfClass(...)` |
| `spawn("prefabs/coin.prefab", x, y)` | `Instantiate(coinPrefab, pos, Quaternion.identity)` | `coin_scene.instantiate()` + `add_child` | `coin:Clone()` + set `Parent` | `GetWorld()->SpawnActor<ACoin>(...)` |
| `key_down("left")` | `Input.GetKey(KeyCode.LeftArrow)` | `Input.is_action_pressed("ui_left")` | `UserInputService:IsKeyDown(Enum.KeyCode.Left)` | Enhanced Input actions |
| `play_sound("sounds/hit.wav")` | `AudioSource.PlayClipAtPoint(clip, pos)` | an `AudioStreamPlayer`'s `play()` | a `Sound`'s `:Play()` | `UGameplayStatics::PlaySound2D` |

## Differences to expect

- **Types.** C#, C++ and C need declared types (`float speed = 5f;`). GDScript and Luau, like
  EasyScript, don't.
- **Units.** Unity uses meters and Unreal centimeters. Godot 2D uses pixels. Roblox uses studs.
  Aven uses one unit per tile or meter.
- **Angles.** EasyScript's `sin()`/`cos()` take degrees. Most languages' math libraries take
  radians. The Code Ladder converts for you.
- **Lists start at 0** everywhere here except Luau, where they start at 1.
- **Assets.** Aven reads files straight from the project folder. Unity, Godot and Unreal import
  them into their own formats with import settings.
- **Multiplayer.** Roblox is multiplayer-first (server scripts vs client scripts). Aven, like a
  fresh Unity or Godot project, is single-player unless you add networking.
