# Interactive Door and Lift Tool

Open **Panels > Level Design > Interactive Door and Lift Tool**. The tool creates
reusable `.3dginteraction` assets for doors, gates, lifts, and moving platforms.

## Basic setup

1. Place or import the mesh that should move, and give it an appropriate collider.
2. Open the tool and choose a preset: Hinged Door, Sliding Door, Gate, Elevator, or
   Moving Platform.
3. Set the local pivot and hinge axis for a hinged door, or set Local Travel for all
   translating objects. Travel is relative to the object's closed orientation.
4. Adjust open/close duration, easing, hold time, auto-close, looping, and start pose.
5. Scrub **Preview Position** or press **Play Preview** to verify the motion.
6. Optionally select open, close, and locked sounds plus open/close action clips.
7. Enable **Locked** and enter a Required Access Tag when a key or permission is needed.
8. Save, select the scene object, then press **Apply to Selected**.

The selected object becomes kinematic, so a lift can carry physics objects and a door
can block or push them. Save the scene normally. The interaction asset is recorded as
a stable dependency and is included by project cooking and packaging.

## Native C++ script calls

Inside a class derived from `engine::Script`:

```cpp
const auto door = FindObject("VaultDoor");
OpenInteraction(door, "BlueKey");
ToggleInteraction(door);
SetInteractionLocked(false, door);
const std::string state = InteractionState(door);
```

Omitting the entity operates on the object that owns the script.

## Lua script calls

```lua
function OnCreate()
    Engine.SetInteractionLocked("VaultDoor", true)
end

function OnUpdate(dt)
    if Engine.KeyPressed(69) then -- E
        Engine.ToggleInteraction("VaultDoor", "BlueKey")
    end
end
```

Other Lua functions are `Engine.OpenInteraction`, `Engine.CloseInteraction`, and
`Engine.InteractionState`. Pass `nil` instead of a name to operate on the script owner.

## Common corrections

- A hinged door orbiting around its center needs its Local Pivot moved to the hinge edge.
- A door moving in the wrong direction needs the travel vector or open angle sign reversed.
- A locked door only accepts the exact Required Access Tag, including capitalization.
- Moving platforms automatically use looping ping-pong motion; elevators normally use
  explicit calls or auto-close after their Open Hold time.
