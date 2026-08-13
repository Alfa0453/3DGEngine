# Weather Editor

Open **Panels > World & Gameplay > Weather Editor**, or double-click a
`.3dgweather` asset in Content.

## Create a weather preset

1. Select **New**, then choose **Clear**, **Rain**, or **Snow** as a starting point.
2. Set the time of day, sky light, sun, clouds, cloud shadows, and fog.
3. Configure precipitation intensity, drop or flake size, fall speed, and wind.
4. For storms, enable lightning and adjust its frequency and intensity.
5. Set surface wetness and puddle amount for materials or gameplay systems that
   consume those values.
6. Optionally choose a saved particle system and ambient audio cue. These are
   stored as stable asset dependencies.
7. Set the default blend time and preview seed, then select **Save**.

New assets are saved under `Content/Assets/Weather` unless an existing weather
asset was opened.

## Preview and apply

The right-hand preview is isolated from the active level. **Preview Transition**
shows the deterministic blend from clear weather into the authored preset.

Use **Capture Level** to copy the active level's atmosphere into the preset. Use
**Apply to Level** to copy the preset's supported sky, cloud, cloud-shadow, sun,
and fog values into World Settings. Saving the scene then carries those visual
settings into editor Play and packaged runtime scenes.

Precipitation, lightning, wind, wetness, particle, and ambient-audio values remain
part of the reusable weather asset so gameplay weather controllers can transition
them without duplicating atmospheric data.

## Asset dependencies

Weather assets participate in the Asset Registry and Asset Dependency Viewer.
Missing particle or audio references can therefore be found before packaging.
