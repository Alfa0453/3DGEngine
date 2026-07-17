# Tutorial — "Ember": A Fireball Spell (Editor + Scripts)

Build a spellcaster game **entirely in the editor**, with gameplay in **scripts** — no
hand-written `Application`, no standalone C++ game loop. You author the scene, the player,
and the fireball VFX in `3DGEditor`, write small `engine::Script` classes for the casting
and flight logic, play it in **Play mode**, then **export** and **package** it as a
standalone game — the same pipeline as
**[TUTORIAL_FirstGame.md](TUTORIAL_FirstGame.md)**.

The star is the engine's **particle system**, authored in the **Particle Editor** and
spawned by a script when you cast.

**What you'll build**

1. An arena scene with a third-person player.
2. A **fireball VFX** authored in the Particle Editor (HDR additive fire).
3. A **Fireball prototype** object (emissive core + fire particles + trigger) that a
   script flies forward and bursts on impact.
4. A **cast** script on the player: left-click spawns a fireball, with a whoosh and an
   impact bang.
5. Target dummies that pop when hit — then export & ship.

> Reference: **[ENGINE_MANUAL.md](ENGINE_MANUAL.md)** §4 (rendering), §11 (particles),
> §6 (scripts). Menu names refer to `3DGEditor`.

---

## 0. Prerequisites

Build and run the editor, and make a `mygame/` content folder with `assets/`.

```bash
cmake -S . -B build
cmake --build build --target 3DGEditor
./build/bin/3DGEditor
```

---

## 1. The arena and the mage

1. **File → New Scene.**
2. **Add → Plane**, scale ~`(20,1,20)`, add a **Plane collider** (Physics section) — the ground.
3. Add a few **Box** walls/pillars with **Box colliders** for the fireball to splash against.
4. **Add → Player Start** — this is your mage. In its **Player Controller** section, set a
   comfortable third-person camera (Walk ~4, Run ~7, Camera Distance ~6). Give it a
   **Health** component if you want it to take damage later.
5. **Add → Directional Light**; in **World Settings** set an evening **Time of Day**
   (darker sells the glow) and enable **Sun Shadows**.
6. **Save Scene** as `mygame/arena.3dgscene`.

Press **Play (P)**: you can already run the mage around with WASD + mouse-look.

---

## 2. Author the fireball VFX in the Particle Editor

1. Open the **Particle Editor** panel and create a new effect. This edits a
   **Particle System** (an emitter you tune with the same fields the engine uses).
2. Dial in **fire** (these are the emitter's properties in the panel):

   | Property | Value | Why |
   |---|---|---|
   | Blend | **Additive** | flames add light; overlaps get brighter |
   | Start Color | HDR orange `(3.6, 1.7, 0.35, 1)` | `>1` values **bloom** |
   | End Color | dark red `(1.3, 0.08, 0, 0)` | fades out (alpha → 0) |
   | Shape / Radius | **Sphere**, `0.13` | a puffy round core |
   | Speed Min/Max | `0.3 / 1.3` | slow drift → the trail lags the head |
   | Life Min/Max | `0.30 / 0.70` | short → the tail fades fast |
   | Gravity | `(0, 1.2, 0)` | *upward* → buoyant flames |
   | Drag | `2.2` | air resistance, puffy |
   | Start/End Size | `0.28 → 0.03` | shrink as they die |
   | Rate | `420` | dense trail while flying |
   | Max Particles | `1500` | pool size |

3. **Save** the effect as `mygame/assets/fireball.particle` (or your project's particle
   asset extension). You now have a reusable fire emitter you can drop on any entity.

Make a second, one-shot effect for the **explosion**: same fire colours, `Rate = 0`
(burst-only), a bigger `Start Size`, `Shape = Sphere` radius ~`0.4`, and a **Burst** of
~90 particles. Save it as `mygame/assets/explosion.particle`.

---

## 3. Build the Fireball and Explosion prototypes

Scripts spawn *copies of named scene objects*, so we author the fireball once as a
**prototype** object and stamp copies at runtime.

**Fireball prototype:**

1. **Add → Sphere**, scale ~`0.15`, name it exactly **`Fireball`**. Give it a bright
   emissive material (Material Maker) so the core glows.
2. In the inspector, add a **Particle System** component and point it at
   `assets/fireball.particle` (set it to **Play** / autoplay).
3. Add a **Sphere Collider** with **Trigger** ticked (so it detects hits without shoving
   things).
4. Add a **Script** component with class name **`FireballProjectile`** (written in §4).
5. Move it out of sight (e.g. far below the floor) — it's a template, not a placed prop.

**Explosion prototype:**

1. **Add → Sphere**, scale ~`0.1`, name it **`Explosion`**, emissive.
2. **Particle System** → `assets/explosion.particle` (autoplay, one-shot).
3. **Script** component → class name **`Despawn`** (removes itself after a moment).
4. Tuck it out of sight too.

**Targets:** add a few **Box** objects named `Target_1`, `Target_2`, … around the arena
for the fireball to smash.

---

## 4. The scripts

Three tiny `engine::Script` classes. Put their headers in the shared **game module** —
`game/include/game/scripts/` — so they run in both the editor and the player (registered
once; see §7).

**`FireballCaster.h`** — on the mage; left-click spawns a fireball aimed by facing:

```cpp
#pragma once
#include <engine/gameplay/Script.h>
#include <engine/ecs/Components.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/quaternion.hpp>

class FireballCaster : public engine::Script {
public:
    void OnUpdate(float) override {
        if (!WasMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT)) return;
        const engine::ecs::Transform* me = Transform();
        if (!me) return;

        // Spawn a fireball a little in front of the mage's chest.
        const glm::vec3 fwd = me->rotation * glm::vec3(0.0f, 0.0f, 1.0f);   // capsule forward
        const glm::vec3 tip = me->position + glm::vec3(0.0f, 0.9f, 0.0f) + fwd * 0.6f;
        engine::ecs::Entity fb = SpawnFromObject("Fireball", tip);

        // Aim it: the projectile script flies along the copy's forward.
        if (auto* t = Registry()->TryGet<engine::ecs::Transform>(fb)) t->rotation = me->rotation;

        PlayAudioCue("assets/sounds/whoosh.wav");   // spatial one-shot at the mage
    }
};
```

**`FireballProjectile.h`** — on the fireball; fly forward, burst on a target or at range:

```cpp
#pragma once
#include <engine/gameplay/Script.h>
#include <engine/ecs/Components.h>

class FireballProjectile : public engine::Script {
public:
    void OnUpdate(float dt) override {
        engine::ecs::Transform* t = Transform();
        if (!t) return;
        const glm::vec3 fwd = t->rotation * glm::vec3(0.0f, 0.0f, 1.0f);
        t->position += fwd * (14.0f * dt);        // fly
        m_dist += 14.0f * dt;

        // Hit any Target_* object?
        engine::ecs::Entity hit = engine::ecs::kNull;
        Registry()->view<engine::ecs::Transform, engine::ecs::RuntimeName>().each(
            [&](engine::ecs::Entity e, engine::ecs::Transform& tt, engine::ecs::RuntimeName& n) {
                if (hit == engine::ecs::kNull && n.value.rfind("Target_", 0) == 0
                    && glm::length(tt.position - t->position) < 1.3f) hit = e;
            });

        if (hit != engine::ecs::kNull || m_dist > 30.0f) {   // impact or fizzle
            SpawnFromObject("Explosion", t->position);
            PlayAudioCue("assets/sounds/impact.wav");
            if (hit != engine::ecs::kNull) Destroy(hit);      // pop the target
            DestroySelf();
        }
    }
private:
    float m_dist = 0.0f;
};
```

**`Despawn.h`** — removes the explosion after its burst plays out:

```cpp
#pragma once
#include <engine/gameplay/Script.h>

class Despawn : public engine::Script {
public:
    void OnCreate() override { Delay(1.0f, [this]{ DestroySelf(); }); }
};
```

> These use the real `engine::Script` API (`WasMouseButtonPressed`, `Transform()`,
> `Registry()`, `SpawnFromObject`, `Destroy`/`DestroySelf`, `Delay`, `PlayAudioCue`) —
> see `engine/gameplay/Script.h`. The `+Z` forward convention matches the capsule/sphere
> meshes; if your fireball flies backward, negate it.

---

## 5. Sound: whoosh, impact, footsteps

You already trigger sound from the scripts: `PlayAudioCue("assets/sounds/whoosh.wav")` on
cast and `"impact.wav"` on hit. `PlayAudioCue` plays a **spatial** one-shot at the
script entity's position, attenuating from the listener (the play camera).

Drop `whoosh.wav` and `impact.wav` into `mygame/assets/sounds/`. For **footsteps**, add a
tiny `Footsteps` script to the mage that plays a step cue on a timer while moving:

```cpp
#pragma once
#include <engine/gameplay/Script.h>
class Footsteps : public engine::Script {
public:
    void OnUpdate(float dt) override {
        const engine::ecs::Transform* t = Transform();
        static glm::vec3 last{0.0f};
        const bool moving = t && glm::length(t->position - last) > 0.01f;
        if (t) last = t->position;
        m_timer -= dt;
        if (moving && m_timer <= 0.0f) { PlayAudioCue("assets/sounds/footstep.wav"); m_timer = 0.42f; }
    }
private:
    float m_timer = 0.0f;
};
```

Alternatively, author sound **without scripts**: put an **Audio Source** component on an
object for looping/positional ambience, or a **Trigger Audio** component to play a sound
on collision/trigger enter — both are inspector components, no code.

---

## 6. Playtest

Press **Play**. Move with WASD, mouse-look, and **left-click** to cast. A glowing fireball
streaks from the mage with a bloom-lit fire trail; when it reaches a `Target_` box it
pops, an explosion bursts at the hit point, and you hear the whoosh and the bang. Iterate
on the effect in the Particle Editor while it's running.

> If the HUD shows a "*scripts not registered*" note, register the classes in the game
> module (§7) and rebuild — the editor links it, so Play mode picks them up automatically.

---

## 7. Export, register, and ship

Same pipeline as the first tutorial:

1. **Export** the scene (F7) → `mygame/arena.3dgscene` (runtime), keeping it beside
   `assets/` so the particle/sound paths resolve.
2. **Register the scripts** once in the shared **game module** — `game/src/GameModule.cpp`
   — which both the editor and the player link (no per-target edits, no duplication):

   ```cpp
   // game/src/GameModule.cpp
   #include "game/GameModule.h"
   #include <engine/gameplay/Script.h>
   #include "game/scripts/FireballCaster.h"
   #include "game/scripts/FireballProjectile.h"
   #include "game/scripts/Despawn.h"
   #include "game/scripts/Footsteps.h"
   #include <memory>

   void RegisterGameModule() {
       auto& s = engine::ScriptRegistry::Instance();
       s.Register("FireballCaster",     []{ return std::make_unique<FireballCaster>(); });
       s.Register("FireballProjectile", []{ return std::make_unique<FireballProjectile>(); });
       s.Register("Despawn",            []{ return std::make_unique<Despawn>(); });
       s.Register("Footsteps",          []{ return std::make_unique<Footsteps>(); });
   }
   ```
3. **Package:**

   ```bash
   cmake -S . -B build -DPLAYER_GAME_DIR=E:/path/to/mygame -DGAMEENGINE_STATIC_RUNTIME=ON
   cmake --build build --target player
   cmake --install build --prefix dist --component player
   cd build && cpack
   ```

Double-click `dist/player.exe`: your arena loads and you can cast fireballs — no editor,
no hand-written game loop, just the scene, the Particle Editor effect, and the scripts.

---

## 8. Make it feel great — next steps

- **Impact light.** Put a bright, short-lived **Point Light** on the `Explosion` prototype
  so the burst actually lights the scene, then despawns with it.
- **Charge & homing.** Hold-to-charge: scale the spawned fireball's size by how long the
  button was held; steer `t->rotation` toward the nearest `Target_` for a homing bolt.
- **Cast animation.** If the mage is a skeletal model, call
  `PlayAnimationAction("Attack")` in `FireballCaster` and spawn the fireball on the clip's
  `cast` **animation event** (`WasAnimationEvent("cast")`) instead of on the click.
- **Different spells.** Duplicate the particle effect into an ice bolt (blue, **Alpha**
  blend, downward gravity) or a smoke puff (grey, low HDR, big end size, high drag), and a
  second caster script bound to right-click.
- **Damage & score.** Give targets **Health**; on hit, damage them and
  `engine::GameMode::Instance().AddScore(...)` — the same rules layer as the first
  tutorial.

Everything here was authored in the editor and driven by scripts — the particle fireball,
the casting, the impacts, the sound — and it ships through the standalone player like any
other content.
