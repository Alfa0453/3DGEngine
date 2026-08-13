# Ability Editor

Open **Panels > Animation & Characters > Ability Editor**. Ability assets are
saved as engine-owned `.3dgability` files under `Content/GameAssets/Abilities`.

## Author an ability

1. Set the name, cooldown, charges, charge recovery, resource costs, activation
   range, and whether a valid target is required.
2. Build the timing from ordered phases. A common spell uses **Wind Up**, **Active**,
   and **Recovery**. Disable interruption on phases that must finish.
3. Add effects to a phase and set their time within that phase. The timeline shows
   phases as coloured blocks and effects as yellow markers.
4. Choose an effect type:
   - **Damage / Heal** changes a Health component.
   - **Impulse** pushes a dynamic rigid body.
   - **Animation Action** starts the named animation action.
   - **Projectile** creates a collision-tested engine projectile.
   - **Particle / Audio** emits a runtime ability event with the selected asset.
   - **Script Event** emits a named event for custom gameplay logic.
5. Choose Self, explicit Target, or Radius targeting. Set radius, magnitude, speed,
   range, direction, and dependency assets where applicable.
6. Scrub or play the timeline, resolve validation warnings, and save.

## Native C++ script example

```cpp
void PlayerMagic::OnCreate() {
    SetAbilityResources(100.0f, 100.0f);
    GrantAbility(GetFieldAsset("FireballAbility"));
}

void PlayerMagic::OnUpdate(float) {
    if (WasMouseButtonPressed(0))
        ActivateAbility("Fireball", FindObject("WizardEnemy"));

    if (WasAbilityEvent("CastFlash")) {
        // Optional custom response to a Particle, Audio, or Script Event effect.
    }
}
```

## Lua example

```lua
function OnCreate()
    Engine.SetAbilityResources(100, 100)
    Engine.GrantAbility(Engine.GetFieldAsset("FireballAbility"))
end

function OnUpdate(dt)
    if Engine.WasMouseButtonPressed(0) then
        Engine.ActivateAbility("Fireball", Engine.FindObject("WizardEnemy"))
    end

    if Engine.WasAbilityEvent("CastFlash") then
        -- custom VFX, audio, HUD, or camera response
    end
end
```

`ActivateAbility` returns false while another ability is active, while its cooldown
is running, when charges or resources are insufficient, or when a required target
is missing/out of range. This prevents attack or spell spamming without extra script
timers. Save slots preserve mana, stamina, charges, recharge progress, and cooldowns.
