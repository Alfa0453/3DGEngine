# 3DG Engine Audio System

## Audio sources

Add an **Audio Source** component to an object and assign either a normal audio
clip or a `.3dgaudio` cue. Sources support 2D/3D playback, autoplay, looping,
mixer routing, attenuation, Doppler, directional cones, occlusion and voice
priority. During Play mode the engine updates source position, direction and
velocity from the object transform. Physics geometry between the active camera
listener and a spatial source automatically applies obstruction attenuation.

## Gameplay audio cues

Open **Panels > Audio Editor > Gameplay Audio Cue**. A cue can operate in:

- **Random** mode, with weighted selection and immediate-repeat prevention.
- **Sequence** mode, advancing through its clips.
- **Layered** mode, playing all clips with optional per-layer delays.

Cues also store volume and pitch variation, cooldown, maximum instances,
priority, spatial behavior and mixer routing. Save cues as `.3dgaudio`; they can
be assigned directly to an Audio Source or played from a script.

```cpp
PlayAudioCue("Content/Audio/FireSpell.3dgaudio");
PlayAudioCue("Content/Audio/UIConfirm.3dgaudio", false);
```

## Adaptive music

Create a `.3dgmusic` asset in **Audio Editor > Adaptive Music**. Each state has
one or more synchronized looping stems, BPM, volume and crossfade duration.

```cpp
LoadAdaptiveMusic("Content/Audio/GameMusic.3dgmusic");
SetMusicState("Exploration");
SetMusicState("Combat", true); // beat-aligned transition
```

## Runtime source control

Scripts attached to an object with an Audio Source can use:

```cpp
PlayAudio(true);
PauseAudio();
ResumeAudio();
StopAudio();
SeekAudio(1.5f);
SetAudioVolume(0.8f);
SetAudioPitch(1.05f);
SetAudioLooping(true);
SetAudioSpatial(true);
SetAudioAttenuation(1.0f, 35.0f, 1.0f);
SetAudioDoppler(1.0f);
SetAudioCone(90.0f, 180.0f, 0.2f);
SetAudioOcclusion(0.0f);
SetAudioPriority(80);
SetAudioBus(engine::AudioBus::SFX);
```

Animation events use `Audio.Play`, `Audio.Restart`, `Audio.Pause`,
`Audio.Resume`, or `Audio.Stop`. Append `:ObjectName` to target another source,
for example `Audio.Restart:MagicStaff`.

## Mixer

The Audio Mixer contains Master, Music, SFX, Dialogue, UI and Ambient buses.
Each bus supports volume, mute, low/high-pass filters, reverb and compression.
The panel also exposes the voice budget, active voice diagnostics, streaming
count, voice stealing/rejection totals and output device format.

Save project mixer setups as `.3dgmixer` assets. Snapshots provide timed
transitions for Default, Paused, Underwater, Indoor and Cinematic mixes.
Dialogue ducking automatically lowers the Music bus while dialogue is active.

## Audio Editor

The waveform editor supports mouse selection, zoom and scrolling, trim,
reverse, gain, normalize, fades, cut/copy/paste, delete, silence, selection
preview, 32-step undo/redo, generated tones/noise and WAV export.

## Performance behavior

The default global budget is 64 voices. When full, a new voice replaces the
oldest voice with a lower or equal priority. Lower-priority requests are
rejected. Long looping non-spatial sources and adaptive music stems stream from
disk; short sound effects are decoded and pooled.
