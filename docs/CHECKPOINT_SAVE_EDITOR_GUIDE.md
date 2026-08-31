# Checkpoint and Save Editor

Open **Panels > World & Gameplay > Checkpoint and Save Editor**. Create a profile,
choose the state that must persist, and save it as a `.3dgsaveprofile` Content asset.

## Checkpoints

Select a marker object in the scene and choose **Add at Selected Object**. Give the
checkpoint a unique name, activation radius, and slot. The marker is only an authoring
anchor; the saved profile stores the position, facing, and rules, so it does not need
to render during play.

Choose **Use in Play**, then enter Play mode. Walking into an enabled checkpoint saves
the selected state. The **Slots & Live Debug** tab shows every slot and can capture,
load, or delete saves while the game is running.

## Script setup

Configure the profile once when the player is created:

```cpp
void PlayerScript::OnCreate() {
    ConfigureSaveProfile("GameAssets/Save/WizardTrial.3dgsaveprofile");
}

void PlayerScript::OnDeath() {
    RespawnFromCheckpoint();
}
```

Lua uses the same workflow:

```lua
function OnCreate()
    Engine.ConfigureSaveProfile("GameAssets/Save/WizardTrial.3dgsaveprofile")
end

function RespawnPlayer()
    Engine.RespawnFromCheckpoint()
end
```

The profile can restore transforms, health, velocity, abilities, inventory, quest
progress, script values, and named entities belonging to currently streamed levels.
Save files retain the source scene/world path so packaged loads return to the correct
runtime world before applying the captured state.
