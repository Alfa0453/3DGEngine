#include "engine/assets/FoliageAsset.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace engine {
namespace {

void SetError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

void WriteVec3(std::ostream& out, const glm::vec3& value) {
    out << value.x << ' ' << value.y << ' ' << value.z;
}

bool ReadVec3(std::istream& in, glm::vec3* value) {
    return value && static_cast<bool>(in >> value->x >> value->y >> value->z);
}

} // namespace

bool ValidateFoliageAsset(const FoliageAssetData& asset, std::string* error) {
    if (asset.types.size() > 1024) {
        SetError(error, "Foliage asset contains too many types.");
        return false;
    }
    for (const FoliageTypeAsset& type : asset.types) {
        if (type.name.empty() || type.meshPath.empty()) {
            SetError(error, "Every foliage type needs a name and static mesh.");
            return false;
        }
        if (type.density < 0.0f || type.minimumSpacing < 0.0f
            || type.cullEndDistance <= 0.0f
            || type.cullStartDistance > type.cullEndDistance
            || type.lod1Distance < 0.0f || type.lod2Distance < type.lod1Distance) {
            SetError(error, "Foliage placement or culling values are invalid.");
            return false;
        }
        if (glm::any(glm::lessThanEqual(type.minScale, glm::vec3(0.0f)))
            || glm::any(glm::lessThan(type.maxScale, type.minScale))) {
            SetError(error, "Foliage scale range is invalid.");
            return false;
        }
    }
    SetError(error, {});
    return true;
}

bool SaveFoliageAsset(const std::string& path, FoliageAssetData asset,
                      std::string* error) {
    if (!asset.header.id.Valid()) asset.header.id = AssetHandle::Generate();
    asset.header.type = AssetType::Foliage;
    asset.header.assetVersion = kFoliageAssetVersion;
    asset.header.importerVersion = 1;
    asset.header.dependencies.clear();
    std::unordered_set<AssetHandle, AssetHandleHash> dependencies;
    for (const FoliageTypeAsset& type : asset.types) {
        if (type.meshId.Valid() && dependencies.insert(type.meshId).second)
            asset.header.dependencies.push_back(type.meshId);
        if (type.materialId.Valid() && dependencies.insert(type.materialId).second)
            asset.header.dependencies.push_back(type.materialId);
        if (type.lod1MeshId.Valid() && dependencies.insert(type.lod1MeshId).second)
            asset.header.dependencies.push_back(type.lod1MeshId);
        if (type.lod2MeshId.Valid() && dependencies.insert(type.lod2MeshId).second)
            asset.header.dependencies.push_back(type.lod2MeshId);
    }
    if (!ValidateFoliageAsset(asset, error)) return false;

    std::ostringstream payload;
    payload << "3DGFoliage " << kFoliageAssetVersion << '\n'
            << "name " << std::quoted(asset.name) << '\n'
            << "types " << asset.types.size() << '\n';
    for (const FoliageTypeAsset& type : asset.types) {
        payload << "type " << std::quoted(type.name) << ' '
                << std::quoted(type.meshPath) << ' '
                << (type.meshId.Valid() ? type.meshId.ToString() : std::string("-")) << ' '
                << std::quoted(type.materialPath) << ' '
                << (type.materialId.Valid() ? type.materialId.ToString() : std::string("-")) << ' '
                << type.density << ' ';
        WriteVec3(payload, type.minScale); payload << ' ';
        WriteVec3(payload, type.maxScale); payload << ' ';
        WriteVec3(payload, type.minRotation); payload << ' ';
        WriteVec3(payload, type.maxRotation); payload << ' '
                << type.minimumSpacing << ' ' << type.minimumSlopeDegrees << ' '
                << type.maximumSlopeDegrees << ' ' << type.minimumWorldHeight << ' '
                << type.maximumWorldHeight << ' ' << type.cullStartDistance << ' '
                << type.cullEndDistance << ' ' << type.windStrength << ' '
                << (type.alignToSurface ? 1 : 0) << ' '
                << (type.randomYaw ? 1 : 0) << ' '
                << (type.castShadows ? 1 : 0) << ' '
                << (type.collisionEnabled ? 1 : 0) << ' '
                << std::quoted(type.lod1MeshPath) << ' '
                << (type.lod1MeshId.Valid() ? type.lod1MeshId.ToString() : std::string("-")) << ' '
                << std::quoted(type.lod2MeshPath) << ' '
                << (type.lod2MeshId.Valid() ? type.lod2MeshId.ToString() : std::string("-")) << ' '
                << type.lod1Distance << ' ' << type.lod2Distance << '\n';
    }
    const std::string bytes = payload.str();
    asset.header.payloadSize = static_cast<std::uint64_t>(bytes.size());

    const std::filesystem::path destination(path);
    std::error_code ec;
    if (destination.has_parent_path())
        std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
        SetError(error, "Could not create foliage asset directory: " + ec.message());
        return false;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out || !WriteNativeAssetHeader(out, asset.header, error)) return false;
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        SetError(error, "Could not finish writing foliage asset.");
        return false;
    }
    SetError(error, {});
    return true;
}

bool LoadFoliageAsset(const std::string& path, FoliageAssetData* output,
                      std::string* error) {
    if (!output) {
        SetError(error, "Foliage asset output is null.");
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    NativeAssetHeader header;
    if (!in || !ReadNativeAssetHeader(in, &header, error)) return false;
    if (header.type != AssetType::Foliage
        || header.assetVersion == 0 || header.assetVersion > kFoliageAssetVersion
        || header.payloadSize > 16ull * 1024ull * 1024ull) {
        SetError(error, "Foliage asset type or version is unsupported.");
        return false;
    }
    std::string payload(static_cast<std::size_t>(header.payloadSize), '\0');
    in.read(payload.data(), static_cast<std::streamsize>(payload.size()));
    if (!in) {
        SetError(error, "Foliage asset payload is truncated.");
        return false;
    }
    std::istringstream data(payload);
    std::string magic, tag;
    std::uint32_t version = 0;
    std::size_t typeCount = 0;
    FoliageAssetData loaded;
    loaded.header = header;
    if (!(data >> magic >> version) || magic != "3DGFoliage"
        || version == 0 || version > kFoliageAssetVersion
        || !(data >> tag >> std::quoted(loaded.name)) || tag != "name"
        || !(data >> tag >> typeCount) || tag != "types" || typeCount > 1024) {
        SetError(error, "Foliage asset payload header is malformed.");
        return false;
    }
    loaded.types.resize(typeCount);
    for (FoliageTypeAsset& type : loaded.types) {
        std::string meshId, materialId, lod1Id, lod2Id;
        int align = 1, yaw = 1, shadows = 1, collision = 0;
        if (!(data >> tag) || tag != "type"
            || !(data >> std::quoted(type.name) >> std::quoted(type.meshPath) >> meshId
                      >> std::quoted(type.materialPath) >> materialId >> type.density)
            || !ReadVec3(data, &type.minScale) || !ReadVec3(data, &type.maxScale)
            || !ReadVec3(data, &type.minRotation) || !ReadVec3(data, &type.maxRotation)
            || !(data >> type.minimumSpacing >> type.minimumSlopeDegrees
                      >> type.maximumSlopeDegrees >> type.minimumWorldHeight
                      >> type.maximumWorldHeight >> type.cullStartDistance
                      >> type.cullEndDistance >> type.windStrength
                      >> align >> yaw >> shadows >> collision)) {
            SetError(error, "Foliage type record is malformed.");
            return false;
        }
        if (meshId != "-" && !AssetHandle::Parse(meshId, &type.meshId)) {
            SetError(error, "Foliage mesh dependency ID is invalid.");
            return false;
        }
        if (materialId != "-" && !AssetHandle::Parse(materialId, &type.materialId)) {
            SetError(error, "Foliage material dependency ID is invalid.");
            return false;
        }
        if (version >= 2 && !(data >> std::quoted(type.lod1MeshPath) >> lod1Id
                                 >> std::quoted(type.lod2MeshPath) >> lod2Id
                                 >> type.lod1Distance >> type.lod2Distance)) {
            SetError(error, "Foliage LOD record is malformed.");
            return false;
        }
        if (lod1Id != "-" && !AssetHandle::Parse(lod1Id, &type.lod1MeshId)) {
            SetError(error, "Foliage LOD1 dependency ID is invalid.");
            return false;
        }
        if (lod2Id != "-" && !AssetHandle::Parse(lod2Id, &type.lod2MeshId)) {
            SetError(error, "Foliage LOD2 dependency ID is invalid.");
            return false;
        }
        type.alignToSurface = align != 0;
        type.randomYaw = yaw != 0;
        type.castShadows = shadows != 0;
        type.collisionEnabled = collision != 0;
    }
    if (!ValidateFoliageAsset(loaded, error)) return false;
    *output = std::move(loaded);
    SetError(error, {});
    return true;
}

} // namespace engine
