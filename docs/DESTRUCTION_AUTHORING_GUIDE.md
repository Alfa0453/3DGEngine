# Destruction Authoring Tool

Open **Panels > Level Design > Destruction Authoring Tool**.

## Author a destructible object

1. Select the intact mesh in the level and choose **Capture Selected Mesh**.
2. Set the object bounds and the X/Y/Z chunk counts. The preview is deterministic;
   changing the seed produces another repeatable break pattern.
3. Add optional damage states. Each state can replace the mesh/material and play a
   particle and sound when its health threshold is crossed.
4. Set health, minimum damage, impact threshold, debris mass, impulses, collision,
   and lifetime.
5. Assign the final break particle, sound, and optional reusable debris mesh.
6. Use **Preview Break in Level** to create editable dynamic preview chunks. Use
   **Clear Preview** before continuing level work.
7. Save. The `.3dgdestruction` asset appears in Content and reopens on double-click.

The level preview is deliberately separate from runtime authoring and never changes
the intact source object. Runtime debris is resolved through the normal engine asset,
physics, material, particle, and audio systems.

## Native C++ script

```cpp
void BreakableCrate::OnStart() {
    ConfigureDestructible("Content/GameAssets/Destruction/Crate.3dgdestruction");
}

void BreakableCrate::OnHit(float damage, const glm::vec3& point,
                           const glm::vec3& impulse) {
    DamageDestructible(damage, point, impulse);
    if (WasDestructionEvent("Broken")) {
        // Award score, open a route, or notify a quest.
    }
}
```

Use `ImpactDestructible(impact, point, direction)` for physics-derived hits.
`DestructibleHealth()` and `IsDestructibleBroken()` provide state queries.

## Lua script

```lua
function OnStart()
    Engine.ConfigureDestructible(
        "Content/GameAssets/Destruction/Crate.3dgdestruction")
end

function DamageCrate(amount, px, py, pz, ix, iy, iz)
    Engine.DamageDestructible(amount, px, py, pz, ix, iy, iz)
    if Engine.WasDestructionEvent("Broken") then
        Engine.Log("Crate destroyed")
    end
end
```

Particles and sounds assigned to damage states or the final break are spawned
automatically. Debris receives dynamic box collision, outward and angular impulses,
and is removed after its configured lifetime.
