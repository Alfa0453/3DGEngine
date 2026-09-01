# Animation Timeline Editor

The Animation Timeline Editor creates engine-owned `.3dgclip` assets from an
imported skeletal mesh or animation asset. Open it from **Panels > Animation >
Animation Timeline Editor**, or double-click a `.3dgclip` in Content.

## Author a clip

1. Choose a **Source File**, then select the take in **Clip**.
2. For animation-only `.3dganim` files, choose a compatible **Preview Mesh**.
3. Drag the red playhead to scrub. Set **Start** and **End** to keep only the
   useful source range; the trimmed range becomes time zero at runtime.
4. Set looping and playback speed. Enable **Action Clip** for attacks, casts,
   reactions, or other script-triggered one-shots.
5. Add event markers at frames that should trigger gameplay, particles, audio,
   damage windows, or script callbacks.
6. Add float curves for continuous values such as cast strength, foot IK weight,
   facial poses, material glow, or effect intensity.
7. Enable **Additive Clip** for action deltas that should layer over the base pose,
   and select the reference time used to calculate that delta. Enabling it also
   makes the asset a one-shot Action Clip.
8. Inspect root displacement before deciding whether to enable **Strip Root
   Motion**, then save the clip into Content.

The animation graph and character editor resolve the saved range, additive data,
curves, and playback speed automatically. Scene files and packaged games retain
the same metadata.

## Events and curves in scripts

Native C++ scripts read authored notifies and the currently playing curve:

```cpp
if (WasAnimationEvent("CastFireball")) {
    // Spawn the projectile from the staff socket.
}

const float strength = Anim().GetCurve("CastStrength", 0.0f);
```

Lua exposes the same data:

```lua
if Engine.WasAnimationEvent("CastFireball") then
    -- Spawn the projectile.
end

local strength = Engine.AnimationCurve("CastStrength", 0.0)
```

Curve time follows the active action layer when an action is playing; otherwise
it follows the current animation-graph clip. Missing curves return the supplied
fallback value.

## Runtime behavior

- Trimmed ranges loop and clamp inside their authored start/end bounds.
- Additive clips use the reference pose and blend as deltas instead of overrides.
- Event times are converted to playback-relative time when standalone actions are
  attached to a character.
- Existing version 1-4 `.3dgclip` assets remain loadable and upgrade on save.
