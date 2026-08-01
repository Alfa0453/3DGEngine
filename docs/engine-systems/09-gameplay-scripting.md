# Gameplay framework, player controller, and scripting

## Gameplay components

`Health` stores current/max HP, alive state, and a one-frame `justDied` pulse.
Damage reduces HP; `UpdateHealth` owns the authoritative alive/death transition.

`Projectile` stores direction, speed, range, traveled distance, damage, sweep
radius, and owner. `UpdateProjectiles` sweeps every projectile, ignores its
owner, damages only the first living Health target, destroys on any solid hit,
and returns `ProjectileHit` records for VFX/audio/scoring.

`Attachment` drives an entity from a parent transform or parent animated bone.
Character-render attachments use the richer socket system; this ECS component
is useful for runtime-spawned attached entities.

`Ragdoll` is covered in the physics chapter.

## Game mode

`GameMode` is the process-global game-rules layer. It tracks:

- Playing, Paused, GameOver, or Victory;
- score;
- elapsed play time;
- display message;
- optional lose-on-player-death rule.

The runtime calls `Update(registry, player, dt)` and freezes gameplay when the
state is not Playing. Scripts can add score, pause/resume, win, lose, reset, or
set a message.

Game Mode Settings select the player object, input enable, pause/restart
permissions, initial score, lose-on-death behavior, and optional camera-mode
override.

## Player controller

`PlayerController` combines:

- kinematic capsule movement;
- walk, run, jump, gravity, slopes, and steps;
- injected `PlayerInput`;
- facing-camera or facing-movement rotation;
- smooth turn speed;
- first-person, third-person, isometric, and platformer camera modes;
- spring-arm collision and return smoothing;
- shoulder camera switching;
- target lock-on;
- stair visual/camera smoothing.

Camera mode is configured before gameplay. `PlayerInput::toggleView` remains
deprecated and should not be used to change the selected game mode during Play.

The controller is input-source independent, making keyboard, gamepad, replay,
or AI-generated input possible.

## Camera direction

`CameraDirector` is the script-facing bridge to host-owned camera sequences.
Scripts queue play/stop/skip commands. The editor or player resolves named
sequence assets, runs `CameraSequencePlayer`, and sends completion, skip, and
timeline events back to the director.

Scripts can query sequence state and event completion without directly owning
camera resources.

## Script lifecycle

Gameplay scripts subclass `engine::Script`:

```cpp
class Coin : public engine::Script {
public:
    void OnCreate() override {}
    void OnUpdate(float dt) override {}
    void OnFixedUpdate(float dt) override {}
    void OnDestroy() override {}
};
```

`UpdateScripts` creates enabled instances, calls `OnCreate` once, then
`OnUpdate`. `FixedUpdateScripts` calls `OnFixedUpdate` on created instances.
`ShutdownScripts` calls `OnDestroy` and releases them.

Objects can have a primary script and multiple additional script slots. Each
slot has an enabled flag, class, source path, fields, and an independent
instance.

## Script context

`ScriptContext` supplies:

- registry and self entity;
- deferred destruction queue;
- input and collision/animation events;
- audio system;
- camera shake and director;
- game mode;
- authored fields;
- deferred scene-load request.

Scripts should use the context helpers rather than retaining pointers across
scene reloads.

## Core Script API

Entity and world:

- `Self`, `Registry`, `Transform`;
- `FindObject`, `FindTransform`;
- `TryGet<T>`, `Has<T>`, `Remove<T>`;
- `SpawnEmpty`, `SpawnFromObject`;
- `DestroySelf`, `Destroy`;
- `RequestSceneLoad`.

Sockets:

- `SocketTransform`;
- `SocketPosition`.

Persistence:

- `SaveValue`, `LoadValue`;
- `SaveCheckpoint`, `LoadCheckpoint`.

Timing:

- `SetTimer`, `SetTimerByEvent`, `Delay`, `ClearTimer`;
- `BindTimerFunction`, `SetTimerByFunctionName`;
- `ClearTimerByFunctionName`, `IsTimerActive`;
- `Sequence().Do().Wait().WaitUntil()`.

Global gameplay time:

- `SetGlobalTimeDilation` and `GlobalTimeDilation`;
- `EffectiveTimeDilation`;
- `HitStop` and `IsHitStopActive`.

Normal timers consume scaled gameplay time. Hit-stop duration consumes unscaled
real time so a zero-dilation freeze always releases.

Input and events:

- key/mouse down and pressed;
- mouse delta;
- trigger touching/entered/exited;
- animation-event queries.

Fields:

- String, Float, Int, Bool, Vec3, Color, Entity, and Asset lookup.

## Grouped APIs

For discoverability, scripts can use:

- `Input()` for keys, mouse buttons, and deltas;
- `Camera()` for shake and cinematic sequences;
- `Particles()` for play/stop/restart/burst/clear/rate/speed/status;
- `Audio()` for source, cue, mixer, and adaptive-music controls;
- `Anim()` for actions, profiles, parameters, triggers, and movement lock.

The older flat helper methods remain available.

## Spawning a projectile from a socket

Typical animation-event flow:

```cpp
void OnUpdate(float) override {
    if (Input().MousePressed(0) && !Anim().IsActionPlaying()) {
        Anim().PlayActionClip("StaffAttack");
    }

    if (WasAnimationEvent("SpawnFireball")) {
        glm::mat4 socket;
        if (SocketTransform("StaffTip", &socket)) {
            const glm::vec3 position = glm::vec3(socket[3]);
            const glm::vec3 forward =
                glm::normalize(glm::vec3(socket[2]));
            const auto fireball =
                SpawnFromObject("FireballPrototype", position);
            if (auto* projectile = TryGet<engine::Projectile>(fireball)) {
                projectile->dir = forward;
                projectile->owner = Self();
            }
        }
    }
}
```

The exact positive/negative socket axis depends on the authored socket frame;
verify it visually in Character Editor.

## Script fields

`ScriptField` adds editable Inspector data with a type, value, optional range,
tooltip, and group. Asset fields should reference Content assets rather than
hard-coded external paths. Entity fields resolve by runtime object name.

## Creating and compiling scripts

The editor workflow:

1. Create or select `Content/Scripts`.
2. Choose a template and class name.
3. Create the script from Inspector, Character Editor, or Behavior Graph.
4. Attach it from the saved-script dropdown.
5. Edit with the built-in editor, VS Code, Visual Studio, Rider, or a custom
   editor.
6. Compile scripts and restart.

The shared `game` module generates/updates registrations so the same class is
available to Editor Play and the standalone player.

## Hot-reload module

`ScriptModule` loads a shared library exporting:

```cpp
extern "C" void RegisterScriptModule(engine::ScriptRegistry&);
```

Before unload:

1. `ShutdownScripts`.
2. Clear `ScriptRegistry`.
3. Unload the module.
4. Rebuild.
5. Load and register again.

Violating this sequence leaves script virtual tables pointing into unloaded
code.

## Error behavior

Script exceptions are caught, the failing slot is disabled, and the optional
script error handler reports the problem to the editor console. Unknown class
names set `missingFactory`; they do not silently instantiate a different type.

Destroy and scene-load requests are deferred until after script iteration to
avoid invalidating the registry from inside a callback.
