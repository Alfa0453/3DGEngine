#include "engine/graphics/LightingScalability.h"

#include "engine/graphics/DynamicIrradiance.h"
#include "engine/graphics/PbrRenderer.h"
#include "engine/graphics/PostProcess.h"
#include "engine/graphics/ReflectionProbeSystem.h"
#include "engine/graphics/SSGI.h"

#include <array>

namespace engine {
namespace {
const std::array<LightingQualityProfile, 4> kProfiles{{
    {LightingQuality::Low, 4, 6, {1,4,8,16}, 1, false, 16, 2,
     6,0.16f,0,120.0f,32ull*1024ull*1024ull,4,6,24,4,8,8,3,128},
    {LightingQuality::Medium, 8, 12, {1,3,6,12}, 2, true, 24, 6,
     8,0.22f,1,220.0f,96ull*1024ull*1024ull,6,4,40,8,16,4,4,192},
    {LightingQuality::High, 12, 18, {1,2,4,8}, 4, true, 48, 12,
     12,0.30f,2,300.0f,256ull*1024ull*1024ull,8,3,56,12,24,2,5,256},
    {LightingQuality::Ultra, 16, 24, {1,1,2,4}, 4, true, 96, 24,
     20,0.35f,2,500.0f,512ull*1024ull*1024ull,12,2,72,16,40,1,6,256}
}};
}

const LightingQualityProfile& GetLightingQualityProfile(LightingQuality quality) {
    const int index = static_cast<int>(quality);
    return kProfiles[static_cast<std::size_t>(index >= 0 && index < 4 ? index : 2)];
}

void ApplyLightingQuality(const LightingQualityProfile& profile,
                          PbrRenderer::Options* pbr,
                          DynamicIrradianceSettings* gi,
                          SSGI* ssgi,
                          ReflectionProbeSystem* reflections,
                          PostProcess* post) {
    if (pbr) {
        pbr->shadowBlockerSamples = profile.shadowBlockerSamples;
        pbr->shadowFilterSamples = profile.shadowFilterSamples;
        pbr->maxShadowedLocalLights = profile.maxShadowedLocalLights;
    }
    if (gi) {
        gi->enabled = gi->enabled && profile.dynamicGi;
        gi->raysPerProbe = profile.giRaysPerProbe;
        gi->probesPerFrame = profile.giProbesPerFrame;
        gi->maxGiRaysPerFrame = profile.giRaysPerProbe * profile.giProbesPerFrame;
        gi->Normalize();
    }
    if (ssgi) { ssgi->steps = profile.ssgiSteps; ssgi->intensity = profile.ssgiIntensity; }
    if (reflections) reflections->SetStreaming(profile.reflectionStreamingDistance,
                                                profile.reflectionBudgetBytes,
                                                profile.maxReflectionProbes);
    if (post) {
        post->volumetrics.xyDownsample = profile.volumetricDownsample;
        post->volumetrics.depthSlices = profile.volumetricSlices;
        post->settings.bloomLevels = profile.bloomLevels;
    }
}

} // namespace engine
