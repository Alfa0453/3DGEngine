# Particle System

## Purpose

The particle system authors and runs reusable effects such as fire, smoke,
sparks, dust, projectiles, trails, impacts, and magic.

## Runtime architecture

The engine has two simulation paths:

- **CPU emitter:** portable and suitable for modest particle counts.
- **GPU emitter:** uses compute support when available for larger effects.

The `Auto` backend selects GPU simulation when the renderer and driver support
it, otherwise it falls back to CPU simulation. An effect should remain valid on
both paths unless it deliberately depends on a GPU-only feature.

## Asset and instance

A `.particle` asset stores authored emitter data. A scene particle component
references that asset. At runtime, `RuntimeParticleSystem` owns live emitter
instances and updates them independently from the editor preview.

An effect may contain more than one emitter—for example:

- a bright fireball core;
- an additive trail;
- sparks;
- an impact burst.

Keep asset authoring separate from the instance transform and playback state.

## Module pipeline

Modules execute in a fixed pipeline:

### Spawn stage

Defines how particles begin:

- spawn rate, burst count, and burst interval;
- maximum particle count;
- lifetime range;
- spawn shape and dimensions;
- initial position;
- initial velocity, direction, and spread.

### Update stage

Changes living particles:

- acceleration and other forces;
- rotation;
- color over life;
- size over life;
- collision response;
- drag and other movement behavior.

### Render stage

Controls presentation:

- sprite or billboard rendering;
- texture and blend behavior;
- trails or ribbons;
- renderer-facing size and color behavior.

Required modules cannot be removed. Module order is normalized and validated
before the effect is compiled into a CPU or GPU emitter pipeline.

## Playback

System playback includes:

- enabled and autoplay state;
- loop behavior;
- prewarm;
- duration and start delay;
- simulation speed;
- local-space or world-space simulation.

Use local space for an effect that must remain attached to a moving object. Use
world space when spawned particles should remain where they were emitted.

## Editor workflow

The Particle Editor contains:

1. an isolated preview viewport;
2. presets such as fire, smoke, sparks, magic, and dust;
3. the ordered module stack;
4. per-module settings;
5. playback and simulation controls;
6. system/effect save controls.

Select a module row to expose its settings. The stage headings organize
execution; the editable values belong to the selected module and system
sections. Previewing in the editor does not automatically place the effect in
the scene.

To use an effect in a level:

1. save the `.particle` asset under `Content`;
2. add a particle system component to an empty object or effect prototype;
3. select the asset;
4. enable it or start it through a script;
5. confirm the object and effect are inside the active camera view.

## Script usage

Gameplay scripts can:

- spawn an effect asset at a transform;
- attach an effect to an entity or socket;
- start, stop, pause, or restart an instance;
- set runtime parameters;
- destroy transient effects after completion.

For projectiles, spawn the projectile object and its trail separately from the
impact effect. Apply damage only after the projectile collision callback, then
spawn the impact effect at the confirmed contact position.

## Sockets and local direction

When spawning from a character:

1. resolve the named socket from the animated model;
2. obtain its world transform after animation has updated;
3. derive forward direction from that transform;
4. offset the initial position slightly beyond the weapon;
5. ignore collision with the owner for the initial projectile.

This prevents effects from appearing at the character origin or traveling
backward because a model-space axis was treated as world-space forward.

## Diagnostics

If an effect does not appear:

- verify the saved asset path resolves through project content;
- verify a runtime component or explicit spawn call exists;
- check enabled, autoplay, delay, duration, and loop;
- inspect lifetime, size, alpha, texture, and maximum particle count;
- confirm the camera and spawn transform;
- confirm required modules passed validation;
- switch from Auto to CPU temporarily to isolate GPU capability problems;
- inspect the console for asset or shader loading failures.

## Important source files

- `engine/include/engine/graphics/ParticleSystem.h`
- `engine/include/engine/graphics/GpuParticleSystem.h`
- `engine/include/engine/graphics/ParticleRenderer.h`
- `engine/include/engine/graphics/RuntimeParticleSystem.h`
- `engine/include/engine/assets/ParticleAsset.h`
- `editor/include/ParticleEditorPanel.h`
- `editor/include/ParticlePresets.h`
- `editor/include/ParticleAsset.h`

