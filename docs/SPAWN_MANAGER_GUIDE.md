# Spawn Manager Guide

The Spawn Manager authors reusable `.3dgspawn` encounters. Open it from **Panels
> World & Gameplay > Spawn Manager**, or double-click a spawn encounter in Assets.

## Set up an encounter

1. Keep one authored scene object for every spawnable prototype. Give each a
   unique hierarchy name such as `WizardPrototype`; it can be hidden outside the
   arena if it should not appear as a normal participant.
2. Create an empty encounter-controller object at the center of the intended
   spawn area.
3. In **Volume & Rules**, select point, box, or sphere placement. Configure auto
   start or player-entry activation, concurrent and total limits, looping,
   recycling, yaw randomization, cooldown, and deterministic seed.
4. In **Weighted Groups**, add entries. **Scene Prototype Name** must exactly match
   the authored object. Weight controls relative probability. Difficulty ranges
   and per-entry alive limits filter the available choices. An optional prefab
   dependency ensures related content is included when cooking.
5. In **Waves**, choose a weighted group, spawn count, start delay, interval,
   end delay, and whether the next wave waits until all managed actors are dead.
6. Save, select the encounter-controller object, and choose **Apply to Selected**.

The preview tab visualizes the volume, eligible weighted entries, planned spawn
count, and difficulty filtering. At runtime the manager emits encounter-start,
wave-start, spawn, failure, wave-clear, completion, and stop events.

## Native C++ scripting

```cpp
void ArenaController::OnCreate() {
    ConfigureSpawnManager("GameAssets/Spawns/WizardAmbush.3dgspawn");
}

void ArenaController::BeginHardMode() {
    ResetSpawn();
    StartSpawn(4.0f); // difficulty selects entries whose range includes 4
}

void ArenaController::SkipToBossWave() {
    TriggerSpawnWave(3);
}
```

Managers on other entities can be controlled by passing their entity handle to
`StartSpawn`, `StopSpawn`, `ResetSpawn`, `TriggerSpawnWave`,
`SetSpawnDifficulty`, `SpawnAlive`, and `IsSpawnRunning`.

## Lua scripting

```lua
function OnCreate()
    ConfigureSpawnManager("GameAssets/Spawns/WizardAmbush.3dgspawn")
end

function BeginEncounter(difficulty)
    ResetSpawn()
    return StartSpawn(difficulty)
end

function StopEncounter()
    StopSpawn()
end
```

With **Recycle Dead** enabled, dead actors managed by the encounter are reset and
reused by later waves. This avoids repeated entity allocation while preserving
their authored prototype components.
