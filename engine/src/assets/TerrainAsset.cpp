#include "engine/assets/TerrainAsset.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace engine {
namespace {
void SetError(std::string* error, const std::string& message) {
    if (error) *error = message;
}
}

bool ValidateTerrainAsset(const TerrainAssetData& asset, std::string* error) {
    const std::size_t count = asset.resolution > 0
        ? static_cast<std::size_t>(asset.resolution) * asset.resolution : 0;
    if (asset.name.empty() || asset.resolution < 2 || asset.resolution > 4096
        || asset.size <= 0.0f || asset.maxHeight < 0.0f
        || asset.octaves < 1 || asset.octaves > 16 || asset.frequency <= 0.0f
        || asset.heights.size() != count
        || asset.grassDensity < 0.0f || asset.grassHeight <= 0.0f
        || asset.grassMinHeightScale <= 0.0f
        || asset.grassMaxHeightScale < asset.grassMinHeightScale
        || asset.grassWindStrength < 0.0f || asset.grassWindSpeed < 0.0f
        || (!asset.paint.empty() && asset.paint.size() != count)) {
        SetError(error, "Terrain dimensions or surface data are invalid.");
        return false;
    }
    if (std::any_of(asset.paint.begin(), asset.paint.end(),
                    [](std::uint8_t value) { return value > 5; })) {
        SetError(error, "Terrain paint contains an invalid layer index.");
        return false;
    }
    SetError(error, {});
    return true;
}

bool SaveTerrainAsset(const std::string& path, TerrainAssetData asset,
                      std::string* error) {
    if (!asset.header.id.Valid()) asset.header.id = AssetHandle::Generate();
    asset.header.type = AssetType::Terrain;
    asset.header.assetVersion = kTerrainAssetVersion;
    asset.header.importerVersion = 1;
    asset.header.dependencies.clear();
    if (!ValidateTerrainAsset(asset, error)) return false;

    std::ostringstream payload(std::ios::out | std::ios::binary);
    payload << "3DGTerrain " << kTerrainAssetVersion << '\n'
            << "name " << std::quoted(asset.name) << '\n'
            << "settings " << asset.resolution << ' ' << asset.size << ' '
            << asset.maxHeight << ' ' << asset.seed << ' ' << asset.octaves << ' '
            << asset.frequency << '\n';
    for (int layer = 1; layer <= 5; ++layer)
        payload << "material " << layer << ' '
                << std::quoted(asset.layerMaterials[layer]) << '\n';
    payload << "grass " << (asset.grassEnabled ? 1 : 0) << ' '
            << asset.grassDensity << ' ' << asset.grassHeight << ' '
            << (asset.grassRandomizeHeight ? 1 : 0) << ' '
            << asset.grassMinHeightScale << ' ' << asset.grassMaxHeightScale << ' '
            << asset.grassWindStrength << ' ' << asset.grassWindSpeed << ' '
            << asset.grassBaseColor.r << ' ' << asset.grassBaseColor.g << ' '
            << asset.grassBaseColor.b << ' ' << asset.grassTipColor.r << ' '
            << asset.grassTipColor.g << ' ' << asset.grassTipColor.b << '\n';
    payload << "heights " << asset.heights.size() << '\n';
    payload.write(reinterpret_cast<const char*>(asset.heights.data()),
                  static_cast<std::streamsize>(asset.heights.size() * sizeof(float)));
    payload << '\n' << "paint " << asset.paint.size() << '\n';
    if (!asset.paint.empty())
        payload.write(reinterpret_cast<const char*>(asset.paint.data()),
                      static_cast<std::streamsize>(asset.paint.size()));
    const std::string bytes = payload.str();
    asset.header.payloadSize = static_cast<std::uint64_t>(bytes.size());

    const std::filesystem::path destination(path);
    std::error_code ec;
    if (destination.has_parent_path())
        std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
        SetError(error, "Could not create terrain asset directory: " + ec.message());
        return false;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out || !WriteNativeAssetHeader(out, asset.header, error)) return false;
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        SetError(error, "Could not finish writing terrain asset.");
        return false;
    }
    SetError(error, {});
    return true;
}

bool LoadTerrainAsset(const std::string& path, TerrainAssetData* output,
                      std::string* error) {
    if (!output) {
        SetError(error, "Terrain asset output is null.");
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    NativeAssetHeader header;
    if (!in || !ReadNativeAssetHeader(in, &header, error)) return false;
    constexpr std::uint64_t maximumPayload = 256ull * 1024ull * 1024ull;
    if (header.type != AssetType::Terrain || header.assetVersion == 0
        || header.assetVersion > kTerrainAssetVersion
        || header.payloadSize > maximumPayload) {
        SetError(error, "Terrain asset type, version, or size is unsupported.");
        return false;
    }
    std::string bytes(static_cast<std::size_t>(header.payloadSize), '\0');
    in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!in) {
        SetError(error, "Terrain asset payload is truncated.");
        return false;
    }
    std::istringstream data(bytes, std::ios::in | std::ios::binary);
    TerrainAssetData loaded;
    loaded.header = header;
    std::string magic, tag;
    std::uint32_t version = 0;
    if (!(data >> magic >> version) || magic != "3DGTerrain"
        || version == 0 || version > kTerrainAssetVersion
        || !(data >> tag >> std::quoted(loaded.name)) || tag != "name"
        || !(data >> tag >> loaded.resolution >> loaded.size >> loaded.maxHeight
                  >> loaded.seed >> loaded.octaves >> loaded.frequency)
        || tag != "settings") {
        SetError(error, "Terrain asset header is malformed.");
        return false;
    }
    for (int expected = 1; expected <= 5; ++expected) {
        int layer = 0;
        if (!(data >> tag >> layer >> std::quoted(loaded.layerMaterials[expected]))
            || tag != "material" || layer != expected) {
            SetError(error, "Terrain material record is malformed.");
            return false;
        }
    }
    int grassEnabled = 0, randomizeHeight = 0;
    if (!(data >> tag >> grassEnabled >> loaded.grassDensity >> loaded.grassHeight
              >> randomizeHeight >> loaded.grassMinHeightScale
              >> loaded.grassMaxHeightScale >> loaded.grassWindStrength
              >> loaded.grassWindSpeed >> loaded.grassBaseColor.r
              >> loaded.grassBaseColor.g >> loaded.grassBaseColor.b
              >> loaded.grassTipColor.r >> loaded.grassTipColor.g
              >> loaded.grassTipColor.b) || tag != "grass") {
        SetError(error, "Terrain grass record is malformed.");
        return false;
    }
    loaded.grassEnabled = grassEnabled != 0;
    loaded.grassRandomizeHeight = randomizeHeight != 0;
    std::size_t heightCount = 0, paintCount = 0;
    if (!(data >> tag >> heightCount) || tag != "heights") {
        SetError(error, "Terrain height record is malformed.");
        return false;
    }
    const std::size_t expectedCount = loaded.resolution > 0
        ? static_cast<std::size_t>(loaded.resolution) * loaded.resolution : 0;
    if (expectedCount == 0 || expectedCount > 4096ull * 4096ull
        || heightCount != expectedCount) {
        SetError(error, "Terrain height count does not match its resolution.");
        return false;
    }
    data.get();
    loaded.heights.resize(heightCount);
    data.read(reinterpret_cast<char*>(loaded.heights.data()),
              static_cast<std::streamsize>(heightCount * sizeof(float)));
    if (!data || data.get() != '\n' || !(data >> tag >> paintCount) || tag != "paint") {
        SetError(error, "Terrain height or paint record is truncated.");
        return false;
    }
    if (paintCount != 0 && paintCount != expectedCount) {
        SetError(error, "Terrain paint count does not match its resolution.");
        return false;
    }
    data.get();
    loaded.paint.resize(paintCount);
    if (paintCount)
        data.read(reinterpret_cast<char*>(loaded.paint.data()),
                  static_cast<std::streamsize>(paintCount));
    if (!data || !ValidateTerrainAsset(loaded, error)) return false;
    *output = std::move(loaded);
    SetError(error, {});
    return true;
}

} // namespace engine
