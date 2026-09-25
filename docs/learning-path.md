# The learning path

Aven is built to be outgrown. It starts simple, stays useful as you learn, and hands you over to
the big engines knowing how they think. There are two ladders: how much of the editor you see,
and how you write game logic.

## Editor levels

The **Level** button (top right) picks how much of the editor is shown. Nothing is removed when
you go up; more tools appear. Aven suggests the next level once you've played and built enough.
You can always choose any level yourself.

| Level | What it adds |
|---|---|
| **Starter** | Game recipes, Ask Aven, Explain, the Error Doctor, blocks, ready-made behaviors, the Pixel Editor, sharing, lessons |
| **Explorer** | Assets, Console, physics, sound (plus the Sound Maker), particles, screen UI, prefabs, tilemaps, sprite animation, lighting, project settings, screenshots and GIFs |
| **Creator** | EasyScript code, the Code Ladder, build & export, undo history, find in project, Bug Replay, the command palette, post-processing, more scenes |
| **Pro** | Every advanced setting, the profiler, Contributor Quests, native C/C++ code, custom shortcuts |

## The Code Ladder

Every way of writing behavior in Aven is a rung. The **Code Ladder** window shows one script on
every rung at once, so you can climb a step at a time:

1. **Behaviors.** Settings, no code. A "Collectible" with *counter: coins, amount: 1*.
2. **Blocks.** "when touched by player → change coins by 1 → destroy". Right-click a behavior and
   choose **Show as code** to see it as blocks or EasyScript.
3. **EasyScript.** The same thing as text you can edit line by line:
   ```easyscript
   def on_trigger(other):
       if other.tag == "player":
           game.coins += 1
           self.destroy()
   ```
4. **C, in Aven.** A native module: the same events and names, now with types, structs and
   `strcmp`. It runs in your game, so you can learn C without leaving a project you know.
5. **Other engines.** The same script as C# for Unity, GDScript for Godot, Luau for Roblox and C++
   for Unreal, each in that engine's usual style, with notes on where the engines differ.

Each rung also has a button to move your project up. **Turn the behavior into this script**
replaces a behavior with editable EasyScript. **Switch to EasyScript** converts blocks. **Save to
native/src and build** makes a C behavior.

## A suggested order

1. **Play and tweak.** Open a template, play it, pause, and change numbers in the Inspector.
   Use Explain to see how it works.
2. **Build something small from a recipe**, then add your own art in the Pixel Editor and sounds
   in the Sound Maker.
3. **Blocks.** Give an object a blocks script: move with keys, collect things, keep score.
4. **EasyScript.** Switch a blocks script to EasyScript and keep going in text: your own
   functions, messages between objects, `wait()` for sequences. The
   [EasyScript guide](easyscript.md) covers the language.
5. **Structure.** Prefabs, several scenes, screen UI, save data. Export your game and share it.
6. **C.** Take one behavior to the C rung. Read the struct, the events and the `aven_*` calls. Then
   write one yourself. See [native code](native-code.md).
7. **Another engine.** Open the Code Ladder on a script you know well and read its Unity or Godot
   version next to it. [Moving to other engines](migrating.md) maps the ideas across.

## When you're ready to move on

Aven's concepts are the ones every engine shares: objects with components, a scene tree, events
(update, collide), prefabs, physics bodies, cameras and materials. Things you'll need to learn
fresh elsewhere: each engine's editor, its asset import pipeline, its build system, and (in Unity
and Unreal) a stricter, typed language. The C rung and the Code Ladder are the bridge to that.
