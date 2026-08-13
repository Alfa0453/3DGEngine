#include "engine/assets/ScatterGraphAsset.h"

#include <algorithm>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <unordered_set>

namespace engine {
namespace {

void Error(std::string* error, const std::string& text) {
    if (error) *error = text;
}

void WriteVec2(std::ostream& out, const glm::vec2& value) {
    out << value.x << ' ' << value.y;
}
void WriteVec3(std::ostream& out, const glm::vec3& value) {
    out << value.x << ' ' << value.y << ' ' << value.z;
}
bool ReadVec2(std::istream& in, glm::vec2* value) {
    return value && static_cast<bool>(in >> value->x >> value->y);
}
bool ReadVec3(std::istream& in, glm::vec3* value) {
    return value && static_cast<bool>(in >> value->x >> value->y >> value->z);
}

float SlopeDegrees(const glm::vec3& normal) {
    if (glm::dot(normal, normal) < 1.0e-8f) return 0.0f;
    return glm::degrees(std::acos(glm::clamp(
        glm::dot(glm::normalize(normal), glm::vec3(0, 1, 0)), -1.0f, 1.0f)));
}

glm::quat AlignUp(const glm::vec3& normal) {
    const glm::vec3 n = glm::dot(normal, normal) > 1.0e-8f
        ? glm::normalize(normal) : glm::vec3(0, 1, 0);
    const float cosine = glm::clamp(glm::dot(glm::vec3(0, 1, 0), n), -1.0f, 1.0f);
    if (cosine < -0.9999f) return glm::angleAxis(glm::pi<float>(), glm::vec3(1, 0, 0));
    if (cosine > 0.9999f) return glm::quat(1, 0, 0, 0);
    return glm::angleAxis(std::acos(cosine),
                          glm::normalize(glm::cross(glm::vec3(0, 1, 0), n)));
}

} // namespace

const char* ScatterNodeTypeName(ScatterNodeType type) {
    switch (type) {
    case ScatterNodeType::Region: return "Region Input";
    case ScatterNodeType::Density: return "Density";
    case ScatterNodeType::HeightFilter: return "Height Filter";
    case ScatterNodeType::SlopeFilter: return "Slope Filter";
    case ScatterNodeType::Transform: return "Random Transform";
    case ScatterNodeType::ExclusionCircle: return "Exclusion Circle";
    case ScatterNodeType::MeshOutput: return "Weighted Mesh Output";
    }
    return "Unknown";
}

bool ValidateScatterGraphAsset(const ScatterGraphAssetData& asset,
                               std::string* error) {
    if (asset.name.empty() || asset.nodes.size() > 4096
        || asset.maximumInstances == 0 || asset.maximumInstances > 1000000
        || glm::any(glm::lessThanEqual(asset.regionMaximum, asset.regionMinimum))) {
        Error(error, "Scatter graph header, region, or instance limit is invalid.");
        return false;
    }
    std::unordered_set<std::uint32_t> ids;
    bool output = false;
    for (const ScatterGraphNode& node : asset.nodes) {
        if (node.id == 0 || !ids.insert(node.id).second) {
            Error(error, "Scatter graph node IDs must be unique and non-zero.");
            return false;
        }
        if (node.type == ScatterNodeType::MeshOutput) {
            output |= node.enabled && !node.meshPath.empty() && node.weight > 0.0f;
        }
        if (node.density < 0.0f || node.radius < 0.0f || node.weight < 0.0f
            || glm::any(glm::lessThanEqual(node.minimumScale, glm::vec3(0.0f)))
            || glm::any(glm::lessThan(node.maximumScale, node.minimumScale))) {
            Error(error, "Scatter graph node values are invalid.");
            return false;
        }
    }
    for (const ScatterGraphNode& node : asset.nodes) {
        if (node.input != 0 && !ids.count(node.input)) {
            Error(error, "Scatter graph contains a missing node connection.");
            return false;
        }
    }
    if (!output) {
        Error(error, "Scatter graph needs at least one enabled weighted mesh output.");
        return false;
    }
    Error(error, {});
    return true;
}

bool SaveScatterGraphAsset(const std::string& path, ScatterGraphAssetData asset,
                           std::string* error) {
    if (!asset.header.id.Valid()) asset.header.id = AssetHandle::Generate();
    asset.header.type = AssetType::ScatterGraph;
    asset.header.assetVersion = kScatterGraphAssetVersion;
    asset.header.importerVersion = 1;
    asset.header.dependencies.clear();
    std::unordered_set<AssetHandle, AssetHandleHash> dependencies;
    for (const ScatterGraphNode& node : asset.nodes) {
        if (node.meshId.Valid() && dependencies.insert(node.meshId).second)
            asset.header.dependencies.push_back(node.meshId);
    }
    if (!ValidateScatterGraphAsset(asset, error)) return false;

    std::ostringstream payload;
    payload << "3DGScatter " << kScatterGraphAssetVersion << '\n'
            << "name " << std::quoted(asset.name) << '\n'
            << "settings " << asset.seed << ' ' << asset.maximumInstances << ' ';
    WriteVec3(payload, asset.regionMinimum); payload << ' ';
    WriteVec3(payload, asset.regionMaximum); payload << '\n';
    payload << "nodes " << asset.nodes.size() << '\n';
    for (const ScatterGraphNode& node : asset.nodes) {
        payload << "node " << node.id << ' ' << static_cast<std::uint32_t>(node.type)
                << ' ' << node.input << ' ' << std::quoted(node.name) << ' ';
        WriteVec2(payload, node.editorPosition);
        payload << ' ' << (node.enabled ? 1 : 0) << ' ' << node.density << ' '
                << node.minimum << ' ' << node.maximum << ' ';
        WriteVec3(payload, node.position); payload << ' ' << node.radius << ' ';
        WriteVec3(payload, node.minimumScale); payload << ' ';
        WriteVec3(payload, node.maximumScale);
        payload << ' ' << node.minimumYaw << ' ' << node.maximumYaw << ' '
                << (node.alignToSurface ? 1 : 0) << ' '
                << std::quoted(node.meshPath) << ' '
                << (node.meshId.Valid() ? node.meshId.ToString() : std::string("-"))
                << ' ' << node.weight << '\n';
    }
    const std::string bytes = payload.str();
    asset.header.payloadSize = static_cast<std::uint64_t>(bytes.size());
    std::error_code ec;
    const std::filesystem::path destination(path);
    if (destination.has_parent_path())
        std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) { Error(error, "Could not create scatter graph folder: " + ec.message()); return false; }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out || !WriteNativeAssetHeader(out, asset.header, error)) return false;
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) { Error(error, "Could not finish writing scatter graph asset."); return false; }
    Error(error, {});
    return true;
}

bool LoadScatterGraphAsset(const std::string& path, ScatterGraphAssetData* output,
                           std::string* error) {
    if (!output) { Error(error, "Scatter graph output is null."); return false; }
    std::ifstream in(path, std::ios::binary);
    NativeAssetHeader header;
    if (!in || !ReadNativeAssetHeader(in, &header, error)) return false;
    if (header.type != AssetType::ScatterGraph || header.assetVersion == 0
        || header.assetVersion > kScatterGraphAssetVersion
        || header.payloadSize > 64ull * 1024ull * 1024ull) {
        Error(error, "Scatter graph type or version is unsupported."); return false;
    }
    std::string bytes(static_cast<std::size_t>(header.payloadSize), '\0');
    in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!in) { Error(error, "Scatter graph payload is truncated."); return false; }
    std::istringstream data(bytes);
    ScatterGraphAssetData loaded; loaded.header = header;
    std::string magic, tag; std::uint32_t version = 0; std::size_t count = 0;
    if (!(data >> magic >> version) || magic != "3DGScatter"
        || version == 0 || version > kScatterGraphAssetVersion
        || !(data >> tag >> std::quoted(loaded.name)) || tag != "name"
        || !(data >> tag >> loaded.seed >> loaded.maximumInstances) || tag != "settings"
        || !ReadVec3(data, &loaded.regionMinimum) || !ReadVec3(data, &loaded.regionMaximum)
        || !(data >> tag >> count) || tag != "nodes" || count > 4096) {
        Error(error, "Scatter graph payload header is malformed."); return false;
    }
    loaded.nodes.resize(count);
    for (ScatterGraphNode& node : loaded.nodes) {
        std::uint32_t type = 0; int enabled = 1, align = 1; std::string meshId;
        if (!(data >> tag >> node.id >> type >> node.input >> std::quoted(node.name))
            || tag != "node" || type > static_cast<std::uint32_t>(ScatterNodeType::MeshOutput)
            || !ReadVec2(data, &node.editorPosition)
            || !(data >> enabled >> node.density >> node.minimum >> node.maximum)
            || !ReadVec3(data, &node.position) || !(data >> node.radius)
            || !ReadVec3(data, &node.minimumScale) || !ReadVec3(data, &node.maximumScale)
            || !(data >> node.minimumYaw >> node.maximumYaw >> align
                      >> std::quoted(node.meshPath) >> meshId >> node.weight)) {
            Error(error, "Scatter graph node record is malformed."); return false;
        }
        node.type = static_cast<ScatterNodeType>(type);
        node.enabled = enabled != 0; node.alignToSurface = align != 0;
        if (meshId != "-" && !AssetHandle::Parse(meshId, &node.meshId)) {
            Error(error, "Scatter graph mesh dependency ID is invalid."); return false;
        }
    }
    if (!ValidateScatterGraphAsset(loaded, error)) return false;
    *output = std::move(loaded); Error(error, {}); return true;
}

std::vector<ScatterPlacement> EvaluateScatterGraph(
    const ScatterGraphAssetData& asset, const ScatterSurfaceSampler& sampleSurface,
    const glm::vec3& worldOffset, std::uint32_t seedOverride) {
    std::vector<ScatterPlacement> result;
    std::vector<const ScatterGraphNode*> outputs;
    float density = 0.25f, outputWeight = 0.0f;
    glm::vec3 minScale(1.0f), maxScale(1.0f);
    float minYaw = 0.0f, maxYaw = 360.0f; bool align = true;
    for (const ScatterGraphNode& node : asset.nodes) if (node.enabled) {
        if (node.type == ScatterNodeType::Density) density *= std::max(node.density, 0.0f);
        else if (node.type == ScatterNodeType::Transform) {
            minScale = node.minimumScale; maxScale = node.maximumScale;
            minYaw = node.minimumYaw; maxYaw = node.maximumYaw;
            align = node.alignToSurface;
        } else if (node.type == ScatterNodeType::MeshOutput
                   && !node.meshPath.empty() && node.weight > 0.0f) {
            outputs.push_back(&node); outputWeight += node.weight;
        }
    }
    if (outputs.empty() || density <= 0.0f) return result;
    const glm::vec3 extent = asset.regionMaximum - asset.regionMinimum;
    const double requested = static_cast<double>(extent.x) * extent.z * density;
    const std::size_t count = std::min<std::size_t>(asset.maximumInstances,
        static_cast<std::size_t>(std::max(0.0, std::round(requested))));
    if (count == 0) return result;

    std::mt19937 random(seedOverride ? seedOverride : asset.seed);
    std::uniform_real_distribution<float> x(asset.regionMinimum.x, asset.regionMaximum.x);
    std::uniform_real_distribution<float> z(asset.regionMinimum.z, asset.regionMaximum.z);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> yaw(minYaw, maxYaw);
    const std::size_t maxAttempts = std::max<std::size_t>(count * 12u, 64u);
    result.reserve(count);
    for (std::size_t attempt = 0; attempt < maxAttempts && result.size() < count; ++attempt) {
        const float px = x(random) + worldOffset.x, pz = z(random) + worldOffset.z;
        const ScatterSurfaceSample surface = sampleSurface
            ? sampleSurface(px, pz) : ScatterSurfaceSample{};
        if (!surface.valid || surface.layerMask <= 0.0f) continue;
        const float py = surface.height + worldOffset.y;
        bool accepted = py >= asset.regionMinimum.y + worldOffset.y
            && py <= asset.regionMaximum.y + worldOffset.y;
        for (const ScatterGraphNode& node : asset.nodes) if (node.enabled && accepted) {
            if (node.type == ScatterNodeType::HeightFilter)
                accepted = py >= node.minimum && py <= node.maximum;
            else if (node.type == ScatterNodeType::SlopeFilter) {
                const float slope = SlopeDegrees(surface.normal);
                accepted = slope >= node.minimum && slope <= node.maximum;
            } else if (node.type == ScatterNodeType::ExclusionCircle) {
                const glm::vec2 delta(px - node.position.x - worldOffset.x,
                                      pz - node.position.z - worldOffset.z);
                accepted = glm::dot(delta, delta) > node.radius * node.radius;
            }
        }
        if (!accepted) continue;
        float pick = unit(random) * outputWeight;
        const ScatterGraphNode* chosen = outputs.back();
        for (const ScatterGraphNode* output : outputs) {
            pick -= output->weight;
            if (pick <= 0.0f) { chosen = output; break; }
        }
        ScatterPlacement placement;
        placement.meshPath = chosen->meshPath; placement.meshId = chosen->meshId;
        placement.position = {px, py, pz};
        const float tx = unit(random), ty = unit(random), tz = unit(random);
        placement.scale = glm::mix(minScale, maxScale, glm::vec3(tx, ty, tz));
        placement.rotation = (align ? AlignUp(surface.normal) : glm::quat(1, 0, 0, 0))
            * glm::angleAxis(glm::radians(yaw(random)), glm::vec3(0, 1, 0));
        result.push_back(std::move(placement));
    }
    return result;
}

} // namespace engine
