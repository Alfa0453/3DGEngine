#pragma once

#include "engine/graphics/PbrRenderer.h"

#include <cstdint>

namespace engine {

class PostProcess;
class SSGI;
class ReflectionProbeSystem;
struct DynamicIrradianceSettings;

enum class LightingQuality : std::uint8_t { Low = 0, Medium, High, Ultra, Custom };

// One authoritative cost profile. Individual systems consume this profile so
// their presets cannot silently disagree about what Low/Medium/High means.
struct LightingQualityProfile {
    LightingQuality quality = LightingQuality::High;
    int shadowBlockerSamples = 12;
    int shadowFilterSamples = 18;
    int cascadeIntervals[4] = {1, 2, 4, 8};
    int maxShadowedLocalLights = 4;
    bool dynamicGi = true;
    std::uint32_t giRaysPerProbe = 48;
    std::uint32_t giProbesPerFrame = 12;
    int ssgiSteps = 12;
    float ssgiIntensity = 0.30f;
    int maxReflectionProbes = 2;
    float reflectionStreamingDistance = 300.0f;
    std::uint64_t reflectionBudgetBytes = 256ull * 1024ull * 1024ull;
    int gtaoDirections = 8;
    int volumetricDownsample = 3;
    int volumetricSlices = 56;
    int maxVolumetricLights = 12;
    int cloudSamples = 24;
    int cloudUpdateInterval = 2;
    int bloomLevels = 5;
    int histogramSize = 256;
};

const LightingQualityProfile& GetLightingQualityProfile(LightingQuality quality);
void ApplyLightingQuality(const LightingQualityProfile& profile,
                          PbrRenderer::Options* pbr,
                          DynamicIrradianceSettings* gi,
                          SSGI* ssgi,
                          ReflectionProbeSystem* reflections,
                          PostProcess* post);

} // namespace engine
