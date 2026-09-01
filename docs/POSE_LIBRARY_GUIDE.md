# Pose Library

The Pose Library stores reusable, skeleton-aware poses in engine-owned `.3dgpose`
assets. Open it from **Panels > Animation & Characters > Pose Library**, or
double-click a pose library in Content.

## Create a library

1. Click **New** and name the library.
2. Choose the character's saved Skeleton.
3. Choose **Bind pose** or an imported Animation as the capture source.
4. For an animation, move **Capture Time** to the desired frame.
5. Click **Capture Current**, name the pose, and add comma-separated tags.
6. Select bones to fine-tune position, rotation, or scale.
7. Use **Preview Blend** to compare the pose against its capture source.
8. Click **Mirror** to create a left/right counterpart, then **Save**.

Pose bones are matched by name, not array index. Bones absent from a saved pose use
the target skeleton's bind transform. This makes partial poses safe and makes poses
more resilient to reordered skeleton data.

## Runtime scripting

Native C++ scripts can apply and remove a pose override:

```cpp
Anim().ApplyPose("Content/GameAssets/PoseLibraries/Wizard.3dgpose", "Cast", 0.8f);
Anim().ClearPose();
```

Lua scripts expose the equivalent functions:

```lua
ApplyAnimationPose("Content/GameAssets/PoseLibraries/Wizard.3dgpose", "Cast", 0.8)
ClearAnimationPose()
```

The override blends after locomotion and before one-shot action layers. Therefore an
attack action can still replace or mask parts of an applied pose.

## Mirroring conventions

Mirroring recognizes common pairs such as `Left/Right`, `_L/_R`, `.L/.R`, and
lowercase equivalents. Use one of those conventions for predictable results.
