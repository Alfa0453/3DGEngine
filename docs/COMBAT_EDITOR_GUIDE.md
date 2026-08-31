# Combat Editor Guide

The Combat Editor creates reusable `.3dgcombat` profiles for players, enemies,
weapons, and other damageable actors. Open it from **Panels > World & Gameplay >
Combat Editor**, or double-click a combat profile in Assets.

## Create a combat profile

1. Select **New**, enter a profile name, and choose its team. Team `0` is neutral;
   actors on the same non-zero team do not damage each other unless **Friendly
   Fire** is enabled.
2. In **Defense & Targeting**, set poise, stagger recovery, block reduction, parry
   timing, post-hit immunity, targeting range, and automatic facing.
3. Add named damage types. Their damage and stagger multipliers are applied by the
   attacker. Armor penetration is stored for projects that add an armor statistic.
4. Add resistances. A multiplier below `1` reduces that type of damage; above `1`
   makes the actor vulnerable.
5. Add combo steps in play order. Assign an action clip, damage type, damage,
   duration, hit time, input window, range, radius, reaction, particle, and audio.
6. Save the profile, select a scene actor, and choose **Apply to Selected**. The
   assignment persists in editor scenes and packaged games.

The combo timeline shows the hit frame and the interval in which the next input is
accepted. The Live Debug tab can simulate hits, guarding, parries, stagger, health,
and emitted combat events without entering play mode.

## C++ script example

```cpp
void PlayerCombat::OnCreate() {
    ConfigureCombat("GameAssets/Combat/Player.3dgcombat");
}

void PlayerCombat::OnUpdate(float) {
    SetCombatBlocking(Input().KeyHeld(Key::MouseRight));
    if (Input().KeyPressed(Key::MouseLeft)) {
        if (CombatStep() < 0) StartCombat(currentTarget);
        else AdvanceCombat();
    }
}

void PlayerCombat::OnEvent(const ScriptEvent& event) {
    if (event.name == "CombatAttackWindow")
        CombatHit(currentTarget);
}
```

`CombatHit` uses the active combo step. `DealCombatDamage(target, amount, type)` is
available for projectiles, hazards, and scripted attacks. Both return `Hit`,
`Blocked`, `Parried`, `Immune`, `Friendly`, or `Miss`.

## Lua script example

```lua
function OnCreate()
    ConfigureCombat("GameAssets/Combat/Wizard.3dgcombat")
end

function BeginAttack(target)
    if CombatStep() < 0 then
        return StartCombat(target)
    end
    return AdvanceCombat()
end

function AttackWindow(target)
    return CombatHit(target)
end

function SetGuard(active)
    SetCombatBlocking(active)
end
```

Combat profiles are engine-owned assets. Their action clips, particles, and audio
are recorded as dependencies so cooking and asset-reference tools keep the entire
attack setup together.
