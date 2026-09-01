# IK Rig Editor

The IK Rig Editor creates reusable `.3dgikrig` assets for foot placement, hand
targets, look-at, aiming, and weapon alignment. One rig can be assigned to every
character that uses the same skeleton.

## Create a rig

1. Open **Panels > Animation > IK Rig Editor**.
2. Choose a skeletal mesh in **Skeleton**. The list searches engine-owned
   `.3dgskmesh` assets under the current project's Content folder.
3. Enter a rig name and choose **New** if the panel contains an older rig.
4. Use **Auto Setup Humanoid** to create common foot, hand, and look-at goals from
   recognizable bone names. Review every selected bone before saving.
5. Save the asset in Content. The recommended folder is
   `Content/GameAssets/Animation/IKRigs`.

The skeleton reference, rig goals, and foot-placement settings are stored by stable
asset identity. Moving a registered rig or skeleton within Content does not break a
character reference.

## Goal types

- **Two Bone** solves a root, middle, and end chain. Use it for arms and legs. The
  target positions the end bone and the pole controls elbow or knee direction.
- **Look At** rotates one bone so its authored forward axis faces a target. Use it
  for the head or upper chest.
- **Aim** is a constrained directional goal for aiming a body or weapon bone.
- **Weapon Alignment** aligns a weapon or hand bone to a script-driven target.

Each goal has an authoring weight, maximum angle, target offset, pole offset, forward
axis, and interpolation speed. Interpolation prevents targets from snapping when a
camera, enemy, or held prop changes direction.

## Foot placement

Enable foot placement in the rig, assign pelvis and both upper-leg, lower-leg, and
foot chains, then set trace distance, foot clearance, pelvis weight, maximum pelvis
drop, and overall weight. Runtime terrain traces run after the animation graph and
action layers. The older Character Editor foot-IK fields remain only as a fallback
for characters without an authored IK rig.

## Assign to a character

1. Open the character asset in the Character Editor.
2. Open **Animation**.
3. Search for the saved asset in **Authored IK Rig**.
4. Save the character, then use **Apply to Selected** or add the character to the
   level.

Alternatively, select a skeletal character in the level and choose **Apply to
Selected** in the IK Rig Editor. Scene and packaged-runtime serialization retain the
rig reference automatically.

## Drive goals from C++

```cpp
ConfigureIKRig("Content/GameAssets/Animation/IKRigs/Wizard.3dgikrig");
SetIKTarget("RightHand", staffTargetWorld, 1.0f);
SetIKTarget("LookAt", enemyHeadWorld, 0.75f);
SetIKWeight("WeaponAim", isAiming ? 1.0f : 0.0f);

// Return to the animation pose when the target is no longer needed.
ClearIKTarget("RightHand");
```

These methods are available on native `Script` instances and operate on the script's
own object.

## Drive goals from Lua

```lua
Engine.ConfigureIKRig("Content/GameAssets/Animation/IKRigs/Wizard.3dgikrig")
Engine.SetIKTarget("RightHand", targetX, targetY, targetZ, 1.0)
Engine.SetIKWeight("LookAt", 0.6)

if Engine.HasIKGoal("RightHand") then
    -- The current character has the requested goal.
end

Engine.ClearIKTarget("RightHand")
```

Pass an object name as the final optional argument when a Lua script must control a
different object.

## Troubleshooting

- A goal that reports a missing bone was authored against a different skeleton or
  uses a misspelled bone. Reopen the rig and choose bones from the dropdowns.
- A bent elbow or knee pointing the wrong way needs a different pole offset.
- A target that snaps needs a larger interpolation speed transition time or a lower
  script weight during activation and release.
- Foot placement requires collision below the feet and a configured runtime ground
  query. Verify physics debug traces and collision channels in Play mode.
- IK runs after the animation graph and standalone action layers, so animation remains
  the source pose while IK only corrects the final result.
