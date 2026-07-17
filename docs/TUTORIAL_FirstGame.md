# Tutorial — Build & Ship Your First Game: "Coin Rush"

This tutorial builds a complete little game from an empty scene to a packaged,
double-clickable executable. You'll make a third-person character who runs around an
arena collecting coins while a patrolling enemy chases them. Collect all the coins to
**win**; let the enemy drain your health to zero and you **lose**.

Along the way you'll touch every part of the pipeline: authoring in the editor, a
player controller, gameplay scripts, behaviour-tree AI, a HUD, the `GameMode` rules
layer, exporting, and packaging.

> Read **[ENGINE_MANUAL.md](ENGINE_MANUAL.md)** alongside this for reference on each
> system. Menu names and panels refer to the `3DGEditor`.

**What you'll build**

1. An arena scene (ground + walls + lights).
2. A third-person player.
3. Coins that add score when collected.
4. An enemy that patrols and chases with a behaviour tree.
5. A HUD (health bar + score + timer).
6. Win/lose rules via `GameMode`.
7. A playtest, then an exported, packaged, standalone build.

---

## 0. Prerequisites

Build and run the editor once:

```bash
cmake -S . -B build
cmake --build build --target 3DGEditor
./build/bin/3DGEditor        # (3DGEditor.exe on Windows)
```

Create a working folder for your game's content, e.g. `mygame/`, with an `assets/`
subfolder. Save your scene and HUD there.

---

## 1. The arena scene

1. **File → New Scene.**
2. **Add → Plane.** Scale it to ~`(20, 1, 20)` in the inspector — this is the ground.
   In the **Physics** section of the inspector, add a **Collider** (Plane) so things
   stand on it.
3. Add four **Box** walls around the edge (Add → Cube, scale/position them into low
   walls). Give each a **Box collider** so the player and enemy can't walk off.
4. **Add → Directional Light** for the sun. Open **World Settings** and set a pleasant
   **Time of Day**; enable **Sun Shadows**. Optionally turn on fog.
5. **File → Save Scene** as `mygame/arena.3dgscene` (the editor's own format).

You now have a lit, walled arena you can fly around in the viewport.

---

## 2. The player

1. **Add → Player Start.** This creates an object named `PlayerStart` with a
   **Player Controller** component.
2. In the inspector's **Player Controller** section, set it up as a third-person
   character: leave **First Person** off, set **Walk Speed** ~`4`, **Run Speed** ~`7`,
   **Jump Speed** ~`5`, and a **Capsule** radius/height that fits a humanoid (0.4 / 1.8).
   Tune the **Camera Distance** and **Target Height** for a comfortable follow cam.
3. In the **Gameplay** section, add a **Health** component (e.g. `100 / 100`). This is
   what the enemy will damage and what drives the lose condition.
4. Position the player near the centre of the arena.

Press **Play** (`P`). You control the capsule with **WASD**, **Space** to jump,
**Shift** to sprint, **V** to toggle first/third person, and the mouse to look. Press
`P` again to stop.

---

## 3. Coins

Coins are trigger volumes the player walks through.

1. **Add → Sphere.** Scale it to ~`0.4`, give it a bright emissive material in the
   **Material Maker** (or just a yellow colour), and name it `Coin_1`.
2. In **Physics**, add a **Sphere Collider** and tick **Trigger** (overlap-only — it
   detects the player without blocking them).
3. In **Gameplay**, add a **Script** component and set its class name to `Coin`
   (we'll write the `Coin` class in §5).
4. Duplicate the coin (Ctrl-D) several times and scatter them around the arena:
   `Coin_2`, `Coin_3`, … Keep them all named with the `Coin_` prefix and the `Coin`
   script.

---

## 4. The enemy

1. **Add → Capsule**, name it `Enemy`, give it a red material and a **Capsule
   Collider**. Add a **Health** component so it exists as a combatant.
2. Give it a **Rigid Body** set to **kinematic** (it's driven by the AI, not by
   gravity) — or leave it as a scripted mover; for this tutorial the behaviour tree
   moves its transform.
3. Open the **Behavior Graph** panel and build a simple brain: a **Selector** with two
   branches —
   - **Chase:** a condition "player in range" → a **MoveTo** task targeting the player,
     plus a service that damages the player's `Health` when close.
   - **Patrol:** otherwise, a **Patrol** task cycling between a few waypoints.

   Save it as `mygame/assets/enemy.btgraph` and assign it to the `Enemy` object's
   **AI Brain** field. Set the enemy's **team/faction** so it targets the player.

The engine ships example behaviour-tree scripts (`StrafeTarget`, `FaceTarget`,
`NearTarget`); you can add your own — see §5.

---

## 5. The scripts

Gameplay logic lives in C++ `engine::Script` classes referenced by name. Put them in the
shared **game module** — `game/include/game/scripts/` — so the same code runs in the
editor's Play mode and in the shipped player (you register them once; see §9). Create two
headers there.

**`Coin.h`** — when the player overlaps this coin, add score and disappear:

```cpp
#pragma once
#include <engine/gameplay/Script.h>
#include <engine/gameplay/GameMode.h>
#include <engine/ecs/Components.h>

class Coin : public engine::Script {
public:
    void OnUpdate(float) override {
        // Find the player (the entity named "PlayerStart") and test proximity.
        engine::ecs::Entity player = FindByName("PlayerStart");
        if (player == engine::ecs::kNull) return;

        const glm::vec3 me     = Get<engine::ecs::Transform>().position;
        const glm::vec3 target = Registry().Get<engine::ecs::Transform>(player).position;
        if (glm::length(target - me) < 1.0f) {
            engine::GameMode::Instance().AddScore(10);
            Destroy();                     // remove this coin entity
        }
    }
};
```

> The exact accessor names (`Get<T>()`, `Registry()`, `FindByName()`, `Destroy()`)
> follow `engine/gameplay/Script.h`'s `ScriptContext`. The pattern is: reach your own
> components, query the registry for others, and call `engine::GameMode::Instance()`.
> A cleaner alternative is to use the coin's **trigger collision event** instead of a
> distance check.

**`GoalTracker.h`** — attach this to one manager object; it wins the game when no coins
remain:

```cpp
#pragma once
#include <engine/gameplay/Script.h>
#include <engine/gameplay/GameMode.h>
#include <engine/ecs/Components.h>

class GoalTracker : public engine::Script {
public:
    void OnUpdate(float) override {
        int coins = 0;
        Registry().view<engine::ecs::RuntimeName>().each(
            [&](engine::ecs::Entity, engine::ecs::RuntimeName& n) {
                if (n.value.rfind("Coin_", 0) == 0) ++coins;
            });
        if (coins == 0 && engine::GameMode::Instance().IsPlaying())
            engine::GameMode::Instance().Win("All coins collected!");
    }
};
```

Add a `GameManager` empty object to the scene with a **Script** component set to
`GoalTracker`. (The lose condition is automatic: `GameMode`'s built-in rule ends the run
when the player's `Health` hits zero.)

We'll register these classes in §9 so both the editor and the player can instantiate
them.

---

## 6. The HUD

1. Open the **HUD Editor** panel and click **New**.
2. Add a **Bar** widget, anchor it **Top Left**, set its **Binding → Health Fraction**.
   This is the player's health bar.
3. Add a **Text** widget anchored **Top Right**, **Binding → Named String**, key
   `score`, and text `Score: {}` (the `{}` is replaced by the bound value).
4. Add another **Text** for the timer: Named Float key `time`, text `Time {}`.
5. Optionally add a **Button** anchored centre with action **Restart Play** for a
   restart control (works when the cursor is free — after game over).
6. **Save** the HUD as `mygame/assets/hud.hud`, then click **Use in Scene** so the
   scene references it.

During play the runtime feeds the HUD live values: `score`, `time`, `gamestate`,
`gamemessage`, plus the player's health.

---

## 7. Win & lose rules

You've already wired both:

- **Lose** — automatic. `GameMode`'s `loseOnPlayerDeath` rule flips the state to
  **GameOver** when the player's `Health` dies (the enemy's behaviour tree drains it).
- **Win** — the `GoalTracker` script calls `GameMode::Instance().Win(...)` when the last
  coin is gone.

When the state leaves *Playing*, the runtime freezes the simulation and shows a centred
**VICTORY / GAME OVER** screen with the score and a "Press R to restart" prompt.

---

## 8. Playtest in the editor

Press **Play**. Run around, collect coins (score ticks up, health bar reacts to the
enemy), and confirm:

- collecting the last coin triggers **VICTORY**,
- letting your health reach zero triggers **GAME OVER**,
- `R` restarts.

Iterate in the editor until it feels right. The Play-mode simulation is the same code
the shipped game runs, so this is a faithful preview.

> If the HUD shows "*N scripts not registered*", register your classes in the game module
> (§9) and rebuild — the editor links it, so Play mode picks them up automatically (no
> separate editor registration needed).

---

## 9. Register the scripts

A scene stores scripts by class name, so the classes must be compiled and registered.
Both the editor and the player link the shared **game module** (`game/`), so you do this
**once** there — it runs in Play mode *and* in the shipped game.

Since you put `Coin.h` and `GoalTracker.h` in `game/include/game/scripts/` (§5), just
`#include` them and add a `Register(...)` line in `game/src/GameModule.cpp`:

```cpp
// game/src/GameModule.cpp
#include "game/GameModule.h"
#include <engine/gameplay/Script.h>
#include "game/scripts/Coin.h"
#include "game/scripts/GoalTracker.h"
#include <memory>

void RegisterGameModule() {
    auto& s = engine::ScriptRegistry::Instance();
    s.Register("Coin",        [] { return std::make_unique<Coin>(); });
    s.Register("GoalTracker", [] { return std::make_unique<GoalTracker>(); });
}
```

That's it — no editing of `player/CMakeLists.txt` or the editor's script hook, and no
duplicate registration. (If a script has a `.cpp`, list it in `game/CMakeLists.txt`.)

---

## 10. Export the runtime scene

In the editor, **Export** the scene (menu / `F7`). This writes a `3DGRuntimeScene` file —
say `mygame/arena.3dgscene` (runtime) — containing everything the player needs:
transforms, meshes, lights, physics, colliders, the player-controller settings, scripts,
the enemy's brain, and the **HUD reference**. Keep it in `mygame/` next to `assets/` so
its relative asset paths resolve.

---

## 11. Point the player at your scene

Create/edit `player/player.cfg` (it ships beside the executable):

```ini
window.width = 1280
window.height = 720
window.vsync = true
player.scene = game/arena.3dgscene
```

For a dev run you can also pass the scene directly:

```bash
cmake --build build --target player
./build/bin/player mygame/arena.3dgscene
```

Run it from your content root (or bundle assets to match) so material/texture paths
resolve; the player shows an on-screen count if any assets fail to load.

---

## 12. Package & ship

Stage a self-contained folder and zip it. Put your exported scene + `assets/` into a
folder and point `PLAYER_GAME_DIR` at it:

```bash
cmake -S . -B build -DPLAYER_GAME_DIR=E:/path/to/mygame -DGAMEENGINE_STATIC_RUNTIME=ON
cmake --build build --target player
cmake --install build --prefix dist --component player
cd build && cpack
```

Result:

```
dist/
  player.exe          # your game
  player.cfg          # window + startup scene (game/arena.3dgscene)
  game/               # your exported scene + assets/ + hud.hud
```

`3DGPlayer-<ver>-<os>.zip` is your shippable build. Double-click `player.exe`: the arena
loads, the HUD appears, coins can be collected, the enemy chases, and win/lose work —
with no editor in sight.

---

## 13. Where to go next

- **More enemies / smarter AI** — richer behaviour trees, factions, navmesh chasing.
- **Juice** — particle bursts on coin pickup, an impact sound via `TriggerAudioAction`,
  camera shake on damage, a coin spin via a **Rotator**.
- **Levels** — export several scenes and switch between them (main menu → level 1 → …)
  driven by scripts + `GameMode`.
- **Polish** — a proper pause menu with HUD **Buttons**, a settings screen, gamepad
  input.

You've now taken a game from an empty scene to a packaged executable using the editor,
scripts, AI, a HUD, and the game-rules layer. That's the full loop — everything else is
building bigger with the same pieces.
