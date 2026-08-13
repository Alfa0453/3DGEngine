#pragma once

#include "engine/assets/AssetIdentity.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

inline constexpr std::uint32_t kDayNightTimelineAssetVersion = 1;

struct DayNightKeyframe {
    float time = 0.5f; // normalized 24-hour clock
    float skyIntensity = 1.0f;
    float skyLightIntensity = 1.0f;
    float sunIntensity = 1.0f;
    float cloudCoverage = 0.45f;
    float cloudDensity = 0.75f;
    float cloudWindSpeed = 0.025f;
    float cloudWindDirection = 25.0f;
    glm::vec3 cloudColor{1.0f, 0.98f, 0.94f};
    float fogDensity = 0.008f;
    float fogHeight = -0.35f;
    float fogHeightFalloff = 0.10f;
    glm::vec3 fogColor{0.58f, 0.68f, 0.80f};
    float windStrength = 0.0f;
    float windDirection = 0.0f;
    std::string ambientAudioPath;
    AssetHandle ambientAudioId;
    std::string eventName;
};

struct DayNightTimelineAssetData {
    NativeAssetHeader header;
    std::string name = "DayNightTimeline";
    float dayLengthSeconds = 600.0f;
    float playbackRate = 1.0f;
    bool loop = true;
    bool autoplay = true;
    std::vector<DayNightKeyframe> keys;
};

void NormalizeDayNightTimeline(DayNightTimelineAssetData& timeline);
bool ValidateDayNightTimeline(const DayNightTimelineAssetData& timeline,
                              std::string* error = nullptr);
DayNightKeyframe SampleDayNightTimeline(const DayNightTimelineAssetData& timeline,
                                        float normalizedTime);
std::vector<std::string> DayNightEventsBetween(
    const DayNightTimelineAssetData& timeline, float previousTime,
    float currentTime, bool wrapped);
bool SaveDayNightTimelineAsset(const std::string& path,
                               DayNightTimelineAssetData timeline,
                               std::string* error = nullptr);
bool LoadDayNightTimelineAsset(const std::string& path,
                               DayNightTimelineAssetData* timeline,
                               std::string* error = nullptr);

// Process-wide playback state. Script calls control it and the editor/player host
// consumes its sample each frame. It owns no renderer resources.
class DayNightTimelineRuntime {
public:
    static DayNightTimelineRuntime& Instance();
    bool Load(const std::string& path, std::string* error = nullptr);
    void Play();
    void Pause();
    void Stop();
    void SetTime(float normalizedTime);
    void SetPlaybackRate(float rate);
    void Tick(float deltaSeconds);
    bool Loaded() const { return m_loaded; }
    bool Playing() const { return m_playing; }
    float Time() const { return m_time; }
    const DayNightTimelineAssetData& Asset() const { return m_asset; }
    DayNightKeyframe Sample() const;
    std::vector<std::string> TakeEvents();
private:
    DayNightTimelineAssetData m_asset;
    float m_time = 0.0f;
    bool m_loaded = false;
    bool m_playing = false;
    std::vector<std::string> m_events;
};

} // namespace engine
