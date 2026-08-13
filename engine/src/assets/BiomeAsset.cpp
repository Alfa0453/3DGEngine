#include "engine/assets/BiomeAsset.h"

#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <unordered_set>

namespace engine {
namespace {
void SetError(std::string* error, const std::string& message) { if (error) *error = message; }
void NormalizeRange(float& a, float& b, float low, float high) {
    a = std::clamp(a, low, high); b = std::clamp(b, low, high); if (a > b) std::swap(a, b);
}
bool Matches(float value, float minimum, float maximum) {
    return value >= minimum && value <= maximum;
}
float SlopeDegrees(const glm::vec3& normal) {
    const glm::vec3 n = glm::dot(normal, normal) > 0.00001f
        ? glm::normalize(normal) : glm::vec3(0, 1, 0);
    return glm::degrees(std::acos(std::clamp(glm::dot(n, glm::vec3(0, 1, 0)), 0.0f, 1.0f)));
}
void AddDependency(std::vector<AssetHandle>& dependencies, AssetHandle id) {
    if (!id.Valid()) return;
    if (std::find(dependencies.begin(), dependencies.end(), id) == dependencies.end())
        dependencies.push_back(id);
}
}

void NormalizeBiome(BiomeAssetData& biome) {
    biome.previewWorldSize = std::clamp(biome.previewWorldSize, 1.0f, 100000.0f);
    biome.maximumInstances = std::clamp(biome.maximumInstances, 0, 1000000);
    biome.transitionDistance = std::clamp(biome.transitionDistance, 0.0f, 10000.0f);
    biome.moisture = std::clamp(biome.moisture, 0.0f, 1.0f);
    biome.temperature = std::clamp(biome.temperature, 0.0f, 1.0f);
    biome.waterLevel = std::clamp(biome.waterLevel, -100000.0f, 100000.0f);
    if (biome.layers.size() > 5) biome.layers.resize(5);
    for (BiomeLayerRule& layer : biome.layers) {
        NormalizeRange(layer.heightMin, layer.heightMax, 0.0f, 1.0f);
        NormalizeRange(layer.slopeMinDegrees, layer.slopeMaxDegrees, 0.0f, 90.0f);
        NormalizeRange(layer.moistureMin, layer.moistureMax, 0.0f, 1.0f);
        NormalizeRange(layer.temperatureMin, layer.temperatureMax, 0.0f, 1.0f);
    }
    for (BiomeFoliageRule& rule : biome.foliage) {
        rule.weight = std::clamp(rule.weight, 0.0f, 100.0f);
        rule.density = std::clamp(rule.density, 0.0f, 100.0f);
        NormalizeRange(rule.scaleRange.x, rule.scaleRange.y, 0.01f, 100.0f);
        NormalizeRange(rule.heightMin, rule.heightMax, 0.0f, 1.0f);
        NormalizeRange(rule.slopeMinDegrees, rule.slopeMaxDegrees, 0.0f, 90.0f);
        NormalizeRange(rule.moistureMin, rule.moistureMax, 0.0f, 1.0f);
        NormalizeRange(rule.temperatureMin, rule.temperatureMax, 0.0f, 1.0f);
    }
}

bool ValidateBiome(const BiomeAssetData& input, std::string* error) {
    BiomeAssetData biome = input; NormalizeBiome(biome);
    if (biome.name.empty()) { SetError(error, "Biome name is empty."); return false; }
    for (const auto& layer : biome.layers) {
        if (layer.materialPath.empty()) { SetError(error, "Biome layer has no material."); return false; }
    }
    for (const auto& rule : biome.foliage) {
        if (rule.meshPath.empty() && rule.density > 0.0f) {
            SetError(error, "Foliage rule has no static mesh."); return false;
        }
    }
    SetError(error, {}); return true;
}

std::vector<BiomePlacement> EvaluateBiome(const BiomeAssetData& input,
    const BiomeSurfaceSampler& surface, const glm::vec3& offset, unsigned seedOverride) {
    BiomeAssetData biome = input; NormalizeBiome(biome);
    std::vector<BiomePlacement> result;
    if (!surface || biome.maximumInstances <= 0) return result;
    const float half = biome.previewWorldSize * 0.5f;
    const float area = biome.previewWorldSize * biome.previewWorldSize;
    std::mt19937 rng(seedOverride ? seedOverride : biome.seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    result.reserve(static_cast<std::size_t>(std::min(biome.maximumInstances, 65536)));
    for (std::size_t ruleIndex = 0; ruleIndex < biome.foliage.size(); ++ruleIndex) {
        const BiomeFoliageRule& rule = biome.foliage[ruleIndex];
        if (rule.meshPath.empty() || rule.density <= 0.0f || rule.weight <= 0.0f) continue;
        const int candidates = std::min(biome.maximumInstances,
            static_cast<int>(std::ceil(area * rule.density * rule.weight)));
        for (int i = 0; i < candidates
            && static_cast<int>(result.size()) < biome.maximumInstances; ++i) {
            const float x = offset.x - half + unit(rng) * biome.previewWorldSize;
            const float z = offset.z - half + unit(rng) * biome.previewWorldSize;
            const BiomeSurfaceSample sample = surface(x, z);
            const float slope = SlopeDegrees(sample.normal);
            if (!Matches(sample.normalizedHeight, rule.heightMin, rule.heightMax)
                || !Matches(slope, rule.slopeMinDegrees, rule.slopeMaxDegrees)
                || !Matches(sample.moisture, rule.moistureMin, rule.moistureMax)
                || !Matches(sample.temperature, rule.temperatureMin, rule.temperatureMax)) continue;
            const float scale = glm::mix(rule.scaleRange.x, rule.scaleRange.y, unit(rng));
            const float yaw = unit(rng) * glm::two_pi<float>();
            glm::quat rotation = glm::angleAxis(yaw, glm::vec3(0, 1, 0));
            if (rule.alignToSurface && glm::dot(sample.normal, sample.normal) > 0.00001f)
                rotation = glm::rotation(glm::vec3(0, 1, 0), glm::normalize(sample.normal)) * rotation;
            result.push_back({{x, sample.height + offset.y, z}, rotation,
                glm::vec3(scale), rule.meshPath, rule.meshId, ruleIndex});
        }
    }
    return result;
}

bool SaveBiomeAsset(const std::string& path, BiomeAssetData& biome, std::string* error) {
    NormalizeBiome(biome);
    if (!ValidateBiome(biome, error)) return false;
    if (!biome.assetId.Valid()) biome.assetId = AssetHandle::Generate();
    std::error_code ec; const std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) { SetError(error, "Could not write biome asset: " + path); return false; }
    out << "3DG_BIOME 1 " << biome.assetId.ToString() << '\n'
        << std::quoted(biome.name) << ' ' << biome.seed << ' ' << biome.previewWorldSize << ' '
        << biome.maximumInstances << ' ' << biome.transitionDistance << ' '
        << biome.moisture << ' ' << biome.temperature << '\n';
    out << "LAYERS " << biome.layers.size() << '\n';
    for (const auto& layer : biome.layers)
        out << std::quoted(layer.name) << ' ' << std::quoted(layer.materialPath) << ' '
            << (layer.materialId.Valid() ? layer.materialId.ToString() : "-") << ' '
            << layer.heightMin << ' ' << layer.heightMax << ' '
            << layer.slopeMinDegrees << ' ' << layer.slopeMaxDegrees << ' '
            << layer.moistureMin << ' ' << layer.moistureMax << ' '
            << layer.temperatureMin << ' ' << layer.temperatureMax << '\n';
    out << "FOLIAGE " << biome.foliage.size() << '\n';
    for (const auto& rule : biome.foliage)
        out << std::quoted(rule.name) << ' ' << std::quoted(rule.meshPath) << ' '
            << (rule.meshId.Valid() ? rule.meshId.ToString() : "-") << ' '
            << rule.weight << ' ' << rule.density << ' ' << rule.scaleRange.x << ' '
            << rule.scaleRange.y << ' ' << rule.heightMin << ' ' << rule.heightMax << ' '
            << rule.slopeMinDegrees << ' ' << rule.slopeMaxDegrees << ' '
            << rule.moistureMin << ' ' << rule.moistureMax << ' '
            << rule.temperatureMin << ' ' << rule.temperatureMax << ' '
            << rule.alignToSurface << ' ' << rule.castShadows << '\n';
    out << "ENV " << std::quoted(biome.weatherPath) << ' '
        << (biome.weatherId.Valid() ? biome.weatherId.ToString() : "-") << ' '
        << biome.waterEnabled << ' ' << biome.waterLevel << ' '
        << std::quoted(biome.waterMaterialPath) << ' '
        << (biome.waterMaterialId.Valid() ? biome.waterMaterialId.ToString() : "-") << ' '
        << std::quoted(biome.particlePath) << ' '
        << (biome.particleId.Valid() ? biome.particleId.ToString() : "-") << ' '
        << std::quoted(biome.ambientAudioPath) << ' '
        << (biome.ambientAudioId.Valid() ? biome.ambientAudioId.ToString() : "-") << '\n';
    std::vector<AssetHandle> deps;
    for (const auto& l : biome.layers) AddDependency(deps, l.materialId);
    for (const auto& f : biome.foliage) AddDependency(deps, f.meshId);
    AddDependency(deps, biome.weatherId); AddDependency(deps, biome.waterMaterialId);
    AddDependency(deps, biome.particleId); AddDependency(deps, biome.ambientAudioId);
    out << "ASSET_DEPS " << deps.size(); for (auto id : deps) out << ' ' << id.ToString(); out << '\n';
    SetError(error, {}); return static_cast<bool>(out);
}

bool LoadBiomeAsset(const std::string& path, BiomeAssetData* output, std::string* error) {
    if (!output) { SetError(error, "Biome output is null."); return false; }
    std::ifstream in(path); BiomeAssetData biome; std::string magic, id;
    int version = 0;
    if (!(in >> magic >> version >> id) || magic != "3DG_BIOME" || version != 1
        || !AssetHandle::Parse(id, &biome.assetId)) {
        SetError(error, "Invalid biome asset: " + path); return false;
    }
    in >> std::quoted(biome.name) >> biome.seed >> biome.previewWorldSize
       >> biome.maximumInstances >> biome.transitionDistance >> biome.moisture
       >> biome.temperature;
    std::string section; std::size_t count = 0;
    if (!(in >> section >> count) || section != "LAYERS" || count > 5) {
        SetError(error, "Biome layers are malformed."); return false;
    }
    biome.layers.resize(count);
    for (auto& layer : biome.layers) {
        std::string assetId;
        in >> std::quoted(layer.name) >> std::quoted(layer.materialPath) >> assetId
           >> layer.heightMin >> layer.heightMax >> layer.slopeMinDegrees
           >> layer.slopeMaxDegrees >> layer.moistureMin >> layer.moistureMax
           >> layer.temperatureMin >> layer.temperatureMax;
        if (assetId != "-" && !AssetHandle::Parse(assetId, &layer.materialId)) in.setstate(std::ios::failbit);
    }
    if (!(in >> section >> count) || section != "FOLIAGE" || count > 4096) {
        SetError(error, "Biome foliage is malformed."); return false;
    }
    biome.foliage.resize(count);
    for (auto& rule : biome.foliage) {
        std::string assetId;
        in >> std::quoted(rule.name) >> std::quoted(rule.meshPath) >> assetId
           >> rule.weight >> rule.density >> rule.scaleRange.x >> rule.scaleRange.y
           >> rule.heightMin >> rule.heightMax >> rule.slopeMinDegrees
           >> rule.slopeMaxDegrees >> rule.moistureMin >> rule.moistureMax
           >> rule.temperatureMin >> rule.temperatureMax >> rule.alignToSurface
           >> rule.castShadows;
        if (assetId != "-" && !AssetHandle::Parse(assetId, &rule.meshId)) in.setstate(std::ios::failbit);
    }
    std::string weatherId, waterId, particleId, audioId;
    in >> section >> std::quoted(biome.weatherPath) >> weatherId
       >> biome.waterEnabled >> biome.waterLevel >> std::quoted(biome.waterMaterialPath)
       >> waterId >> std::quoted(biome.particlePath) >> particleId
       >> std::quoted(biome.ambientAudioPath) >> audioId;
    auto parse = [&](const std::string& text, AssetHandle& handle) {
        return text == "-" || AssetHandle::Parse(text, &handle);
    };
    if (!in || section != "ENV" || !parse(weatherId, biome.weatherId)
        || !parse(waterId, biome.waterMaterialId) || !parse(particleId, biome.particleId)
        || !parse(audioId, biome.ambientAudioId)) {
        SetError(error, "Biome environment is malformed."); return false;
    }
    NormalizeBiome(biome); *output = std::move(biome); SetError(error, {}); return true;
}

} // namespace engine
