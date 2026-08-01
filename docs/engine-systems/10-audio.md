# Audio System

## Purpose

The audio system provides playback, spatial sound, mixing, reusable sound cues,
adaptive music, runtime event hooks, and editor-side waveform and mix tools.

## Main runtime types

| Type | Responsibility |
|---|---|
| `AudioEngine` | Owns the playback backend, active voices, buses, effects, snapshots, cues, and music state |
| `AudioAsset` | Describes an engine-owned audio resource and its import/playback metadata |
| `RuntimeAudioSystem` | Connects scene components and gameplay events to `AudioEngine` |
| `AudioEditing` | Decodes audio into editable sample buffers and writes supported output |
| `AudioSourceComponent` | Stores the authored settings for a sound-emitting scene object |

The playback backend is initialized defensively. A missing output device must not
prevent the rest of the engine or editor from running.

## Playback models

The engine supports two useful playback lifetimes:

- **Fire-and-forget:** short effects such as impacts, UI clicks, footsteps, and
  one-shot spell sounds.
- **Persistent source:** a source represented by a handle that can be moved,
  stopped, paused, faded, or adjusted while it is playing.

For persistent world sounds, update the source transform every frame so spatial
audio follows the object.

## Spatial audio

Spatial sources can use:

- world position and velocity;
- minimum and maximum attenuation distance;
- rolloff;
- listener-relative direction;
- inner and outer cone angles;
- doppler response;
- obstruction or occlusion;
- per-source volume, pitch, priority, looping, and spatial toggles.

The runtime system obtains the listener from the active gameplay camera. Scene
audio source positions are taken from their owning entities.

## Mixing

The standard buses are:

- Master
- Music
- SFX
- Dialogue
- UI
- Ambient

Each source or cue is routed to a bus. Bus controls make it possible to change
volume or mute an entire category without editing individual sounds.

Supported mix processing includes low-pass and high-pass filtering, reverb,
and compression. Mix snapshots collect a group of bus and effect settings into
a named state. Typical snapshots include `Default`, `Paused`, `Underwater`,
`Indoor`, and `Cinematic`.

## Sound cues

A cue is higher-level playback logic built from one or more sounds:

- **Random:** chooses a variation, optionally using weights.
- **Sequence:** advances through entries in order.
- **Layered:** starts multiple entries together.

Cue entries can vary volume, pitch, delay, and spatial behavior. The cue can
also enforce cooldown and simultaneous-instance limits. Use cues for repeated
events so variation and concurrency policy remain data-driven.

## Adaptive music

Adaptive music contains named states and stems. A state controls which stems
are audible and how transitions occur. BPM and beat synchronization allow a
transition to line up with musical timing, while crossfades avoid abrupt cuts.

Gameplay code should request a music state, rather than manually starting and
stopping every stem.

## Runtime event integration

`RuntimeAudioSystem` can respond to:

- `AudioSourceComponent` creation and lifetime;
- animation events;
- collision and trigger actions;
- explicit gameplay requests;
- source/listener movement;
- periodic occlusion checks.

Animation events are the preferred way to synchronize footsteps, weapon
swishes, impacts, and spell releases with an animation.

## Audio editor

The editor provides:

- waveform inspection and trimming;
- cue authoring and preview;
- music state and stem setup;
- mixer buses, effects, and snapshot controls;
- playback preview;
- runtime voice and budget diagnostics.

Use the Audio Editor for source and cue authoring. Use the Audio Mixer for
project-wide buses, effects, and snapshots.

## Script usage

Scripts access sound through the gameplay script API. Common operations are:

1. play a one-shot sound or cue at a world position;
2. start a persistent attached source;
3. retain its handle;
4. update, fade, or stop it later;
5. select a mix snapshot or adaptive music state.

Use engine asset paths or registered asset identity rather than external source
file paths. That keeps editor Play and exported builds consistent.

## Performance guidance

- Prefer mono audio for spatial effects.
- Limit long, high-quality, simultaneously decoded sources.
- Use cue instance limits for rapidly repeated gameplay events.
- Reduce occlusion update frequency for distant sources.
- Route sounds correctly so project-wide volume controls remain cheap.
- Inspect active voices and bus levels in the audio diagnostics before changing
  backend settings.

## Important source files

- `engine/include/engine/audio/AudioEngine.h`
- `engine/include/engine/audio/AudioAsset.h`
- `engine/include/engine/audio/AudioTypes.h`
- `engine/include/engine/audio/RuntimeAudioSystem.h`
- `engine/include/engine/audio/AudioEditing.h`
- `editor/include/EditorPanels.h`

For a task-oriented guide, also see `docs/AUDIO_SYSTEM.md`.

