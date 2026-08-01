# Lua Gameplay Scripting

Lua scripts are first-class gameplay scripts. They use the same Script component,
inspector fields, multiple-script ordering, scene and character assets, Play mode,
and packaged-game path as native C++ scripts.

## Create and attach

1. Select an object in the scene.
2. In **Script**, enter a class/name.
3. Set **Language** to **Lua**.
4. Pick a starter template and select **Create + Attach**.
5. Edit and save the generated file under `Content/Scripts`.
6. Enter Play mode. Lua does not require compiling or restarting the editor.

Saved `.lua` files appear in the Script dropdown, Character Editor script list,
and Content browser. A Lua script is identified by its `.lua` source path; its
class name is a readable attachment name and does not need C++ registration.

## Lifecycle

All callbacks are optional:

```lua
function OnCreate()
    Engine.Log("Created")
end

function OnUpdate(dt)
    -- variable-rate gameplay
end

function OnFixedUpdate(dt)
    -- fixed-rate physics-related gameplay
end

function OnDestroy()
    Engine.Log("Destroyed")
end
```

An error reports the script name and callback, disables only that script instance,
and leaves the rest of the update loop running.

## Inspector fields

Add fields in the normal Script inspector, or use **Detect Fields from Source**.

```lua
local speed = Engine.GetFieldFloat("speed", 4.0)
local lives = Engine.GetFieldInt("lives", 3)
local enabled = Engine.GetFieldBool("enabled", true)
local targetName = Engine.GetFieldString("target", "Player")
local x, y, z = Engine.GetFieldVec3("offset", 0, 0, 0)
local target = Engine.GetFieldEntity("target")
local particle = Engine.GetFieldAsset("effect", "")
```

## Engine API

### Objects and transforms

```lua
local self = Engine.Self()
local player = Engine.FindObject("Player")
local x, y, z = Engine.GetPosition(player) -- omit entity for self
Engine.SetPosition(player, x, y, z)
Engine.Translate(0, 0, 2 * dt)             -- self
local sx, sy, sz = Engine.GetScale()
Engine.SetScale(1, 1, 1)
local spawned = Engine.SpawnEmpty("Marker", x, y, z)
Engine.Destroy(spawned)
Engine.DestroySelf()
local x, y, z = Engine.SocketPosition("StaffTip")
```

`GetPosition`, `GetScale`, and `SocketPosition` return no values when the requested
object/component/socket is unavailable. `FindObject` and `GetFieldEntity` return
`nil` when no object matches.

### Input

```lua
if Engine.IsKeyDown(87) then end
if Engine.WasKeyPressed(32) then end
if Engine.IsMouseButtonDown(0) then end
if Engine.WasMouseButtonPressed(0) then end
local dx, dy = Engine.MouseDelta()
```

Key values use the same GLFW integer key codes as native scripts.

### Animation

```lua
Engine.PlayActionClip("Attack")
Engine.SetAnimationParameter("Speed", 4.0)
Engine.SetAnimationBool("Grounded", true)
Engine.SetAnimationTrigger("Jump")
local busy = Engine.IsAnimationActionPlaying()
```

### Audio, particles, and camera

```lua
Engine.PlayAudio(true)        -- restart
Engine.StopAudio()
Engine.PlayParticles(true)
Engine.BurstParticles(24)
Engine.StopParticles(true)    -- clear
Engine.ShakeCamera(0.8, 0.25, 18.0)
```

These functions operate on components attached to the scripted object.

### Health and game state

```lua
local player = Engine.FindObject("Player")
local hp = Engine.GetHealth(player)
local maxHp = Engine.GetMaxHealth(player)
Engine.Damage(player, 10)
Engine.Heal(player, 5)
Engine.AddScore(100)
local score = Engine.GetScore()
Engine.Win("Level complete")
Engine.Lose("Defeated")
```

### Timers, slow motion, and hit stop

Lua timers call a function in the same script by name:

```lua
local attackTimer = 0

function OnCreate()
    attackTimer = Engine.SetTimerByFunctionName("CastSpell", 0.75, true)
end

function CastSpell()
    Engine.Log("Cast")
end

function StopCasting()
    Engine.ClearTimer(attackTimer)
    -- Or clear every timer that invokes this function:
    Engine.ClearTimerByFunctionName("CastSpell")
end
```

`Engine.IsTimerActive(attackTimer)` checks a handle;
`Engine.IsTimerActive("CastSpell")` checks by function name. Normal timers use
scaled gameplay time, so they slow down and pause with global dilation.

```lua
Engine.SetGlobalTimeDilation(0.25) -- quarter-speed slow motion
local baseScale = Engine.GetGlobalTimeDilation()
local currentScale = Engine.GetEffectiveTimeDilation()

Engine.HitStop(0.08)        -- freeze gameplay for 80 ms of real time
Engine.HitStop(0.15, 0.05)  -- near-freeze instead of a complete stop
local frozen = Engine.IsHitStopActive()

Engine.SetGlobalTimeDilation(1.0) -- restore normal speed
```

Hit-stop duration is measured in unscaled real time. This is important when its
dilation is zero: the freeze can still count down and release. Dilation affects
scripts, timers, physics, AI, animation, particles, camera blends, and
cinematics; editor UI and the audio mixer continue in real time.

## Security and packaging

Lua 5.4 runs embedded in the engine. The exposed standard libraries are base,
coroutine, table, string, math, and UTF-8. File access, operating-system commands,
dynamic package loading, and the debug library are not exposed.

Keep Lua files inside `Content/Scripts`. The existing content cooking/export path
copies them with the project, and the runtime resolves the same serialized source
path used by Editor Play.
