#include "engine/graphics/LightingBuildData.h"
#include "engine/graphics/LightingScene.h"

#include <glad/glad.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

namespace engine {
namespace {

constexpr char kMagic[8] = {'3','D','G','L','I','T','E','5'};
constexpr float kPi = 3.14159265358979323846f;

struct PathCounters {
    std::uint64_t segments = 0;
    std::uint64_t misses = 0;
    std::uint64_t energyTerminations = 0;
    std::uint64_t maxBounceTerminations = 0;
    std::uint64_t shadowRays = 0;
    std::uint64_t textureSamples = 0;
    std::uint64_t emissiveHits = 0;
};

std::array<float, 4> EvaluateSH4Basis(const glm::vec3& direction) {
    return {0.2820947918f, 0.4886025119f * direction.y,
            0.4886025119f * direction.z, 0.4886025119f * direction.x};
}

template <class T> void HashBytes(std::uint64_t& h, const T& value) {
    const auto* p = reinterpret_cast<const unsigned char*>(&value);
    for (std::size_t i = 0; i < sizeof(T); ++i) { h ^= p[i]; h *= 1099511628211ull; }
}

glm::vec3 SphereDirection(std::uint32_t i, std::uint32_t count) {
    constexpr float golden = 2.39996322972865332f;
    const float y = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / static_cast<float>(count);
    const float radius = std::sqrt(std::max(0.0f, 1.0f - y * y));
    const float angle = golden * static_cast<float>(i);
    return glm::normalize(glm::vec3(std::cos(angle) * radius, y,
                                    std::sin(angle) * radius));
}

float RadicalInverse(std::uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

glm::vec3 CosineHemisphere(const glm::vec3& inputNormal,
                           std::uint32_t pathIndex,
                           std::uint32_t pathCount,
                           std::uint32_t scramble) {
    const glm::vec3 normal = glm::normalize(inputNormal);
    const float u = (static_cast<float>(pathIndex) + 0.5f)
        / static_cast<float>(std::max(pathCount, 1u));
    const float v = RadicalInverse(pathIndex ^ scramble);
    const float radius = std::sqrt(u);
    const float phi = 2.0f * kPi * v;
    const glm::vec3 local(radius * std::cos(phi), radius * std::sin(phi),
                          std::sqrt(std::max(0.0f, 1.0f - u)));
    const glm::vec3 helper = std::abs(normal.z) < 0.999f
        ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 tangent = glm::normalize(glm::cross(helper, normal));
    const glm::vec3 bitangent = glm::cross(normal, tangent);
    return glm::normalize(tangent * local.x + bitangent * local.y
                          + normal * local.z);
}

float SrgbToLinear(float value) {
    value = glm::clamp(value, 0.0f, 1.0f);
    return value <= 0.04045f ? value / 12.92f
        : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

glm::vec3 SampleBaseColor(const LightingRayHit& hit, bool useTextures,
                          PathCounters* counters) {
    glm::vec3 base = glm::clamp(hit.albedo, glm::vec3(0.0f), glm::vec3(1.0f));
    const auto& texture = hit.baseColorTexture;
    if (!useTextures || !texture || texture->width == 0 || texture->height == 0
        || texture->rgba.size()
            < static_cast<std::size_t>(texture->width) * texture->height * 4u)
        return base;
    auto address = [&](float value) {
        return texture->repeat ? value - std::floor(value)
                               : glm::clamp(value, 0.0f, 1.0f);
    };
    const float u = address(hit.uv.x);
    const float v = address(hit.uv.y);
    const float x = u * static_cast<float>(texture->width - 1u);
    const float y = v * static_cast<float>(texture->height - 1u);
    const std::uint32_t x0 = static_cast<std::uint32_t>(std::floor(x));
    const std::uint32_t y0 = static_cast<std::uint32_t>(std::floor(y));
    const std::uint32_t x1 = std::min(x0 + 1u, texture->width - 1u);
    const std::uint32_t y1 = std::min(y0 + 1u, texture->height - 1u);
    auto pixel = [&](std::uint32_t px, std::uint32_t py) {
        const std::size_t at = (static_cast<std::size_t>(py) * texture->width + px) * 4u;
        glm::vec3 color(texture->rgba[at], texture->rgba[at + 1u],
                        texture->rgba[at + 2u]);
        color /= 255.0f;
        if (texture->srgb)
            color = {SrgbToLinear(color.r), SrgbToLinear(color.g),
                     SrgbToLinear(color.b)};
        return color;
    };
    const glm::vec3 a = glm::mix(pixel(x0, y0), pixel(x1, y0), x - x0);
    const glm::vec3 b = glm::mix(pixel(x0, y1), pixel(x1, y1), x - x0);
    if (counters) ++counters->textureSamples;
    return base * glm::mix(a, b, y - y0);
}

float RayBias(const LightingRayHit& hit) {
    return glm::clamp(std::max(1e-4f, hit.distance * 2e-5f), 1e-4f, 0.01f);
}

glm::vec3 AdjustSaturation(glm::vec3 color, float saturation) {
    const float luminance = glm::dot(color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
    return glm::max(glm::mix(glm::vec3(luminance), color,
                            glm::clamp(saturation, 0.0f, 2.0f)), glm::vec3(0.0f));
}

glm::vec3 EvaluateBuildLights(const std::vector<LightingBuildLight>& lights,
                              const LightingSceneBvh& acceleration,
                              const LightingRayHit& hit, float maximumDistance,
                              PathCounters* counters) {
    glm::vec3 incoming(0.0f);
    for (const LightingBuildLight& light : lights) {
        // The authoritative environment sun is evaluated exactly once by
        // EvaluateSun. Build-light entries here are finite local emitters.
        if (light.type == LightingBuildLight::Type::Directional) continue;
        if(light.type==LightingBuildLight::Type::Rectangle){
            const glm::vec3 right=glm::normalize(light.right),up=glm::normalize(light.up);
            const glm::vec3 planeNormal=glm::normalize(glm::cross(right,up));
            if(!light.twoSided&&glm::dot(planeNormal,hit.position-light.position)<=0.0f)continue;
            const float area=std::max(4.0f*light.halfSize.x*light.halfSize.y,0.0001f);
            static constexpr glm::vec2 offsets[4]={{-0.5f,-0.5f},{0.5f,-0.5f},{0.5f,0.5f},{-0.5f,0.5f}};
            for(const glm::vec2 offset:offsets){const glm::vec3 sample=light.position+
                    right*(offset.x*2.0f*light.halfSize.x)+up*(offset.y*2.0f*light.halfSize.y);
                const glm::vec3 delta=sample-hit.position;const float distanceSquared=glm::dot(delta,delta);
                const float distance=std::sqrt(std::max(distanceSquared,1e-6f));if(distance>=light.range)continue;
                const glm::vec3 L=delta/distance;const float nDotL=glm::max(glm::dot(hit.normal,L),0.0f);
                const float lightCos=light.twoSided?std::abs(glm::dot(planeNormal,-L)):glm::max(glm::dot(planeNormal,-L),0.0f);
                if(nDotL<=0.0f||lightCos<=0.0f)continue;
                const float bias=RayBias(hit);if(counters)++counters->shadowRays;
                if(acceleration.Occluded(hit.position+hit.geometricNormal*bias,L,distance-bias))continue;
                const float normalized=distance/std::max(light.range,0.01f);const float window=glm::clamp(1.0f-std::pow(normalized,4.0f),0.0f,1.0f);
                incoming+=glm::max(light.color,glm::vec3(0.0f))*(area*0.25f)*lightCos*nDotL*window*window/std::max(distanceSquared,0.01f);}
            continue;
        }
        glm::vec3 L(0.0f); float attenuation = 1.0f; float shadowDistance = maximumDistance;
        if (light.type == LightingBuildLight::Type::Directional) {
            L = glm::normalize(-light.direction);
        } else {
            const glm::vec3 delta = light.position - hit.position;
            const float distanceSquared = glm::dot(delta, delta);
            const float distance = std::sqrt(std::max(distanceSquared, 1e-6f));
            if (distance >= light.range) continue;
            L = delta / distance; shadowDistance = distance - RayBias(hit);
            const float normalized = distance / std::max(light.range, 0.01f);
            const float window = glm::clamp(1.0f - normalized * normalized * normalized * normalized, 0.0f, 1.0f);
            attenuation = window * window / std::max(distanceSquared, 0.01f);
            if (light.type == LightingBuildLight::Type::Spot) {
                const float cone = glm::dot(glm::normalize(-light.direction), L);
                attenuation *= glm::smoothstep(light.outerCos, light.innerCos, cone);
            }
        }
        const float nDotL = glm::max(glm::dot(hit.normal, L), 0.0f);
        if (nDotL <= 0.0f) continue;
        if (counters) ++counters->shadowRays;
        if (acceleration.Occluded(hit.position + hit.geometricNormal * RayBias(hit),
                                  L, shadowDistance)) continue;
        incoming += glm::max(light.color, glm::vec3(0.0f)) * attenuation * nDotL;
    }
    return incoming;
}

glm::vec3 EvaluateSun(const DirectionalSkyRadiance& sky,
                      const LightingSceneBvh& acceleration,
                      const LightingRayHit& hit, float maximumDistance,
                      PathCounters* counters) {
    const glm::vec3 direction = glm::normalize(sky.sunDirection);
    const float nDotL = glm::max(glm::dot(hit.normal, direction), 0.0f);
    if (nDotL <= 0.0f) return glm::vec3(0.0f);
    if (counters) ++counters->shadowRays;
    if (acceleration.Occluded(hit.position + hit.geometricNormal * RayBias(hit),
                              direction, maximumDistance))
        return glm::vec3(0.0f);
    return glm::max(sky.sunRadiance, glm::vec3(0.0f)) * nDotL;
}

template <class T> bool Write(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value)); return !!out;
}
template <class T> bool Read(std::ifstream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(value)); return !!in;
}

} // namespace

glm::vec3 DirectionalSkyRadiance::Sample(const glm::vec3& input) const {
    const glm::vec3 direction = glm::normalize(input);
    const float above = glm::clamp(direction.y, 0.0f, 1.0f);
    glm::vec3 radiance = direction.y >= 0.0f
        ? glm::mix(glm::max(horizon, glm::vec3(0.0f)),
                   glm::max(zenith, glm::vec3(0.0f)), std::sqrt(above))
        : glm::max(ground, glm::vec3(0.0f));
    radiance += glm::max(sunRadiance, glm::vec3(0.0f))
        * std::pow(glm::clamp(glm::dot(direction, glm::normalize(sunDirection)), 0.0f, 1.0f),
                   glm::clamp(sunSharpness, 1.0f, 4096.0f));
    return radiance;
}

bool LightingBuildData::IsValid() const {
    if (version != kVersion || dimensions.x <= 0 || dimensions.y <= 0 || dimensions.z <= 0)
        return false;
    const std::size_t expected = static_cast<std::size_t>(dimensions.x) * dimensions.y * dimensions.z;
    return expected == probes.size() && glm::all(glm::greaterThan(boundsMax, boundsMin));
}

std::size_t LightingBuildData::Index(int x, int y, int z) const {
    return static_cast<std::size_t>((z * dimensions.y + y) * dimensions.x + x);
}

std::uint64_t HashLightingGeometry(const std::vector<LightingTriangle>& triangles,
                                   const LightingBuildSettings& settings) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const auto& t : triangles) {
        HashBytes(hash, t.a); HashBytes(hash, t.b); HashBytes(hash, t.c);
        HashBytes(hash,t.albedo); HashBytes(hash,t.emissive); HashBytes(hash,t.metallic);
        HashBytes(hash,t.normalA); HashBytes(hash,t.normalB); HashBytes(hash,t.normalC);
        HashBytes(hash,t.uvA); HashBytes(hash,t.uvB); HashBytes(hash,t.uvC);
        HashBytes(hash,t.entityId); HashBytes(hash,t.materialSlot);
        if (t.baseColorTexture) {
            HashBytes(hash,t.baseColorTexture->width); HashBytes(hash,t.baseColorTexture->height);
            HashBytes(hash,t.baseColorTexture->srgb); HashBytes(hash,t.baseColorTexture->repeat);
            for (std::uint8_t value : t.baseColorTexture->rgba) HashBytes(hash,value);
        }
    }
    HashBytes(hash, settings.quality); HashBytes(hash, settings.probeSpacing);
    HashBytes(hash, settings.maxRayDistance); HashBytes(hash, settings.boundsPadding);
    HashBytes(hash, settings.minimumVisibility); HashBytes(hash, settings.raysPerProbe);
    HashBytes(hash, settings.diffuseBounces);
    HashBytes(hash, settings.directionalIrradiance); HashBytes(hash, settings.indirectBounceStrength);
    HashBytes(hash, settings.indirectBounceEnabled); HashBytes(hash, settings.emissiveContribution);
    HashBytes(hash, settings.useMaterialTextures); HashBytes(hash, settings.includeStaticLocalLights);
    HashBytes(hash, settings.includeEmissive); HashBytes(hash, settings.indirectSaturation);
    HashBytes(hash, settings.energyThreshold);
    return hash;
}

bool BuildLightingProbes(const std::vector<LightingTriangle>& triangles,
                         const glm::vec3& skyColor, std::uint64_t sourceHash,
                         const std::string& sourceScene,
                         const LightingBuildSettings& input, LightingBuildData* output,
                         LightingBuildProgress* progress, std::string* error) {
    DirectionalSkyRadiance sky;
    const glm::vec3 safe = glm::max(skyColor, glm::vec3(0.0f));
    sky.zenith = safe;
    sky.horizon = safe * 0.75f;
    sky.ground = safe * 0.08f;
    return BuildLightingProbes(triangles, sky, sourceHash, sourceScene, input,
                               output, progress, error);
}

bool BuildLightingProbes(const std::vector<LightingTriangle>& triangles,
                         const DirectionalSkyRadiance& sky, std::uint64_t sourceHash,
                         const std::string& sourceScene,
                         const LightingBuildSettings& input, LightingBuildData* output,
                         LightingBuildProgress* progress, std::string* error) {
    return BuildLightingProbes(triangles, {}, sky, sourceHash, sourceScene, input,
                               output, progress, error);
}

bool BuildLightingProbes(const std::vector<LightingTriangle>& triangles,
                         const std::vector<LightingBuildLight>& staticLights,
                         const DirectionalSkyRadiance& sky, std::uint64_t sourceHash,
                         const std::string& sourceScene,
                         const LightingBuildSettings& input, LightingBuildData* output,
                         LightingBuildProgress* progress, std::string* error) {
    const auto buildStarted = std::chrono::steady_clock::now();
    if (!output) { if (error) *error = "Lighting build output is null."; return false; }
    if (triangles.empty()) { if (error) *error = "Lighting build found no static triangles."; return false; }
    LightingBuildSettings settings = input;
    settings.probeSpacing = std::max(settings.probeSpacing, 0.25f);
    settings.maxRayDistance = std::max(settings.maxRayDistance, settings.probeSpacing);
    settings.raysPerProbe = std::clamp(settings.raysPerProbe, 8u, 1024u);
    settings.diffuseBounces = std::clamp(settings.diffuseBounces, 1u, 4u);
    settings.indirectBounceStrength = glm::clamp(settings.indirectBounceStrength, 0.0f, 1.0f);
    settings.emissiveContribution = glm::clamp(settings.emissiveContribution, 0.0f, 8.0f);
    settings.indirectSaturation = glm::clamp(settings.indirectSaturation, 0.0f, 2.0f);
    settings.energyThreshold = glm::clamp(settings.energyThreshold, 0.0001f, 0.1f);
    if (settings.quality == LightingBuildQuality::Preview) {
        settings.raysPerProbe = std::min(settings.raysPerProbe, 32u);
        settings.diffuseBounces = 1u;
    }
    if (settings.quality == LightingBuildQuality::High) {
        settings.raysPerProbe = std::max(settings.raysPerProbe, 192u);
        settings.indirectBounceEnabled = true;
    }
    if (settings.quality == LightingBuildQuality::Production) {
        settings.raysPerProbe = std::max(settings.raysPerProbe, 384u);
        settings.indirectBounceEnabled = true;
    }

    glm::vec3 lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
    for (const auto& t : triangles) {
        lo = glm::min(lo, glm::min(t.a, glm::min(t.b, t.c)));
        hi = glm::max(hi, glm::max(t.a, glm::max(t.b, t.c)));
    }
    lo -= glm::vec3(settings.boundsPadding);
    hi += glm::vec3(settings.boundsPadding);
    hi.y = std::max(hi.y, lo.y + settings.probeSpacing * 2.0f);
    glm::ivec3 dimensions = glm::max(glm::ivec3(2), glm::ivec3(glm::ceil((hi - lo) / settings.probeSpacing)) + 1);
    constexpr std::size_t kMaxProbes = 262144;
    while (static_cast<std::size_t>(dimensions.x) * dimensions.y * dimensions.z > kMaxProbes) {
        settings.probeSpacing *= 1.25f;
        dimensions = glm::max(glm::ivec3(2), glm::ivec3(glm::ceil((hi - lo) / settings.probeSpacing)) + 1);
    }
    hi = lo + glm::vec3(dimensions - 1) * settings.probeSpacing;

    LightingBuildData built;
    built.sourceHash = sourceHash; built.sourceScene = sourceScene;
    built.boundsMin = lo; built.boundsMax = hi; built.dimensions = dimensions;
    built.spacing = settings.probeSpacing; built.settings = settings;
    const std::size_t count = static_cast<std::size_t>(dimensions.x) * dimensions.y * dimensions.z;
    built.probes.resize(count);
    if (progress) progress->phase = LightingBuildProgress::Phase::BuildingAcceleration;
    const LightingSceneBvh acceleration(triangles);
    if (progress) { progress->completed = 0; progress->total = static_cast<std::uint32_t>(count);
        progress->raysCast = 0; progress->currentBounce = 0; progress->pathSegments = 0;
        progress->shadowRays = 0; progress->materialTextureSamples = 0;
        progress->emissiveHits = 0;
        progress->phase = LightingBuildProgress::Phase::IrradianceProbes; }

    std::atomic<bool> cancelled{false};
    std::atomic<std::uint64_t> totalSegments{0}, totalMisses{0};
    std::atomic<std::uint64_t> totalEnergyTerminations{0}, totalMaxTerminations{0};
    std::atomic<std::uint64_t> totalShadowRays{0}, totalTextureSamples{0};
    std::atomic<std::uint64_t> totalEmissiveHits{0};
    auto buildRange = [&](std::size_t begin, std::size_t end) {
      for (std::size_t flat = begin; flat < end; ++flat) {
        if (progress && progress->cancel.load()) { cancelled = true; return; }
        const int x = static_cast<int>(flat % static_cast<std::size_t>(dimensions.x));
        const std::size_t yz = flat / static_cast<std::size_t>(dimensions.x);
        const int y = static_cast<int>(yz % static_cast<std::size_t>(dimensions.y));
        const int z = static_cast<int>(yz / static_cast<std::size_t>(dimensions.y));
        const glm::vec3 position = lo + glm::vec3(x, y, z) * settings.probeSpacing;
        // Cull finite emitters once for this probe. Secondary hit points remain
        // inside maxRayDistance of the probe, so range-expanded culling is safe
        // while avoiding a full-scene light loop for every path segment.
        std::vector<LightingBuildLight> relevantLights;
        if (settings.includeStaticLocalLights) {
            relevantLights.reserve(std::min<std::size_t>(staticLights.size(), 32u));
            std::vector<std::pair<float, std::size_t>> candidates;
            candidates.reserve(staticLights.size());
            for (std::size_t lightIndex = 0; lightIndex < staticLights.size(); ++lightIndex) {
                const LightingBuildLight& light = staticLights[lightIndex];
                if (light.type == LightingBuildLight::Type::Directional) continue;
                const float distanceSquared = glm::dot(light.position - position,
                                                       light.position - position);
                const float reach = settings.maxRayDistance + std::max(light.range, 0.0f);
                if (distanceSquared <= reach * reach) candidates.emplace_back(distanceSquared, lightIndex);
            }
            const std::size_t keep = std::min<std::size_t>(candidates.size(), 32u);
            std::partial_sort(candidates.begin(), candidates.begin() + keep, candidates.end());
            for (std::size_t candidate = 0; candidate < keep; ++candidate)
                relevantLights.push_back(staticLights[candidates[candidate].second]);
        }
        std::uint32_t visible = 0, skySamples = 0;
        std::array<glm::vec3, 4> projected{};
        std::array<glm::vec3, 4> bounceProjected{};
        std::array<glm::vec3, 4> higherBounceProjected{};
        std::array<glm::vec3, 4> emissiveProjected{};
        PathCounters counters;
        std::uint32_t nearBlocked = 0;
        static constexpr glm::vec3 axes[6] = {
            {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (const glm::vec3& axis : axes)
            if (acceleration.Occluded(position, axis, settings.probeSpacing * 0.2f))
                ++nearBlocked;
        for (std::uint32_t ray = 0; ray < settings.raysPerProbe; ++ray) {
            const glm::vec3 direction = SphereDirection(ray, settings.raysPerProbe);
            if (direction.y > 0.0f) ++skySamples;
            const auto basis = EvaluateSH4Basis(direction);
            glm::vec3 pathOrigin = position;
            glm::vec3 pathDirection = direction;
            glm::vec3 throughput(1.0f);
            glm::vec3 radiance(0.0f), firstBounce(0.0f), higherBounce(0.0f);
            glm::vec3 emissiveRadiance(0.0f);
            std::uint32_t surfaces = 0;
            for (std::uint32_t bounce = 0; bounce < settings.diffuseBounces; ++bounce) {
                if (progress && progress->cancel.load()) { cancelled = true; break; }
                LightingRayHit hit;
                ++counters.segments;
                if (progress) {
                    ++progress->raysCast;
                    std::uint32_t observed = progress->currentBounce.load();
                    while (observed < bounce + 1u
                           && !progress->currentBounce.compare_exchange_weak(observed, bounce + 1u)) {}
                }
                if (!acceleration.Trace(pathOrigin, pathDirection,
                                        settings.maxRayDistance, &hit)) {
                    if (surfaces == 0 && direction.y > 0.0f) ++visible;
                    const glm::vec3 miss = throughput * sky.Sample(pathDirection);
                    radiance += miss;
                    if (surfaces == 1) firstBounce += miss;
                    else if (surfaces > 1) higherBounce += miss;
                    ++counters.misses;
                    break;
                }
                ++surfaces;
                if (settings.includeEmissive && settings.emissiveContribution > 0.0f) {
                    const glm::vec3 emitted = throughput
                        * glm::max(hit.emissive, glm::vec3(0.0f))
                        * settings.emissiveContribution;
                    if (glm::dot(emitted, emitted) > 0.0f) {
                        radiance += emitted; emissiveRadiance += emitted;
                        ++counters.emissiveHits;
                    }
                }
                if (!settings.indirectBounceEnabled
                    || settings.indirectBounceStrength <= 0.0f) break;
                glm::vec3 incoming = EvaluateSun(
                    sky, acceleration, hit, settings.maxRayDistance, &counters);
                if (settings.includeStaticLocalLights)
                    incoming += EvaluateBuildLights(relevantLights, acceleration, hit,
                                                     settings.maxRayDistance, &counters);
                glm::vec3 diffuseColor = SampleBaseColor(
                    hit, settings.useMaterialTextures, &counters)
                    * (1.0f - glm::clamp(hit.metallic, 0.0f, 1.0f));
                diffuseColor = glm::clamp(
                    AdjustSaturation(diffuseColor, settings.indirectSaturation),
                    glm::vec3(0.0f), glm::vec3(1.0f));
                const glm::vec3 response = diffuseColor * settings.indirectBounceStrength;
                const glm::vec3 reflected = throughput * response * incoming / kPi;
                radiance += reflected;
                if (surfaces == 1) firstBounce += reflected; else higherBounce += reflected;
                // Continuation directions use pdf = cos(theta) / PI. The
                // Lambert BRDF (albedo / PI) * cos(theta) therefore divides by
                // that PDF to exactly `albedo`; no second cosine or PI belongs
                // in the path throughput update below.
                throughput *= response;
                if (std::max({throughput.r, throughput.g, throughput.b})
                    < settings.energyThreshold) {
                    ++counters.energyTerminations; break;
                }
                const glm::vec3 nextOrigin = hit.position + hit.geometricNormal * RayBias(hit);
                const glm::vec3 nextDirection = CosineHemisphere(hit.normal, ray,
                    settings.raysPerProbe,
                    static_cast<std::uint32_t>(flat * 9781u + bounce * 6271u));
                if (bounce + 1u == settings.diffuseBounces) {
                    // Evaluate the environment at the terminal continuation so
                    // a one-bounce build still receives hemispherical skylight.
                    ++counters.segments;
                    if (progress) ++progress->raysCast;
                    LightingRayHit terminalHit;
                    if (!acceleration.Trace(nextOrigin, nextDirection,
                                            settings.maxRayDistance, &terminalHit)) {
                        const glm::vec3 terminal = throughput * sky.Sample(nextDirection);
                        radiance += terminal;
                        if (surfaces == 1) firstBounce += terminal;
                        else higherBounce += terminal;
                        ++counters.misses;
                    } else if (settings.includeEmissive) {
                        const glm::vec3 terminalEmission = throughput
                            * glm::max(terminalHit.emissive, glm::vec3(0.0f))
                            * settings.emissiveContribution;
                        radiance += terminalEmission; emissiveRadiance += terminalEmission;
                        if (glm::dot(terminalEmission, terminalEmission) > 0.0f)
                            ++counters.emissiveHits;
                    }
                    ++counters.maxBounceTerminations; break;
                }
                pathOrigin = nextOrigin;
                pathDirection = nextDirection;
            }
            for (std::size_t coefficient = 0; coefficient < projected.size(); ++coefficient) {
                projected[coefficient] += radiance * basis[coefficient];
                bounceProjected[coefficient] += firstBounce * basis[coefficient];
                higherBounceProjected[coefficient] += higherBounce * basis[coefficient];
                emissiveProjected[coefficient] += emissiveRadiance * basis[coefficient];
            }
        }
        LightingProbe& probe = built.probes[built.Index(x, y, z)];
        probe.skyVisibility = std::clamp(static_cast<float>(visible) / std::max(skySamples, 1u),
                                         settings.minimumVisibility, 1.0f);
        const float projectionWeight = 4.0f * kPi / static_cast<float>(settings.raysPerProbe);
        probe.irradianceSH[0] = projected[0] * projectionWeight * kPi;
        for (std::size_t coefficient = 1; coefficient < probe.irradianceSH.size(); ++coefficient)
            probe.irradianceSH[coefficient] = projected[coefficient] * projectionWeight * (2.0f * kPi / 3.0f);
        for (std::size_t coefficient = 0; coefficient < 4; ++coefficient) {
            const float convolution = coefficient == 0 ? kPi : (2.0f * kPi / 3.0f);
            probe.bounceSH[coefficient] = bounceProjected[coefficient] * projectionWeight * convolution;
            probe.higherBounceSH[coefficient] = higherBounceProjected[coefficient]
                * projectionWeight * convolution;
            probe.emissiveSH[coefficient] = emissiveProjected[coefficient] * projectionWeight * convolution;
        }
        const auto up = EvaluateSH4Basis(glm::vec3(0, 1, 0));
        probe.ambient = glm::max(probe.irradianceSH[0] * up[0]
            + probe.irradianceSH[1] * up[1] + probe.irradianceSH[2] * up[2]
            + probe.irradianceSH[3] * up[3], glm::vec3(0.0f));
        probe.valid = nearBlocked < 6;
        if (!probe.valid) {
            probe.ambient = glm::vec3(0.0f);
            for (glm::vec3& coefficient : probe.irradianceSH) coefficient = glm::vec3(0.0f);
        }
        totalSegments += counters.segments; totalMisses += counters.misses;
        totalEnergyTerminations += counters.energyTerminations;
        totalMaxTerminations += counters.maxBounceTerminations;
        totalShadowRays += counters.shadowRays; totalTextureSamples += counters.textureSamples;
        totalEmissiveHits += counters.emissiveHits;
        if (progress) {
            progress->pathSegments += counters.segments;
            progress->shadowRays += counters.shadowRays;
            progress->materialTextureSamples += counters.textureSamples;
            progress->emissiveHits += counters.emissiveHits;
            ++progress->completed;
        }
      }
    };
    const std::size_t hardwareThreads = std::max<std::size_t>(1, std::thread::hardware_concurrency());
    const std::size_t workerCount = std::min<std::size_t>(hardwareThreads, std::max<std::size_t>(1, count / 32));
    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    for (std::size_t worker = 0; worker < workerCount; ++worker) {
        const std::size_t begin = count * worker / workerCount;
        const std::size_t end = count * (worker + 1) / workerCount;
        workers.emplace_back(buildRange, begin, end);
    }
    for (auto& worker : workers) worker.join();
    if (cancelled.load()) { if (error) *error = "Lighting build cancelled."; return false; }
    built.stats.pathSegments = totalSegments.load();
    built.stats.raysTerminatedByMiss = totalMisses.load();
    built.stats.raysTerminatedByEnergy = totalEnergyTerminations.load();
    built.stats.raysTerminatedByMaxBounce = totalMaxTerminations.load();
    built.stats.shadowRays = totalShadowRays.load();
    built.stats.materialTextureSamples = totalTextureSamples.load();
    built.stats.emissiveHits = totalEmissiveHits.load();
    built.stats.averagePathLength = count == 0 || settings.raysPerProbe == 0
        ? 0.0f : static_cast<float>(built.stats.pathSegments)
            / static_cast<float>(count * settings.raysPerProbe);
    built.stats.buildMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - buildStarted).count();
    if (progress) {
        progress->pathSegments = built.stats.pathSegments;
        progress->shadowRays = built.stats.shadowRays;
        progress->materialTextureSamples = built.stats.materialTextureSamples;
        progress->emissiveHits = built.stats.emissiveHits;
    }
    if (progress) progress->phase = LightingBuildProgress::Phase::Complete;
    *output = std::move(built);
    return true;
}

bool SaveLightingBuildData(const std::string& path, const LightingBuildData& data, std::string* error) {
    if (!data.IsValid()) { if (error) *error = "Cannot save invalid lighting build data."; return false; }
    std::error_code ec; std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    const std::string temporary = path + ".tmp";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) { if (error) *error = "Could not open lighting asset for writing: " + temporary; return false; }
    out.write(kMagic, sizeof(kMagic));
    const std::uint32_t sceneLength = static_cast<std::uint32_t>(data.sourceScene.size());
    const std::uint64_t probeCount = data.probes.size();
    Write(out, data.version); Write(out, data.sourceHash); Write(out, sceneLength);
    out.write(data.sourceScene.data(), sceneLength);
    Write(out, data.boundsMin); Write(out, data.boundsMax); Write(out, data.dimensions); Write(out, data.spacing);
    Write(out, data.settings.quality); Write(out, data.settings.probeSpacing); Write(out, data.settings.maxRayDistance);
    Write(out, data.settings.boundsPadding); Write(out, data.settings.minimumVisibility); Write(out, data.settings.raysPerProbe);
    Write(out, data.settings.diffuseBounces);
    const std::uint8_t directional = data.settings.directionalIrradiance ? 1 : 0;
    Write(out, directional); Write(out, data.settings.indirectBounceStrength);
    const std::uint8_t bounceEnabled = data.settings.indirectBounceEnabled ? 1 : 0;
    Write(out, bounceEnabled); Write(out, data.settings.emissiveContribution);
    Write(out, data.settings.indirectSaturation);
    const std::uint8_t useTextures = data.settings.useMaterialTextures ? 1 : 0;
    const std::uint8_t localLights = data.settings.includeStaticLocalLights ? 1 : 0;
    const std::uint8_t includeEmissive = data.settings.includeEmissive ? 1 : 0;
    Write(out, useTextures); Write(out, localLights); Write(out, includeEmissive);
    Write(out, data.settings.energyThreshold);
    Write(out, data.stats.buildMilliseconds); Write(out, data.stats.pathSegments);
    Write(out, data.stats.raysTerminatedByMiss); Write(out, data.stats.raysTerminatedByEnergy);
    Write(out, data.stats.raysTerminatedByMaxBounce); Write(out, data.stats.shadowRays);
    Write(out, data.stats.materialTextureSamples); Write(out, data.stats.emissiveHits);
    Write(out, data.stats.averagePathLength);
    Write(out, probeCount);
    for (const auto& p : data.probes) { Write(out, p.ambient); for(const auto& sh:p.irradianceSH)Write(out,sh);
        for(const auto& sh:p.bounceSH)Write(out,sh); for(const auto& sh:p.higherBounceSH)Write(out,sh);
        for(const auto& sh:p.emissiveSH)Write(out,sh);
        Write(out, p.skyVisibility); const std::uint8_t valid=p.valid?1:0; Write(out,valid); }
    out.flush(); out.close();
    if (!out) { std::filesystem::remove(temporary, ec); if (error) *error = "Failed while writing lighting asset."; return false; }
    const std::string backup=path+".bak";std::filesystem::remove(backup,ec);ec.clear();
    const bool hadPrevious=std::filesystem::exists(path,ec);ec.clear();
    if(hadPrevious){std::filesystem::rename(path,backup,ec);if(ec){std::filesystem::remove(temporary,ec);if(error)*error="Could not preserve the previous lighting asset.";return false;}}
    std::filesystem::rename(temporary,path,ec);
    if(ec){const std::string message=ec.message();ec.clear();if(hadPrevious)std::filesystem::rename(backup,path,ec);if(error)*error="Could not commit lighting asset: "+message;return false;}
    if(hadPrevious)std::filesystem::remove(backup,ec);
    return true;
}

bool LoadLightingBuildData(const std::string& path, LightingBuildData* data, std::string* error) {
    if (!data) { if (error) *error = "Lighting load output is null."; return false; }
    std::ifstream in(path, std::ios::binary); if (!in) { if (error) *error = "Could not open lighting asset: " + path; return false; }
    char magic[8]{}; in.read(magic, sizeof(magic));
    if (std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) { if (error) *error = "Lighting build data is outdated and requires rebuild."; return false; }
    LightingBuildData loaded; std::uint32_t sceneLength=0; std::uint64_t probeCount=0;
    if (!Read(in, loaded.version) || loaded.version != LightingBuildData::kVersion || !Read(in,loaded.sourceHash) || !Read(in,sceneLength) || sceneLength>1024*1024) { if(error)*error="Lighting build data is outdated and requires rebuild."; return false; }
    loaded.sourceScene.resize(sceneLength); in.read(loaded.sourceScene.data(), sceneLength);
    if (!Read(in,loaded.boundsMin)||!Read(in,loaded.boundsMax)||!Read(in,loaded.dimensions)||!Read(in,loaded.spacing)
        ||!Read(in,loaded.settings.quality)||!Read(in,loaded.settings.probeSpacing)||!Read(in,loaded.settings.maxRayDistance)
        ||!Read(in,loaded.settings.boundsPadding)||!Read(in,loaded.settings.minimumVisibility)||!Read(in,loaded.settings.raysPerProbe)
        ||!Read(in,loaded.settings.diffuseBounces)) { if(error)*error="Truncated or unsafe lighting asset."; return false; }
    std::uint8_t directional=0;
    std::uint8_t bounceEnabled=0;
    std::uint8_t useTextures=0, localLights=0, includeEmissive=0;
    if(!Read(in,directional)||!Read(in,loaded.settings.indirectBounceStrength)
        ||!Read(in,bounceEnabled)||!Read(in,loaded.settings.emissiveContribution)
        ||!Read(in,loaded.settings.indirectSaturation)
        ||!Read(in,useTextures)||!Read(in,localLights)||!Read(in,includeEmissive)
        ||!Read(in,loaded.settings.energyThreshold)
        ||!Read(in,loaded.stats.buildMilliseconds)||!Read(in,loaded.stats.pathSegments)
        ||!Read(in,loaded.stats.raysTerminatedByMiss)||!Read(in,loaded.stats.raysTerminatedByEnergy)
        ||!Read(in,loaded.stats.raysTerminatedByMaxBounce)||!Read(in,loaded.stats.shadowRays)
        ||!Read(in,loaded.stats.materialTextureSamples)||!Read(in,loaded.stats.emissiveHits)
        ||!Read(in,loaded.stats.averagePathLength)
        ||!Read(in,probeCount) || probeCount>262144) { if(error)*error="Truncated or unsafe lighting asset."; return false; }
    loaded.settings.directionalIrradiance=directional!=0;
    loaded.settings.indirectBounceEnabled=bounceEnabled!=0;
    loaded.settings.useMaterialTextures=useTextures!=0;
    loaded.settings.includeStaticLocalLights=localLights!=0;
    loaded.settings.includeEmissive=includeEmissive!=0;
    loaded.probes.resize(static_cast<std::size_t>(probeCount));
    for(auto& p:loaded.probes){std::uint8_t valid=0;if(!Read(in,p.ambient)){if(error)*error="Truncated lighting probes.";return false;}
        for(auto& sh:p.irradianceSH)if(!Read(in,sh)){if(error)*error="Truncated lighting probes.";return false;}
        for(auto& sh:p.bounceSH)if(!Read(in,sh)){if(error)*error="Truncated lighting probes.";return false;}
        for(auto& sh:p.higherBounceSH)if(!Read(in,sh)){if(error)*error="Truncated lighting probes.";return false;}
        for(auto& sh:p.emissiveSH)if(!Read(in,sh)){if(error)*error="Truncated lighting probes.";return false;}
        if(!Read(in,p.skyVisibility)||!Read(in,valid)){if(error)*error="Truncated lighting probes.";return false;}p.valid=valid!=0;}
    if(!loaded.IsValid()){if(error)*error="Lighting asset grid is inconsistent.";return false;}
    *data=std::move(loaded); return true;
}

LightingProbeGrid::~LightingProbeGrid(){ Reset(); }
LightingProbeGrid::LightingProbeGrid(LightingProbeGrid&& o) noexcept { *this=std::move(o); }
LightingProbeGrid& LightingProbeGrid::operator=(LightingProbeGrid&& o) noexcept { if(this!=&o){Reset();m_textures=o.m_textures;o.m_textures.fill(0);m_boundsMin=o.m_boundsMin;m_boundsMax=o.m_boundsMax;m_dimensions=o.m_dimensions;m_probeCount=o.m_probeCount;m_memoryBytes=o.m_memoryBytes;o.m_dimensions=glm::ivec3(0);o.m_probeCount=0;o.m_memoryBytes=0;}return *this; }
void LightingProbeGrid::Reset(){
    // The CPU-side probe builder and its tests are valid before an OpenGL
    // context exists.  GLAD function pointers are null in that phase.
    if (glad_glDeleteTextures) {
        glDeleteTextures(static_cast<GLsizei>(m_textures.size()), m_textures.data());
    }
    m_textures.fill(0);
    m_dimensions=glm::ivec3(0);
    m_probeCount=0;
    m_memoryBytes=0;
}
bool LightingProbeGrid::Upload(const LightingBuildData& data,std::string* error){
    if(!data.IsValid()){if(error)*error="Cannot upload invalid lighting grid.";return false;}
    std::array<std::vector<float>,16> packed;for(auto& texture:packed)texture.resize(data.probes.size()*4,0.0f);
    auto packSh=[&](std::size_t base,std::size_t sample,const std::array<glm::vec3,4>& sh,float validity){const std::size_t b=sample*4;
        packed[base][b]=sh[0].r*validity;packed[base][b+1]=sh[0].g*validity;packed[base][b+2]=sh[0].b*validity;packed[base][b+3]=sh[1].r*validity;
        packed[base+1][b]=sh[1].g*validity;packed[base+1][b+1]=sh[1].b*validity;packed[base+1][b+2]=sh[2].r*validity;packed[base+1][b+3]=sh[2].g*validity;
        packed[base+2][b]=sh[2].b*validity;packed[base+2][b+1]=sh[3].r*validity;packed[base+2][b+2]=sh[3].g*validity;packed[base+2][b+3]=sh[3].b*validity;};
    for(std::size_t i=0;i<data.probes.size();++i){const auto&p=data.probes[i];const float v=p.valid?1.0f:0.0f;const std::size_t b=i*4;
        packSh(0,i,p.irradianceSH,v);packSh(4,i,p.bounceSH,v);
        packSh(7,i,p.higherBounceSH,v);packSh(10,i,p.emissiveSH,v);
        std::array<glm::vec3,4> direct{};for(std::size_t coefficient=0;coefficient<4;++coefficient)
            direct[coefficient]=p.irradianceSH[coefficient]-p.bounceSH[coefficient]
                -p.higherBounceSH[coefficient]-p.emissiveSH[coefficient];
        packSh(13,i,direct,v);packed[3][b]=p.skyVisibility*v;packed[3][b+1]=v;
        packed[3][b+2]=p.depthMean*v;packed[3][b+3]=p.depthSecondMoment*v;}
    Reset();glGenTextures(static_cast<GLsizei>(m_textures.size()),m_textures.data());
    for(std::size_t index=0;index<m_textures.size();++index){glBindTexture(GL_TEXTURE_3D,m_textures[index]);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);glTexImage3D(GL_TEXTURE_3D,0,GL_RGBA16F,data.dimensions.x,data.dimensions.y,data.dimensions.z,0,GL_RGBA,GL_FLOAT,packed[index].data());}
    const GLenum status=glGetError();glBindTexture(GL_TEXTURE_3D,0);if(status!=GL_NO_ERROR){Reset();if(error)*error="OpenGL could not create the SH lighting probe textures.";return false;}
    m_boundsMin=data.boundsMin;m_boundsMax=data.boundsMax;m_dimensions=data.dimensions;m_probeCount=data.probes.size();
    // Sixteen RGBA16F volumes: combined SH (3), metadata (1), first bounce
    // (3), higher bounces (3), emissive (3), direct/environment (3).
    m_memoryBytes=static_cast<std::uint64_t>(m_probeCount)*128ull;return true;
}
bool LightingProbeGrid::UpdateCombined(const LightingBuildData& data,
                                       const std::vector<std::size_t>& changedIndices,
                                       std::string* error){
    if(!Valid()||!data.IsValid()||data.dimensions!=m_dimensions||data.probes.size()!=m_probeCount){
        if(error)*error="Dynamic lighting update does not match the allocated probe grid.";return false;}
    std::vector<std::size_t> indices=changedIndices;
    indices.erase(std::remove_if(indices.begin(),indices.end(),[&](std::size_t i){return i>=m_probeCount;}),indices.end());
    std::sort(indices.begin(),indices.end());indices.erase(std::unique(indices.begin(),indices.end()),indices.end());
    auto pack=[&](const LightingProbe&p,std::array<std::array<float,4>,4>& texels){
        const float v=p.valid?1.0f:0.0f;const auto&sh=p.irradianceSH;
        texels[0]={sh[0].r*v,sh[0].g*v,sh[0].b*v,sh[1].r*v};
        texels[1]={sh[1].g*v,sh[1].b*v,sh[2].r*v,sh[2].g*v};
        texels[2]={sh[2].b*v,sh[3].r*v,sh[3].g*v,sh[3].b*v};
        texels[3]={p.skyVisibility*v,v,p.depthMean*v,p.depthSecondMoment*v};};
    for(std::size_t flat:indices){const int x=static_cast<int>(flat%static_cast<std::size_t>(m_dimensions.x));
        const std::size_t yz=flat/static_cast<std::size_t>(m_dimensions.x);const int y=static_cast<int>(yz%static_cast<std::size_t>(m_dimensions.y));const int z=static_cast<int>(yz/static_cast<std::size_t>(m_dimensions.y));
        std::array<std::array<float,4>,4> texels{};pack(data.probes[flat],texels);
        for(std::size_t texture=0;texture<4;++texture){glBindTexture(GL_TEXTURE_3D,m_textures[texture]);
            glTexSubImage3D(GL_TEXTURE_3D,0,x,y,z,1,1,1,GL_RGBA,GL_FLOAT,texels[texture].data());}}
    const GLenum status=glGetError();glBindTexture(GL_TEXTURE_3D,0);if(status!=GL_NO_ERROR){if(error)*error="OpenGL rejected a partial dynamic probe upload.";return false;}return true;
}
void LightingProbeGrid::Bind(unsigned int unit,Contribution contribution)const{
    std::size_t base=0;if(contribution==Contribution::Bounce)base=4;
    else if(contribution==Contribution::HigherBounce)base=7;
    else if(contribution==Contribution::Emissive)base=10;
    else if(contribution==Contribution::DirectEnvironment)base=13;
    for(std::size_t i=0;i<3;++i){glActiveTexture(GL_TEXTURE0+unit+static_cast<unsigned int>(i));glBindTexture(GL_TEXTURE_3D,m_textures[base+i]);}
    glActiveTexture(GL_TEXTURE0+unit+3);glBindTexture(GL_TEXTURE_3D,m_textures[3]);
}

} // namespace engine
