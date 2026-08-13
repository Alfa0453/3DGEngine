#include "engine/assets/DayNightTimelineAsset.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <glm/common.hpp>

namespace engine {
namespace {
void Error(std::string* error, const std::string& text) { if (error) *error = text; }
float Wrap01(float value) { value = std::fmod(value, 1.0f); return value < 0.0f ? value + 1.0f : value; }
float Lerp(float a, float b, float t) { return a + (b - a) * t; }
glm::vec3 Lerp(const glm::vec3& a, const glm::vec3& b, float t) { return a + (b - a) * t; }
void AddDependency(std::vector<AssetHandle>& deps, AssetHandle id) {
    if (id.Valid() && std::find(deps.begin(), deps.end(), id) == deps.end()) deps.push_back(id);
}
}

void NormalizeDayNightTimeline(DayNightTimelineAssetData& timeline) {
    timeline.dayLengthSeconds = std::clamp(timeline.dayLengthSeconds, 1.0f, 86400.0f);
    timeline.playbackRate = std::clamp(timeline.playbackRate, 0.0f, 100.0f);
    for (auto& key : timeline.keys) {
        key.time = Wrap01(key.time);
        key.skyIntensity = std::clamp(key.skyIntensity, 0.0f, 16.0f);
        key.skyLightIntensity = std::clamp(key.skyLightIntensity, 0.0f, 8.0f);
        key.sunIntensity = std::clamp(key.sunIntensity, 0.0f, 16.0f);
        key.cloudCoverage = std::clamp(key.cloudCoverage, 0.0f, 1.0f);
        key.cloudDensity = std::clamp(key.cloudDensity, 0.0f, 4.0f);
        key.cloudWindSpeed = std::clamp(key.cloudWindSpeed, 0.0f, 4.0f);
        key.cloudWindDirection = Wrap01(key.cloudWindDirection / 360.0f) * 360.0f;
        key.cloudColor = glm::clamp(key.cloudColor, glm::vec3(0.0f), glm::vec3(1.0f));
        key.fogDensity = std::clamp(key.fogDensity, 0.0f, 1.0f);
        key.fogHeight = std::clamp(key.fogHeight, -10000.0f, 10000.0f);
        key.fogHeightFalloff = std::clamp(key.fogHeightFalloff, 0.0f, 10.0f);
        key.fogColor = glm::clamp(key.fogColor, glm::vec3(0.0f), glm::vec3(1.0f));
        key.windStrength = std::clamp(key.windStrength, 0.0f, 4.0f);
        key.windDirection = Wrap01(key.windDirection / 360.0f) * 360.0f;
    }
    std::stable_sort(timeline.keys.begin(), timeline.keys.end(),
        [](const auto& a, const auto& b) { return a.time < b.time; });
}

bool ValidateDayNightTimeline(const DayNightTimelineAssetData& timeline, std::string* error) {
    if (timeline.name.empty()) { Error(error, "Timeline name is empty."); return false; }
    if (timeline.keys.empty()) { Error(error, "Timeline requires at least one keyframe."); return false; }
    for (std::size_t i = 1; i < timeline.keys.size(); ++i) {
        if (std::abs(timeline.keys[i].time - timeline.keys[i - 1].time) < 0.00001f) {
            Error(error, "Timeline contains duplicate keyframe times."); return false;
        }
    }
    Error(error, {}); return true;
}

DayNightKeyframe SampleDayNightTimeline(const DayNightTimelineAssetData& source, float time) {
    DayNightTimelineAssetData timeline = source; NormalizeDayNightTimeline(timeline);
    if (timeline.keys.empty()) return {};
    if (timeline.keys.size() == 1) return timeline.keys.front();
    time = Wrap01(time);
    std::size_t next = 0;
    while (next < timeline.keys.size() && timeline.keys[next].time <= time) ++next;
    const std::size_t right = next % timeline.keys.size();
    const std::size_t left = (right + timeline.keys.size() - 1) % timeline.keys.size();
    const auto& a = timeline.keys[left]; const auto& b = timeline.keys[right];
    float start = a.time, end = b.time, sample = time;
    if (right == 0) { end += 1.0f; if (sample < start) sample += 1.0f; }
    const float t = std::clamp((sample - start) / std::max(end - start, 0.00001f), 0.0f, 1.0f);
    const float s = t * t * (3.0f - 2.0f * t);
    DayNightKeyframe out = a; out.time = time;
#define BLEND(field) out.field = Lerp(a.field, b.field, s)
    BLEND(skyIntensity); BLEND(skyLightIntensity); BLEND(sunIntensity);
    BLEND(cloudCoverage); BLEND(cloudDensity); BLEND(cloudWindSpeed);
    BLEND(cloudWindDirection); BLEND(cloudColor); BLEND(fogDensity);
    BLEND(fogHeight); BLEND(fogHeightFalloff); BLEND(fogColor);
    BLEND(windStrength); BLEND(windDirection);
#undef BLEND
    out.ambientAudioPath = t < 0.5f ? a.ambientAudioPath : b.ambientAudioPath;
    out.ambientAudioId = t < 0.5f ? a.ambientAudioId : b.ambientAudioId;
    out.eventName.clear();
    return out;
}

std::vector<std::string> DayNightEventsBetween(const DayNightTimelineAssetData& timeline,
                                                float previous, float current, bool wrapped) {
    std::vector<std::string> events;
    previous = Wrap01(previous); current = Wrap01(current);
    for (const auto& key : timeline.keys) if (!key.eventName.empty()) {
        const bool crossed = wrapped ? (key.time > previous || key.time <= current)
                                     : (key.time > previous && key.time <= current);
        if (crossed) events.push_back(key.eventName);
    }
    return events;
}

bool SaveDayNightTimelineAsset(const std::string& path, DayNightTimelineAssetData timeline,
                               std::string* error) {
    NormalizeDayNightTimeline(timeline);
    if (!ValidateDayNightTimeline(timeline, error)) return false;
    timeline.header.type = AssetType::DayNightTimeline;
    timeline.header.assetVersion = kDayNightTimelineAssetVersion;
    if (!timeline.header.id.Valid()) timeline.header.id = AssetHandle::Generate();
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream out(path, std::ios::trunc);
    if (!out) { Error(error, "Could not write timeline: " + path); return false; }
    out << "3DGDayNight " << kDayNightTimelineAssetVersion << '\n'
        << timeline.header.id.ToString() << '\n' << std::quoted(timeline.name) << ' '
        << timeline.dayLengthSeconds << ' ' << timeline.playbackRate << ' '
        << timeline.loop << ' ' << timeline.autoplay << ' ' << timeline.keys.size() << '\n';
    std::vector<AssetHandle> deps;
    for (const auto& k : timeline.keys) {
        out << k.time << ' ' << k.skyIntensity << ' ' << k.skyLightIntensity << ' '
            << k.sunIntensity << ' ' << k.cloudCoverage << ' ' << k.cloudDensity << ' '
            << k.cloudWindSpeed << ' ' << k.cloudWindDirection << ' '
            << k.cloudColor.x << ' ' << k.cloudColor.y << ' ' << k.cloudColor.z << ' '
            << k.fogDensity << ' ' << k.fogHeight << ' ' << k.fogHeightFalloff << ' '
            << k.fogColor.x << ' ' << k.fogColor.y << ' ' << k.fogColor.z << ' '
            << k.windStrength << ' ' << k.windDirection << ' '
            << std::quoted(k.ambientAudioPath) << ' '
            << (k.ambientAudioId.Valid() ? k.ambientAudioId.ToString() : "-") << ' '
            << std::quoted(k.eventName) << '\n';
        AddDependency(deps, k.ambientAudioId);
    }
    out << "ASSET_DEPS " << deps.size(); for (auto id : deps) out << ' ' << id.ToString(); out << '\n';
    Error(error, {}); return static_cast<bool>(out);
}

bool LoadDayNightTimelineAsset(const std::string& path, DayNightTimelineAssetData* output,
                               std::string* error) {
    if (!output) { Error(error, "Timeline output is null."); return false; }
    std::ifstream in(path); std::string magic, id; int version = 0; std::size_t count = 0;
    DayNightTimelineAssetData timeline;
    in >> magic >> version >> id >> std::quoted(timeline.name) >> timeline.dayLengthSeconds
       >> timeline.playbackRate >> timeline.loop >> timeline.autoplay >> count;
    if (!in || magic != "3DGDayNight" || version < 1 || version > static_cast<int>(kDayNightTimelineAssetVersion)
        || !AssetHandle::Parse(id, &timeline.header.id)) {
        Error(error, "Timeline asset is malformed: " + path); return false;
    }
    timeline.header.type = AssetType::DayNightTimeline; timeline.header.assetVersion = version;
    timeline.keys.resize(count);
    for (auto& k : timeline.keys) {
        std::string audioId;
        in >> k.time >> k.skyIntensity >> k.skyLightIntensity >> k.sunIntensity
           >> k.cloudCoverage >> k.cloudDensity >> k.cloudWindSpeed >> k.cloudWindDirection
           >> k.cloudColor.x >> k.cloudColor.y >> k.cloudColor.z >> k.fogDensity
           >> k.fogHeight >> k.fogHeightFalloff >> k.fogColor.x >> k.fogColor.y >> k.fogColor.z
           >> k.windStrength >> k.windDirection >> std::quoted(k.ambientAudioPath)
           >> audioId >> std::quoted(k.eventName);
        if (!in || (audioId != "-" && !AssetHandle::Parse(audioId, &k.ambientAudioId))) {
            Error(error, "Timeline keyframe is malformed: " + path); return false;
        }
        AddDependency(timeline.header.dependencies, k.ambientAudioId);
    }
    NormalizeDayNightTimeline(timeline);
    if (!ValidateDayNightTimeline(timeline, error)) return false;
    *output = std::move(timeline); Error(error, {}); return true;
}

DayNightTimelineRuntime& DayNightTimelineRuntime::Instance() { static DayNightTimelineRuntime value; return value; }
bool DayNightTimelineRuntime::Load(const std::string& path, std::string* error) {
    DayNightTimelineAssetData value; if (!LoadDayNightTimelineAsset(path, &value, error)) return false;
    m_asset = std::move(value); m_time = m_asset.keys.empty() ? 0.0f : m_asset.keys.front().time;
    m_loaded = true; m_playing = m_asset.autoplay; m_events.clear(); return true;
}
void DayNightTimelineRuntime::Play() { if (m_loaded) m_playing = true; }
void DayNightTimelineRuntime::Pause() { m_playing = false; }
void DayNightTimelineRuntime::Stop() { m_playing = false; m_time = m_asset.keys.empty() ? 0.0f : m_asset.keys.front().time; m_events.clear(); }
void DayNightTimelineRuntime::SetTime(float value) { m_time = Wrap01(value); }
void DayNightTimelineRuntime::SetPlaybackRate(float rate) { m_asset.playbackRate = std::clamp(rate, 0.0f, 100.0f); }
void DayNightTimelineRuntime::Tick(float dt) {
    if (!m_loaded || !m_playing || dt <= 0.0f) return;
    const float previous = m_time;
    const float advance = dt * m_asset.playbackRate / std::max(m_asset.dayLengthSeconds, 1.0f);
    float next = previous + advance; const bool wrapped = next >= 1.0f;
    if (wrapped && !m_asset.loop) { next = 1.0f - 0.000001f; m_playing = false; }
    else next = Wrap01(next);
    auto events = DayNightEventsBetween(m_asset, previous, next, wrapped);
    m_events.insert(m_events.end(), events.begin(), events.end()); m_time = next;
}
DayNightKeyframe DayNightTimelineRuntime::Sample() const { return SampleDayNightTimeline(m_asset, m_time); }
std::vector<std::string> DayNightTimelineRuntime::TakeEvents() { auto result = std::move(m_events); m_events.clear(); return result; }

} // namespace engine
