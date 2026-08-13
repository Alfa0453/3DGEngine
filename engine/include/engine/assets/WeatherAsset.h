#pragma once

#include "engine/assets/AssetIdentity.h"

#include <glm/vec3.hpp>

#include <string>

namespace engine {

enum class PrecipitationType { None = 0, Rain, Snow };

struct WeatherAssetData {
    int version = 1;
    AssetHandle assetId;
    std::string name = "Clear";

    float timeOfDay = 0.46f;
    float skyLightIntensity = 1.0f;
    float sunIntensity = 1.0f;
    bool clouds = true;
    float cloudCoverage = 0.45f;
    float cloudDensity = 0.75f;
    float cloudScale = 1.35f;
    float cloudSoftness = 0.18f;
    float cloudWindSpeed = 0.025f;
    float cloudWindDirection = 25.0f;
    glm::vec3 cloudColor{1.0f, 0.98f, 0.94f};
    bool cloudShadows = true;
    float cloudShadowStrength = 0.45f;

    bool fog = true;
    glm::vec3 fogColor{0.58f, 0.68f, 0.80f};
    float fogDensity = 0.008f;
    float fogHeight = -0.35f;
    float fogHeightFalloff = 0.10f;

    PrecipitationType precipitation = PrecipitationType::None;
    float precipitationIntensity = 0.0f;
    float precipitationSize = 0.35f;
    float precipitationSpeed = 14.0f;
    float windStrength = 0.0f;
    float windDirection = 25.0f;
    bool lightning = false;
    float lightningFrequency = 0.08f;
    float lightningIntensity = 2.0f;
    float surfaceWetness = 0.0f;
    float puddleAmount = 0.0f;
    float defaultTransitionSeconds = 3.0f;
    unsigned previewSeed = 1337;

    std::string particleAssetPath;
    AssetHandle particleAssetId;
    std::string ambientAudioPath;
    AssetHandle ambientAudioId;
};

void NormalizeWeather(WeatherAssetData& weather);
WeatherAssetData BlendWeather(const WeatherAssetData& from,
                              const WeatherAssetData& to, float alpha);
bool SaveWeatherAsset(const std::string& path, WeatherAssetData& weather,
                      std::string* error = nullptr);
bool LoadWeatherAsset(const std::string& path, WeatherAssetData* weather,
                      std::string* error = nullptr);

} // namespace engine
