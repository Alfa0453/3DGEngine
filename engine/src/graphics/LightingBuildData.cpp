#include "engine/graphics/LightingBuildData.h"
#include "engine/graphics/LightingScene.h"

#include <glad/glad.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

namespace engine {
namespace {

constexpr char kMagic[8] = {'3','D','G','L','I','T','E','3'};
constexpr float kPi = 3.14159265358979323846f;

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

glm::vec3 AdjustSaturation(glm::vec3 color, float saturation) {
    const float luminance = glm::dot(color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
    return glm::max(glm::mix(glm::vec3(luminance), color,
                            glm::clamp(saturation, 0.0f, 2.0f)), glm::vec3(0.0f));
}

glm::vec3 EvaluateBuildLights(const std::vector<LightingBuildLight>& lights,
                              const LightingSceneBvh& acceleration,
                              const LightingRayHit& hit, float maximumDistance) {
    glm::vec3 incoming(0.0f);
    for (const LightingBuildLight& light : lights) {
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
                if(nDotL<=0.0f||lightCos<=0.0f||acceleration.Occluded(hit.position+hit.normal*0.025f,L,distance-0.025f))continue;
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
            L = delta / distance; shadowDistance = distance - 0.025f;
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
        if (acceleration.Occluded(hit.position + hit.normal * 0.025f, L, shadowDistance)) continue;
        incoming += glm::max(light.color, glm::vec3(0.0f)) * attenuation * nDotL;
    }
    return incoming;
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
    for (const auto& t : triangles) { HashBytes(hash, t.a); HashBytes(hash, t.b); HashBytes(hash, t.c); HashBytes(hash,t.albedo); HashBytes(hash,t.emissive); }
    HashBytes(hash, settings.quality); HashBytes(hash, settings.probeSpacing);
    HashBytes(hash, settings.maxRayDistance); HashBytes(hash, settings.boundsPadding);
    HashBytes(hash, settings.minimumVisibility); HashBytes(hash, settings.raysPerProbe);
    HashBytes(hash, settings.directionalIrradiance); HashBytes(hash, settings.indirectBounceStrength);
    HashBytes(hash, settings.indirectBounceEnabled); HashBytes(hash, settings.emissiveContribution);
    HashBytes(hash, settings.indirectSaturation);
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
    if (!output) { if (error) *error = "Lighting build output is null."; return false; }
    if (triangles.empty()) { if (error) *error = "Lighting build found no static triangles."; return false; }
    LightingBuildSettings settings = input;
    settings.probeSpacing = std::max(settings.probeSpacing, 0.25f);
    settings.maxRayDistance = std::max(settings.maxRayDistance, settings.probeSpacing);
    settings.raysPerProbe = std::clamp(settings.raysPerProbe, 8u, 1024u);
    settings.indirectBounceStrength = glm::clamp(settings.indirectBounceStrength, 0.0f, 1.0f);
    settings.emissiveContribution = glm::clamp(settings.emissiveContribution, 0.0f, 8.0f);
    settings.indirectSaturation = glm::clamp(settings.indirectSaturation, 0.0f, 2.0f);
    if (settings.quality == LightingBuildQuality::Preview) {
        settings.raysPerProbe = std::min(settings.raysPerProbe, 32u);
        settings.indirectBounceEnabled = false;
        settings.emissiveContribution = 0.0f;
    }
    if (settings.quality == LightingBuildQuality::High) {
        settings.raysPerProbe = std::max(settings.raysPerProbe, 192u);
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
        progress->raysCast = 0; progress->phase = LightingBuildProgress::Phase::IrradianceProbes; }

    std::atomic<bool> cancelled{false};
    auto buildRange = [&](std::size_t begin, std::size_t end) {
      for (std::size_t flat = begin; flat < end; ++flat) {
        if (progress && progress->cancel.load()) { cancelled = true; return; }
        const int x = static_cast<int>(flat % static_cast<std::size_t>(dimensions.x));
        const std::size_t yz = flat / static_cast<std::size_t>(dimensions.x);
        const int y = static_cast<int>(yz % static_cast<std::size_t>(dimensions.y));
        const int z = static_cast<int>(yz / static_cast<std::size_t>(dimensions.y));
        const glm::vec3 position = lo + glm::vec3(x, y, z) * settings.probeSpacing;
        std::uint32_t visible = 0, skySamples = 0;
        std::array<glm::vec3, 4> projected{{glm::vec3(0.0f), glm::vec3(0.0f),
                                            glm::vec3(0.0f), glm::vec3(0.0f)}};
        std::array<glm::vec3, 4> bounceProjected{};
        std::array<glm::vec3, 4> emissiveProjected{};
        std::uint32_t nearBlocked = 0;
        static constexpr glm::vec3 axes[6] = {
            {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (const glm::vec3& axis : axes)
            if (acceleration.Occluded(position, axis, settings.probeSpacing * 0.2f))
                ++nearBlocked;
        for (std::uint32_t ray = 0; ray < settings.raysPerProbe; ++ray) {
            const glm::vec3 direction = SphereDirection(ray, settings.raysPerProbe);
            if (direction.y > 0.0f) ++skySamples;
            LightingRayHit hit;
            const bool blocked = acceleration.Trace(position, direction, settings.maxRayDistance, &hit);
            if (progress) ++progress->raysCast;
            if (!blocked) {
                if (direction.y > 0.0f) ++visible;
                const auto basis = EvaluateSH4Basis(direction);
                const glm::vec3 radiance = sky.Sample(direction);
                for (std::size_t coefficient = 0; coefficient < projected.size(); ++coefficient)
                    projected[coefficient] += radiance * basis[coefficient];
            } else if ((settings.indirectBounceEnabled && settings.indirectBounceStrength > 0.0f)
                       || settings.emissiveContribution > 0.0f) {
                const auto basis = EvaluateSH4Basis(direction);
                glm::vec3 bounced(0.0f);
                if(settings.indirectBounceEnabled&&settings.indirectBounceStrength>0.0f){
                    const bool skyOpen=!acceleration.Occluded(hit.position+hit.normal*0.025f,
                        hit.normal,settings.maxRayDistance);
                    const glm::vec3 environment=skyOpen?sky.Sample(hit.normal)*0.5f:glm::vec3(0.0f);
                    const glm::vec3 direct=EvaluateBuildLights(staticLights,acceleration,hit,
                        settings.maxRayDistance);
                    bounced=AdjustSaturation(hit.albedo*(environment+direct)/kPi,
                        settings.indirectSaturation)*settings.indirectBounceStrength;
                }
                const glm::vec3 emissive = glm::max(hit.emissive, glm::vec3(0.0f))
                                         * settings.emissiveContribution;
                for (std::size_t coefficient = 0; coefficient < projected.size(); ++coefficient) {
                    bounceProjected[coefficient] += bounced * basis[coefficient];
                    emissiveProjected[coefficient] += emissive * basis[coefficient];
                    projected[coefficient] += (bounced + emissive) * basis[coefficient];
                }
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
            probe.emissiveSH[coefficient] = emissiveProjected[coefficient] * projectionWeight * convolution;
        }
        const auto up = EvaluateSH4Basis(glm::vec3(0, 1, 0));
        probe.ambient = glm::max(probe.irradianceSH[0] * up[0]
            + probe.irradianceSH[1] * up[1]
            + probe.irradianceSH[2] * up[2]
            + probe.irradianceSH[3] * up[3], glm::vec3(0.0f));
        probe.valid = nearBlocked < 6;
        if (!probe.valid) {
            probe.ambient = glm::vec3(0.0f);
            for (glm::vec3& coefficient : probe.irradianceSH)
                coefficient = glm::vec3(0.0f);
        }
        if (progress) ++progress->completed;
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
    const std::uint8_t directional = data.settings.directionalIrradiance ? 1 : 0;
    Write(out, directional); Write(out, data.settings.indirectBounceStrength);
    const std::uint8_t bounceEnabled = data.settings.indirectBounceEnabled ? 1 : 0;
    Write(out, bounceEnabled); Write(out, data.settings.emissiveContribution);
    Write(out, data.settings.indirectSaturation);
    Write(out, probeCount);
    for (const auto& p : data.probes) { Write(out, p.ambient); for(const auto& sh:p.irradianceSH)Write(out,sh);
        for(const auto& sh:p.bounceSH)Write(out,sh); for(const auto& sh:p.emissiveSH)Write(out,sh);
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
        ) { if(error)*error="Truncated or unsafe lighting asset."; return false; }
    std::uint8_t directional=0;
    std::uint8_t bounceEnabled=0;
    if(!Read(in,directional)||!Read(in,loaded.settings.indirectBounceStrength)
        ||!Read(in,bounceEnabled)||!Read(in,loaded.settings.emissiveContribution)
        ||!Read(in,loaded.settings.indirectSaturation)
        ||!Read(in,probeCount) || probeCount>262144) { if(error)*error="Truncated or unsafe lighting asset."; return false; }
    loaded.settings.directionalIrradiance=directional!=0;
    loaded.settings.indirectBounceEnabled=bounceEnabled!=0;
    loaded.probes.resize(static_cast<std::size_t>(probeCount));
    for(auto& p:loaded.probes){std::uint8_t valid=0;if(!Read(in,p.ambient)){if(error)*error="Truncated lighting probes.";return false;}
        for(auto& sh:p.irradianceSH)if(!Read(in,sh)){if(error)*error="Truncated lighting probes.";return false;}
        for(auto& sh:p.bounceSH)if(!Read(in,sh)){if(error)*error="Truncated lighting probes.";return false;}
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
    std::array<std::vector<float>,13> packed;for(auto& texture:packed)texture.resize(data.probes.size()*4,0.0f);
    auto packSh=[&](std::size_t base,std::size_t sample,const std::array<glm::vec3,4>& sh,float validity){const std::size_t b=sample*4;
        packed[base][b]=sh[0].r*validity;packed[base][b+1]=sh[0].g*validity;packed[base][b+2]=sh[0].b*validity;packed[base][b+3]=sh[1].r*validity;
        packed[base+1][b]=sh[1].g*validity;packed[base+1][b+1]=sh[1].b*validity;packed[base+1][b+2]=sh[2].r*validity;packed[base+1][b+3]=sh[2].g*validity;
        packed[base+2][b]=sh[2].b*validity;packed[base+2][b+1]=sh[3].r*validity;packed[base+2][b+2]=sh[3].g*validity;packed[base+2][b+3]=sh[3].b*validity;};
    for(std::size_t i=0;i<data.probes.size();++i){const auto&p=data.probes[i];const float v=p.valid?1.0f:0.0f;const std::size_t b=i*4;
        packSh(0,i,p.irradianceSH,v);packSh(4,i,p.bounceSH,v);packSh(7,i,p.emissiveSH,v);
        std::array<glm::vec3,4> direct{};for(std::size_t coefficient=0;coefficient<4;++coefficient)
            direct[coefficient]=glm::max(p.irradianceSH[coefficient]-p.bounceSH[coefficient]-p.emissiveSH[coefficient],glm::vec3(0.0f));
        packSh(10,i,direct,v);packed[3][b]=p.skyVisibility*v;packed[3][b+1]=v;
        packed[3][b+2]=p.depthMean*v;packed[3][b+3]=p.depthSecondMoment*v;}
    Reset();glGenTextures(static_cast<GLsizei>(m_textures.size()),m_textures.data());
    for(std::size_t index=0;index<m_textures.size();++index){glBindTexture(GL_TEXTURE_3D,m_textures[index]);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);glTexImage3D(GL_TEXTURE_3D,0,GL_RGBA16F,data.dimensions.x,data.dimensions.y,data.dimensions.z,0,GL_RGBA,GL_FLOAT,packed[index].data());}
    const GLenum status=glGetError();glBindTexture(GL_TEXTURE_3D,0);if(status!=GL_NO_ERROR){Reset();if(error)*error="OpenGL could not create the SH lighting probe textures.";return false;}
    m_boundsMin=data.boundsMin;m_boundsMax=data.boundsMax;m_dimensions=data.dimensions;m_probeCount=data.probes.size();
    // Thirteen RGBA16F volumes: combined SH (3), metadata (1), bounce SH
    // (3), emissive SH (3), and direct-environment SH (3) = 104 bytes/probe.
    m_memoryBytes=static_cast<std::uint64_t>(m_probeCount)*104ull;return true;
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
    else if(contribution==Contribution::Emissive)base=7;
    else if(contribution==Contribution::DirectEnvironment)base=10;
    for(std::size_t i=0;i<3;++i){glActiveTexture(GL_TEXTURE0+unit+static_cast<unsigned int>(i));glBindTexture(GL_TEXTURE_3D,m_textures[base+i]);}
    glActiveTexture(GL_TEXTURE0+unit+3);glBindTexture(GL_TEXTURE_3D,m_textures[3]);
}

} // namespace engine
