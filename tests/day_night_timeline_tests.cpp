#include <engine/assets/DayNightTimelineAsset.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>

namespace {
int failures = 0;
void Check(bool condition, const char* text) { if (!condition) { ++failures; std::cerr << text << '\n'; } }
bool Near(float a, float b, float epsilon = 0.001f) { return std::abs(a - b) <= epsilon; }
}

int main() {
    engine::DayNightTimelineAssetData timeline;
    timeline.name = "Regression Day"; timeline.dayLengthSeconds = 100.0f;
    engine::DayNightKeyframe dawn; dawn.time = 0.25f; dawn.sunIntensity = 0.2f;
    dawn.fogDensity = 0.08f; dawn.eventName = "Sunrise";
    engine::DayNightKeyframe noon; noon.time = 0.5f; noon.sunIntensity = 2.0f;
    noon.fogDensity = 0.0f;
    engine::DayNightKeyframe night; night.time = 0.9f; night.sunIntensity = 0.0f;
    night.eventName = "Night"; night.ambientAudioPath = "Audio/Night.3dgaudio";
    night.ambientAudioId = {3, 9};
    timeline.keys = {night, dawn, noon};
    engine::NormalizeDayNightTimeline(timeline);
    std::string error;
    Check(engine::ValidateDayNightTimeline(timeline, &error), "valid timeline rejected");
    Check(timeline.keys.front().time == 0.25f, "keyframes were not sorted");
    const auto middle = engine::SampleDayNightTimeline(timeline, 0.375f);
    Check(middle.sunIntensity > dawn.sunIntensity && middle.sunIntensity < noon.sunIntensity,
          "keyframe interpolation failed");
    const auto midnight = engine::SampleDayNightTimeline(timeline, 0.05f);
    Check(midnight.sunIntensity >= 0.0f && midnight.sunIntensity <= 0.2f,
          "circular midnight interpolation failed");
    const auto events = engine::DayNightEventsBetween(timeline, 0.85f, 0.30f, true);
    Check(events.size() == 2, "wrapped events did not return both markers");
    Check(std::find(events.begin(), events.end(), "Sunrise") != events.end()
          && std::find(events.begin(), events.end(), "Night") != events.end(),
          "wrapped event names are missing");

    const auto root = std::filesystem::temp_directory_path() / "3dg_day_night_test";
    const auto path = root / "Regression.3dgdaynight";
    std::error_code ec; std::filesystem::remove_all(root, ec);
    Check(engine::SaveDayNightTimelineAsset(path.string(), timeline, &error), "timeline save failed");
    engine::DayNightTimelineAssetData loaded;
    Check(engine::LoadDayNightTimelineAsset(path.string(), &loaded, &error), "timeline load failed");
    Check(loaded.header.type == engine::AssetType::DayNightTimeline
          && loaded.header.id.Valid() && loaded.header.dependencies.size() == 1
          && loaded.keys.size() == 3, "timeline identity/dependencies did not round trip");
    auto& runtime = engine::DayNightTimelineRuntime::Instance();
    Check(runtime.Load(path.string(), &error), "runtime timeline load failed");
    runtime.SetTime(0.49f); runtime.SetPlaybackRate(1.0f); runtime.Play(); runtime.Tick(2.0f);
    Check(Near(runtime.Time(), 0.51f, 0.002f), "runtime time advancement failed");
    runtime.Pause(); const float paused = runtime.Time(); runtime.Tick(5.0f);
    Check(Near(runtime.Time(), paused), "paused timeline advanced");
    std::filesystem::remove_all(root, ec);
    return failures == 0 ? 0 : 1;
}
