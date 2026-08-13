# Day/Night Timeline Guide

Open **Panels > World & Gameplay > Day/Night Timeline**, or double-click a
`.3dgdaynight` asset in Content.

## Author a timeline

1. Press **New** and choose the real-time duration of one game day.
2. Add keys to the normalized 24-hour track. `0.00` is midnight, `0.25` is
   sunrise, `0.50` is noon, and `0.75` is sunset.
3. At each key, edit sun and skylight strength, sky brightness, clouds, fog,
   wind, ambient audio, and an optional gameplay event name.
4. Use **Capture Level** to copy the active World Settings into the selected key.
5. Scrub the track or press **Play** while **Preview in Level** is enabled.
6. Save the asset. Press **Use as Level Default** to make it autoplay when that
   level is launched or packaged.

Interpolation is smooth and circular, including the last-to-first transition at
midnight. Ambient audio changes at the midpoint between neighboring keys.

## C++ gameplay controls

```cpp
LoadDayNightTimeline("Content/GameAssets/Timelines/Adventure.3dgdaynight");
SetDayNightTime(0.24f);          // just before sunrise
SetDayNightPlaybackRate(2.0f);  // twice the authored rate
PlayDayNightTimeline();

if (WasDayNightEvent("Sunrise")) {
    // Start morning encounters, music, or NPC schedules.
}
```

Available controls also include `PauseDayNightTimeline()`,
`StopDayNightTimeline()`, and `DayNightTime()`.

## Lua gameplay controls

```lua
Engine.LoadDayNightTimeline(
    "Content/GameAssets/Timelines/Adventure.3dgdaynight", true)
Engine.SetDayNightTime(0.24)
Engine.SetDayNightPlaybackRate(2.0)

function OnUpdate(dt)
    if Engine.WasDayNightEvent("Sunrise") then
        -- Start morning gameplay.
    end
end
```

Lua also exposes `PlayDayNightTimeline`, `PauseDayNightTimeline`,
`StopDayNightTimeline`, and `GetDayNightTime`.
