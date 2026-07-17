# Camera Manager

Status: **Complete**

The Camera Manager now covers the full editor and gameplay camera workflow:

- saved scene cameras with lens settings, primary-camera selection, piloting,
  duplication, renaming, and Play-mode selection;
- smooth pose and lens blending with selectable easing;
- collision-aware third-person spring arms;
- shoulder switching and lock-on targeting;
- priority-based Camera Zones with enter/exit restoration;
- layered camera shake with editor preview and script controls;
- cinematic sequences with ordered shots, holds, looping, linear rails, and
  Catmull-Rom spline rails;
- viewport rail, camera-node, and look-at visualization;
- a cinematic timeline with pause/resume, scrubbing, duration display, and
  colored event/audio/animation cue markers;
- trigger-controlled and script-controlled cinematic playback, input locking,
  skipping, completion events, and named timeline events;
- scene persistence and reference-safe camera/sequence renaming.

## Script API

```cpp
PlayCameraSequence("Opening Cinematic", true, true);
StopCameraSequence();
SkipCameraSequence();

if (WasCameraSequenceEvent("Opening Cinematic", "RevealBoss")) {
    // Start dialogue, particles, AI, or other gameplay.
}

if (WasCameraSequenceFinished("Opening Cinematic")) {
    // Continue gameplay after completion or skipping.
}

ShakeCamera(1.0f, 0.35f, 18.0f);
```

The second and third `PlayCameraSequence` arguments control player-input locking
and whether Enter may skip the cinematic.

## Timeline Tracks

- **Event:** emits a named event to scripts at its authored time.
- **Audio:** plays the selected audio asset at its authored time and volume.
- **Animation:** plays a full-body action clip on the named animated object in
  Play mode.

Camera Manager regression coverage is part of `camera_manager_regression`.
