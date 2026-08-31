# Interaction Editor

Open **Panels > Level Design > Interaction Editor**. The editor creates
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
7. Configure **Prompt and Input**. Press interactions complete immediately; Hold
   interactions accumulate the supplied held time before they fire.
8. Under **Availability Conditions**, optionally require an access tag, one or more
   comma-separated gameplay tags, facing, and line of sight.
9. Under **Animation Requirements**, choose the interactor action clip. An interaction
   can wait for a named animation event before the door/lift motion begins.
10. Name the started, completed, and failed events for scripts and gameplay systems.
11. Use **Availability Preview** to test distance, angle, sight, access, and condition
    tags without entering Play mode.
12. Save, select the scene object, then press **Apply to Selected**.

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

// Full authored contract: player position/facing are read from the script owner.
if (CanInteract(door, "BlueKey", "PowerOn,QuestReady")) {
    RequestInteraction(door, deltaSeconds, "BlueKey", "PowerOn,QuestReady");
}

// Use this from the action clip event named by Commit Event.
SignalInteractionEvent(door, "InteractionCommit");
if (WasInteractionEvent("InteractionCompleted", door)) {
    // Continue the quest or enable the next gameplay step.
}
```

Omitting the entity operates on the object that owns the script.

## Lua script calls

```lua
function OnCreate()
    Engine.SetInteractionLocked("VaultDoor", true)
end

function OnUpdate(dt)
    local tags = "PowerOn,QuestReady"
    if Engine.CanInteract("VaultDoor", "BlueKey", tags, true) and Engine.KeyDown(69) then
        Engine.RequestInteraction("VaultDoor", dt, "BlueKey", tags, true)
    end
end
```

The UI can display `Engine.InteractionPrompt(...)`. Animation events call
`Engine.SignalInteractionEvent("VaultDoor", "InteractionCommit")`. Legacy direct
motion functions remain available: `Engine.OpenInteraction`, `Engine.CloseInteraction`,
`Engine.ToggleInteraction`, and `Engine.InteractionState`.

Call `Engine.CancelInteractionInput("VaultDoor")` when a held input is released.
`Engine.WasInteractionEvent("InteractionCompleted", "VaultDoor")` consumes a named
runtime event once, which is useful for quests, audio, UI, and scripted consequences.

## Runtime behavior

`CanInteract` and `InteractionPrompt` evaluate the exact saved rules. A failed request
records a reason and emits the configured failed event. A successful request emits the
started event and action clip, accumulates hold time when required, then either begins
motion or waits at the animation-event gate. Reaching the open pose emits the completed
event. Existing version-1 interaction assets load with safe version-2 defaults.

## Common corrections

- A hinged door orbiting around its center needs its Local Pivot moved to the hinge edge.
- A door moving in the wrong direction needs the travel vector or open angle sign reversed.
- A locked door only accepts the exact Required Access Tag, including capitalization.
- Moving platforms automatically use looping ping-pong motion; elevators normally use
  explicit calls or auto-close after their Open Hold time.
- A Hold interaction must receive held seconds every frame; releasing input should call
  `CancelInteractionInput` so partial progress does not carry into the next attempt.
- If motion never begins, verify the action clip emits the exact **Commit Event** name.
