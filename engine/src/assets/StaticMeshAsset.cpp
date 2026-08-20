#include "engine/assets/StaticMeshAsset.h"

#include "engine/assets/AssetRegistry.h"
#include "engine/graphics/ImageDecode.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <type_traits>
#include <utility>

namespace engine {
namespace {

constexpr std::uint32_t kMaximumCollectionEntries = 100000;
constexpr std::uint32_t kMaximumStringBytes = 1024 * 1024;
constexpr std::uint64_t kMaximumElementCount = 500000000ull;
constexpr std::uint64_t kMaximumTextureBytes = 1024ull * 1024ull * 1024ull;

void SetError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

template<typename T>
bool WriteUnsigned(std::ostream& output, T value) {
    static_assert(std::is_unsigned<T>::value, "unsigned integer required");
    for (std::size_t i = 0; i < sizeof(T); ++i)
        output.put(static_cast<char>((value >> (i * 8u)) & static_cast<T>(0xffu)));
    return static_cast<bool>(output);
}

template<typename T>
bool ReadUnsigned(std::istream& input, T* value) {
    static_assert(std::is_unsigned<T>::value, "unsigned integer required");
    if (!value) return false;
    *value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        const int byte = input.get();
        if (byte == std::char_traits<char>::eof()) return false;
        *value |= static_cast<T>(static_cast<unsigned char>(byte)) << (i * 8u);
    }
    return true;
}

bool WriteSigned32(std::ostream& output, std::int32_t value) {
    return WriteUnsigned(output, static_cast<std::uint32_t>(value));
}

bool ReadSigned32(std::istream& input, std::int32_t* value) {
    std::uint32_t raw = 0;
    if (!value || !ReadUnsigned(input, &raw)) return false;
    std::memcpy(value, &raw, sizeof(raw));
    return true;
}

bool WriteFloat(std::ostream& output, float value) {
    std::uint32_t raw = 0;
    static_assert(sizeof(raw) == sizeof(value), "float size must be 32 bits");
    std::memcpy(&raw, &value, sizeof(raw));
    return WriteUnsigned(output, raw);
}

bool ReadFloat(std::istream& input, float* value) {
    std::uint32_t raw = 0;
    if (!value || !ReadUnsigned(input, &raw)) return false;
    std::memcpy(value, &raw, sizeof(raw));
    return true;
}

bool WriteString(std::ostream& output, const std::string& value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) return false;
    if (!WriteUnsigned(output, static_cast<std::uint32_t>(value.size()))) return false;
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    return static_cast<bool>(output);
}

bool ReadString(std::istream& input, std::string* value) {
    std::uint32_t size = 0;
    if (!value || !ReadUnsigned(input, &size) || size > kMaximumStringBytes) return false;
    value->resize(size);
    input.read(value->data(), static_cast<std::streamsize>(size));
    return static_cast<bool>(input);
}

bool AddSize(std::uint64_t* total, std::uint64_t amount) {
    if (!total || amount > std::numeric_limits<std::uint64_t>::max() - *total) return false;
    *total += amount;
    return true;
}

bool AddStringSize(std::uint64_t* total, const std::string& text) {
    return text.size() <= std::numeric_limits<std::uint32_t>::max()
        && AddSize(total, 4u + static_cast<std::uint64_t>(text.size()));
}

bool PayloadSize(const StaticMeshAssetData& asset, std::uint64_t* size) {
    if (!size) return false;
    std::uint64_t total = 6u * sizeof(float) + 4u * sizeof(std::uint32_t);
    for (const StaticMeshMaterialData& material : asset.materials) {
        if (!AddStringSize(&total, material.name)
            || !AddSize(&total, 10u * sizeof(float) + 4u * sizeof(std::int32_t)))
            return false;
    }
    for (const StaticMeshTextureData& texture : asset.textures) {
        if (!AddStringSize(&total, texture.name)
            || !AddSize(&total, 2u * sizeof(std::uint32_t)
                               + sizeof(std::uint64_t)
                               + static_cast<std::uint64_t>(texture.rgba.size())))
            return false;
    }
    for (const StaticMeshSubMeshData& subMesh : asset.subMeshes) {
        if (!AddSize(&total, sizeof(std::int32_t) + 3u * sizeof(std::uint64_t))
            || !AddSize(&total, static_cast<std::uint64_t>(subMesh.vertices.size())
                                    * sizeof(float))
            || !AddSize(&total, static_cast<std::uint64_t>(subMesh.indices.size())
                                    * sizeof(std::uint32_t))
            || !AddSize(&total, static_cast<std::uint64_t>(subMesh.vertexColors.size())
                                    * sizeof(float)))
            return false;
    }
    *size = total;
    return true;
}

bool WritePayload(std::ostream& output, const StaticMeshAssetData& asset) {
    for (float value : asset.minimum) if (!WriteFloat(output, value)) return false;
    for (float value : asset.maximum) if (!WriteFloat(output, value)) return false;
    if (!WriteUnsigned(output, static_cast<std::uint32_t>(asset.materials.size()))
        || !WriteUnsigned(output, static_cast<std::uint32_t>(asset.textures.size()))
        || !WriteUnsigned(output, static_cast<std::uint32_t>(asset.subMeshes.size())))
        return false;

    for (const StaticMeshMaterialData& material : asset.materials) {
        if (!WriteString(output, material.name)) return false;
        for (float value : material.diffuse) if (!WriteFloat(output, value)) return false;
        for (float value : material.specular) if (!WriteFloat(output, value)) return false;
        for (float value : material.emissive) if (!WriteFloat(output, value)) return false;
        if (!WriteFloat(output, material.shininess)
            || !WriteSigned32(output, material.diffuseMap)
            || !WriteSigned32(output, material.normalMap)
            || !WriteSigned32(output, material.specularMap)
            || !WriteSigned32(output, material.emissiveMap))
            return false;
    }

    for (const StaticMeshTextureData& texture : asset.textures) {
        if (!WriteString(output, texture.name)
            || !WriteUnsigned(output, texture.width)
            || !WriteUnsigned(output, texture.height)
            || !WriteUnsigned(output, static_cast<std::uint64_t>(texture.rgba.size())))
            return false;
        output.write(reinterpret_cast<const char*>(texture.rgba.data()),
                     static_cast<std::streamsize>(texture.rgba.size()));
        if (!output) return false;
    }

    for (const StaticMeshSubMeshData& subMesh : asset.subMeshes) {
        if (!WriteSigned32(output, subMesh.material)
            || !WriteUnsigned(output, static_cast<std::uint64_t>(subMesh.vertices.size())))
            return false;
        for (float value : subMesh.vertices) if (!WriteFloat(output, value)) return false;
        if (!WriteUnsigned(output, static_cast<std::uint64_t>(subMesh.indices.size())))
            return false;
        for (std::uint32_t value : subMesh.indices)
            if (!WriteUnsigned(output, value)) return false;
        if (!WriteUnsigned(output, static_cast<std::uint64_t>(subMesh.vertexColors.size())))
            return false;
        for (float value : subMesh.vertexColors)
            if (!WriteFloat(output, value)) return false;
    }
    if (!WriteUnsigned(output, static_cast<std::uint32_t>(asset.collisionType))) return false;
    return true;
}

bool ReadPayload(std::istream& input, StaticMeshAssetData* asset,
                 std::uint32_t assetVersion) {
    for (float& value : asset->minimum) if (!ReadFloat(input, &value)) return false;
    for (float& value : asset->maximum) if (!ReadFloat(input, &value)) return false;
    std::uint32_t materialCount = 0;
    std::uint32_t textureCount = 0;
    std::uint32_t subMeshCount = 0;
    if (!ReadUnsigned(input, &materialCount)
        || !ReadUnsigned(input, &textureCount)
        || !ReadUnsigned(input, &subMeshCount)
        || materialCount > kMaximumCollectionEntries
        || textureCount > kMaximumCollectionEntries
        || subMeshCount > kMaximumCollectionEntries)
        return false;

    asset->materials.resize(materialCount);
    for (StaticMeshMaterialData& material : asset->materials) {
        if (!ReadString(input, &material.name)) return false;
        for (float& value : material.diffuse) if (!ReadFloat(input, &value)) return false;
        for (float& value : material.specular) if (!ReadFloat(input, &value)) return false;
        for (float& value : material.emissive) if (!ReadFloat(input, &value)) return false;
        if (!ReadFloat(input, &material.shininess)
            || !ReadSigned32(input, &material.diffuseMap)
            || !ReadSigned32(input, &material.normalMap)
            || !ReadSigned32(input, &material.specularMap)
            || !ReadSigned32(input, &material.emissiveMap))
            return false;
    }

    asset->textures.resize(textureCount);
    for (StaticMeshTextureData& texture : asset->textures) {
        std::uint64_t byteCount = 0;
        if (!ReadString(input, &texture.name)
            || !ReadUnsigned(input, &texture.width)
            || !ReadUnsigned(input, &texture.height)
            || !ReadUnsigned(input, &byteCount)
            || byteCount > kMaximumTextureBytes
            || byteCount > static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max()))
            return false;
        texture.rgba.resize(static_cast<std::size_t>(byteCount));
        input.read(reinterpret_cast<char*>(texture.rgba.data()),
                   static_cast<std::streamsize>(texture.rgba.size()));
        if (!input) return false;
    }

    asset->subMeshes.resize(subMeshCount);
    for (StaticMeshSubMeshData& subMesh : asset->subMeshes) {
        std::uint64_t vertexFloatCount = 0;
        std::uint64_t indexCount = 0;
        if (!ReadSigned32(input, &subMesh.material)
            || !ReadUnsigned(input, &vertexFloatCount)
            || vertexFloatCount > kMaximumElementCount
            || vertexFloatCount > static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max()))
            return false;
        subMesh.vertices.resize(static_cast<std::size_t>(vertexFloatCount));
        for (float& value : subMesh.vertices) if (!ReadFloat(input, &value)) return false;
        if (!ReadUnsigned(input, &indexCount)
            || indexCount > kMaximumElementCount
            || indexCount > static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max()))
            return false;
        subMesh.indices.resize(static_cast<std::size_t>(indexCount));
        for (std::uint32_t& value : subMesh.indices)
            if (!ReadUnsigned(input, &value)) return false;
        if (assetVersion >= 2u) {
            std::uint64_t colorFloatCount = 0;
            if (!ReadUnsigned(input, &colorFloatCount)
                || colorFloatCount > kMaximumElementCount
                || colorFloatCount > static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::max()))
                return false;
            subMesh.vertexColors.resize(static_cast<std::size_t>(colorFloatCount));
            for (float& value : subMesh.vertexColors)
                if (!ReadFloat(input, &value)) return false;
        }
    }
    if (assetVersion >= 3u) {
        std::uint32_t collisionType = 0;
        if (!ReadUnsigned(input, &collisionType)
            || collisionType > static_cast<std::uint32_t>(StaticMeshCollisionType::TriangleMesh))
            return false;
        asset->collisionType = static_cast<StaticMeshCollisionType>(collisionType);
    }
    return true;
}

std::string LowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

void FlipRows(std::vector<std::uint8_t>& rgba, std::uint32_t width,
              std::uint32_t height) {
    const std::size_t row = static_cast<std::size_t>(width) * 4u;
    for (std::uint32_t y = 0; y < height / 2u; ++y) {
        std::swap_ranges(rgba.begin() + static_cast<std::ptrdiff_t>(y * row),
                         rgba.begin() + static_cast<std::ptrdiff_t>((y + 1u) * row),
                         rgba.begin() + static_cast<std::ptrdiff_t>(
                             (height - 1u - y) * row));
    }
}

bool DecodeTga(const std::filesystem::path& path, StaticMeshTextureData* texture) {
    std::ifstream input(path, std::ios::binary);
    std::array<std::uint8_t, 18> header{};
    input.read(reinterpret_cast<char*>(header.data()),
               static_cast<std::streamsize>(header.size()));
    if (!input || header[2] != 2 || (header[16] != 24 && header[16] != 32))
        return false;
    const std::uint32_t width = header[12] | (static_cast<std::uint32_t>(header[13]) << 8u);
    const std::uint32_t height = header[14] | (static_cast<std::uint32_t>(header[15]) << 8u);
    if (width == 0 || height == 0) return false;
    input.seekg(header[0], std::ios::cur);
    const std::uint32_t channels = header[16] / 8u;
    const std::uint64_t rawSize = static_cast<std::uint64_t>(width) * height * channels;
    if (rawSize > kMaximumTextureBytes) return false;
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(rawSize));
    input.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    if (!input) return false;

    texture->width = width;
    texture->height = height;
    texture->rgba.resize(static_cast<std::size_t>(width) * height * 4u);
    const bool topOrigin = (header[17] & 0x20u) != 0;
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint32_t sourceY = topOrigin ? height - 1u - y : y;
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t source =
                (static_cast<std::size_t>(sourceY) * width + x) * channels;
            const std::size_t destination =
                (static_cast<std::size_t>(y) * width + x) * 4u;
            texture->rgba[destination] = raw[source + 2u];
            texture->rgba[destination + 1u] = raw[source + 1u];
            texture->rgba[destination + 2u] = raw[source];
            texture->rgba[destination + 3u] =
                channels == 4u ? raw[source + 3u] : 255u;
        }
    }
    return true;
}

bool DecodeTextureFile(const std::filesystem::path& path,
                       StaticMeshTextureData* texture) {
    if (!texture) return false;
    try {
        const std::string extension = LowerExtension(path);
        if (extension == ".tga") return DecodeTga(path, texture);
        image::Image decoded = extension == ".png"
            ? image::DecodePNG(path.string())
            : image::DecodeJPEG(path.string());
        if (decoded.width <= 0 || decoded.height <= 0 || decoded.rgba.empty())
            return false;
        texture->width = static_cast<std::uint32_t>(decoded.width);
        texture->height = static_cast<std::uint32_t>(decoded.height);
        texture->rgba.assign(decoded.rgba.begin(), decoded.rgba.end());
        FlipRows(texture->rgba, texture->width, texture->height);
        return true;
    } catch (...) {
        return false;
    }
}

bool DecodeEmbeddedTexture(const aiTexture* embedded,
                           StaticMeshTextureData* texture) {
    if (!embedded || !texture) return false;
    try {
        if (embedded->mHeight == 0) {
            const auto* bytes =
                reinterpret_cast<const unsigned char*>(embedded->pcData);
            const std::size_t size = embedded->mWidth;
            std::string hint = embedded->achFormatHint;
            std::transform(hint.begin(), hint.end(), hint.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            image::Image decoded;
            if (hint == "png"
                || (size > 3 && bytes[0] == 0x89 && bytes[1] == 0x50))
                decoded = image::DecodePNGFromMemeory(bytes, size);
            else if (hint == "jpg" || hint == "jpeg"
                     || (size > 2 && bytes[0] == 0xff && bytes[1] == 0xd8))
                decoded = image::DecodeJPEGFromMemory(bytes, size);
            else
                return false;
            texture->width = static_cast<std::uint32_t>(decoded.width);
            texture->height = static_cast<std::uint32_t>(decoded.height);
            texture->rgba.assign(decoded.rgba.begin(), decoded.rgba.end());
            FlipRows(texture->rgba, texture->width, texture->height);
            return true;
        }

        texture->width = embedded->mWidth;
        texture->height = embedded->mHeight;
        texture->rgba.resize(static_cast<std::size_t>(texture->width)
                             * texture->height * 4u);
        for (std::size_t i = 0;
             i < static_cast<std::size_t>(texture->width) * texture->height; ++i) {
            const aiTexel& pixel = embedded->pcData[i];
            texture->rgba[i * 4u] = pixel.r;
            texture->rgba[i * 4u + 1u] = pixel.g;
            texture->rgba[i * 4u + 2u] = pixel.b;
            texture->rgba[i * 4u + 3u] = pixel.a;
        }
        FlipRows(texture->rgba, texture->width, texture->height);
        return true;
    } catch (...) {
        return false;
    }
}

glm::mat4 ToGlm(const aiMatrix4x4& value) {
    return glm::mat4(value.a1, value.b1, value.c1, value.d1,
                     value.a2, value.b2, value.c2, value.d2,
                     value.a3, value.b3, value.c3, value.d3,
                     value.a4, value.b4, value.c4, value.d4);
}

bool ValidMapIndex(std::int32_t index, std::size_t textureCount) {
    return index == -1
        || (index >= 0 && static_cast<std::size_t>(index) < textureCount);
}

} // namespace

bool ValidateStaticMeshAsset(const StaticMeshAssetData& asset, std::string* error) {
    if (asset.header.type != AssetType::StaticMesh || !asset.header.id.Valid()
        || asset.header.assetVersion != kStaticMeshAssetVersion) {
        SetError(error, "Static mesh identity, type, or version is invalid.");
        return false;
    }
    for (float value : asset.minimum)
        if (!std::isfinite(value)) {
            SetError(error, "Static mesh minimum bounds are invalid.");
            return false;
        }
    for (float value : asset.maximum)
        if (!std::isfinite(value)) {
            SetError(error, "Static mesh maximum bounds are invalid.");
            return false;
        }
    if (asset.subMeshes.empty()) {
        SetError(error, "Static mesh has no drawable submeshes.");
        return false;
    }
    if (asset.materials.size() > kMaximumCollectionEntries
        || asset.textures.size() > kMaximumCollectionEntries
        || asset.subMeshes.size() > kMaximumCollectionEntries) {
        SetError(error, "Static mesh contains too many records.");
        return false;
    }

    for (const StaticMeshTextureData& texture : asset.textures) {
        const std::uint64_t expected =
            static_cast<std::uint64_t>(texture.width) * texture.height * 4u;
        if (texture.width == 0 || texture.height == 0
            || expected != texture.rgba.size() || expected > kMaximumTextureBytes) {
            SetError(error, "Static mesh contains an invalid embedded texture.");
            return false;
        }
    }
    for (const StaticMeshMaterialData& material : asset.materials) {
        if (!std::isfinite(material.shininess)
            || !ValidMapIndex(material.diffuseMap, asset.textures.size())
            || !ValidMapIndex(material.normalMap, asset.textures.size())
            || !ValidMapIndex(material.specularMap, asset.textures.size())
            || !ValidMapIndex(material.emissiveMap, asset.textures.size())) {
            SetError(error, "Static mesh contains an invalid material.");
            return false;
        }
    }
    for (const StaticMeshSubMeshData& subMesh : asset.subMeshes) {
        if (subMesh.vertices.empty()
            || subMesh.vertices.size() % kStaticMeshVertexStride != 0
            || subMesh.indices.empty() || subMesh.indices.size() % 3u != 0
            || (subMesh.material < -1
                || (subMesh.material >= 0
                    && static_cast<std::size_t>(subMesh.material)
                           >= asset.materials.size()))) {
            SetError(error, "Static mesh contains invalid geometry or material indices.");
            return false;
        }
        const std::size_t vertexCount =
            subMesh.vertices.size() / kStaticMeshVertexStride;
        if (!subMesh.vertexColors.empty()
            && subMesh.vertexColors.size() != vertexCount * 4u) {
            SetError(error, "Static mesh vertex paint count does not match its geometry.");
            return false;
        }
        for (float value : subMesh.vertices)
            if (!std::isfinite(value)) {
                SetError(error, "Static mesh contains a non-finite vertex value.");
                return false;
            }
        for (float value : subMesh.vertexColors)
            if (!std::isfinite(value) || value < 0.0f || value > 1.0f) {
                SetError(error, "Static mesh contains an invalid vertex paint value.");
                return false;
            }
        for (std::uint32_t index : subMesh.indices)
            if (index >= vertexCount) {
                SetError(error, "Static mesh contains an out-of-range vertex index.");
                return false;
            }
    }
    SetError(error, {});
    return true;
}

bool SaveStaticMeshAsset(const std::string& path, StaticMeshAssetData asset,
                         std::string* error) {
    asset.header.type = AssetType::StaticMesh;
    asset.header.assetVersion = kStaticMeshAssetVersion;
    std::uint64_t payloadSize = 0;
    if (!ValidateStaticMeshAsset(asset, error) || !PayloadSize(asset, &payloadSize)) {
        if (error && error->empty()) *error = "Static mesh payload is too large.";
        return false;
    }
    asset.header.payloadSize = payloadSize;

    const std::filesystem::path destination(path);
    std::error_code ec;
    if (destination.has_parent_path())
        std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
        SetError(error, "Could not create static mesh directory: " + ec.message());
        return false;
    }
    const std::filesystem::path temporary = destination.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output
            || !WriteNativeAssetHeader(output, asset.header, error)
            || !WritePayload(output, asset)) {
            std::filesystem::remove(temporary, ec);
            if (error && error->empty()) *error = "Could not write static mesh payload.";
            return false;
        }
    }

    const std::filesystem::path backup = destination.string() + ".bak";
    std::filesystem::remove(backup, ec);
    ec.clear();
    if (std::filesystem::exists(destination, ec)) {
        std::filesystem::rename(destination, backup, ec);
        if (ec) {
            std::filesystem::remove(temporary);
            SetError(error, "Could not replace static mesh: " + ec.message());
            return false;
        }
    }
    ec.clear();
    std::filesystem::rename(temporary, destination, ec);
    if (ec) {
        std::error_code rollback;
        if (std::filesystem::exists(backup, rollback))
            std::filesystem::rename(backup, destination, rollback);
        SetError(error, "Could not commit static mesh: " + ec.message());
        return false;
    }
    std::filesystem::remove(backup, ec);
    SetError(error, {});
    return true;
}

bool LoadStaticMeshAsset(const std::string& path, StaticMeshAssetData* asset,
                         std::string* error) {
    if (!asset) {
        SetError(error, "Static mesh output is null.");
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        SetError(error, "Could not open static mesh: " + path);
        return false;
    }

    StaticMeshAssetData loaded;
    if (!ReadNativeAssetHeader(input, &loaded.header, error)) return false;
    if (loaded.header.type != AssetType::StaticMesh
        || loaded.header.assetVersion < 1u
        || loaded.header.assetVersion > kStaticMeshAssetVersion) {
        SetError(error, "Native asset is not a supported static mesh.");
        return false;
    }
    const std::streampos payloadStart = input.tellg();
    if (!ReadPayload(input, &loaded, loaded.header.assetVersion)) {
        SetError(error, "Static mesh payload is invalid or truncated.");
        return false;
    }
    const std::streampos payloadEnd = input.tellg();
    if (payloadStart < 0 || payloadEnd < payloadStart
        || static_cast<std::uint64_t>(payloadEnd - payloadStart)
               != loaded.header.payloadSize) {
        SetError(error, "Static mesh payload size does not match its header.");
        return false;
    }
    // Version 1 had the same geometry payload but no vertex-paint array.
    // Upgrade it in memory; the next explicit save writes the version-2 form.
    loaded.header.assetVersion = kStaticMeshAssetVersion;
    if (!ValidateStaticMeshAsset(loaded, error)) return false;
    *asset = std::move(loaded);
    SetError(error, {});
    return true;
}

std::uint64_t HashAssetSourceFile(const std::string& path, std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        SetError(error, "Could not open source file for hashing: " + path);
        return 0;
    }
    std::uint64_t hash = 14695981039346656037ull;
    std::array<char, 64 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize read = input.gcount();
        for (std::streamsize i = 0; i < read; ++i) {
            hash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)]);
            hash *= 1099511628211ull;
        }
    }
    if (!input.eof()) {
        SetError(error, "Could not finish hashing source file: " + path);
        return 0;
    }
    SetError(error, {});
    return hash;
}

bool ImportStaticMeshSource(const std::string& sourcePath,
                            const StaticMeshImportOptions& options,
                            StaticMeshAssetData* asset,
                            StaticMeshImportResult* result,
                            std::string* error) {
    if (!asset || !std::isfinite(options.uniformScale)
        || options.uniformScale <= 0.0f) {
        SetError(error, "Static mesh import settings are invalid.");
        return false;
    }

    unsigned flags = aiProcess_Triangulate | aiProcess_GenUVCoords
        | aiProcess_SortByPType | aiProcess_ValidateDataStructure;
    if (options.generateSmoothNormals) flags |= aiProcess_GenSmoothNormals;
    if (options.generateTangents) flags |= aiProcess_CalcTangentSpace;
    if (options.joinIdenticalVertices) flags |= aiProcess_JoinIdenticalVertices;
    if (options.flipUVs) flags |= aiProcess_FlipUVs;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(sourcePath, flags);
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        SetError(error, "Static mesh import failed for '" + sourcePath
            + "': " + importer.GetErrorString());
        return false;
    }
    for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
        if (scene->mMeshes[i]->HasBones()) {
            SetError(error, "The source contains bones and must be imported as a "
                "skeletal mesh: " + sourcePath);
            return false;
        }
    }

    StaticMeshAssetData imported;
    imported.header.type = AssetType::StaticMesh;
    imported.header.id = AssetHandle::Generate();
    imported.header.assetVersion = kStaticMeshAssetVersion;
    imported.header.importerVersion = kStaticMeshImporterVersion;
    std::string hashError;
    imported.header.sourceHash = HashAssetSourceFile(sourcePath, &hashError);
    if (!hashError.empty()) {
        SetError(error, hashError);
        return false;
    }

    const std::filesystem::path sourceDirectory =
        std::filesystem::path(sourcePath).parent_path();
    std::map<std::string, int> textureCache;
    auto loadTexture = [&](const aiMaterial* material, aiTextureType type) -> int {
        if (!material || material->GetTextureCount(type) == 0) return -1;
        aiString assimpPath;
        if (material->GetTexture(type, 0, &assimpPath) != AI_SUCCESS) return -1;
        const std::string key = assimpPath.C_Str();
        const auto cached = textureCache.find(key);
        if (cached != textureCache.end()) return cached->second;

        StaticMeshTextureData texture;
        texture.name = std::filesystem::path(key).filename().string();
        bool decoded = false;
        if (const aiTexture* embedded = scene->GetEmbeddedTexture(key.c_str()))
            decoded = DecodeEmbeddedTexture(embedded, &texture);
        else {
            std::filesystem::path texturePath(key);
            if (texturePath.is_relative()) texturePath = sourceDirectory / texturePath;
            decoded = DecodeTextureFile(texturePath.lexically_normal(), &texture);
        }
        const int index = decoded
            ? static_cast<int>(imported.textures.size()) : -1;
        if (decoded) imported.textures.push_back(std::move(texture));
        textureCache.emplace(key, index);
        return index;
    };

    imported.materials.reserve(scene->mNumMaterials);
    for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
        const aiMaterial* source = scene->mMaterials[i];
        StaticMeshMaterialData material;
        aiString name;
        if (source->Get(AI_MATKEY_NAME, name) == AI_SUCCESS)
            material.name = name.C_Str();
        aiColor3D color;
        if (source->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
            material.diffuse = {{color.r, color.g, color.b}};
        if (source->Get(AI_MATKEY_COLOR_SPECULAR, color) == AI_SUCCESS)
            material.specular = {{color.r, color.g, color.b}};
        if (source->Get(AI_MATKEY_COLOR_EMISSIVE, color) == AI_SUCCESS)
            material.emissive = {{color.r, color.g, color.b}};
        float shininess = 0.0f;
        if (source->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS
            && shininess > 0.0f)
            material.shininess = shininess;
        material.diffuseMap = loadTexture(source, aiTextureType_DIFFUSE);
        material.specularMap = loadTexture(source, aiTextureType_SPECULAR);
        material.emissiveMap = loadTexture(source, aiTextureType_EMISSIVE);
        material.normalMap = loadTexture(source, aiTextureType_NORMALS);
        if (material.normalMap < 0)
            material.normalMap = loadTexture(source, aiTextureType_HEIGHT);
        imported.materials.push_back(std::move(material));
    }

    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());
    std::size_t vertexTotal = 0;
    std::size_t triangleTotal = 0;
    std::function<void(const aiNode*, const glm::mat4&)> visit =
        [&](const aiNode* node, const glm::mat4& parent) {
            const glm::mat4 transform = parent * ToGlm(node->mTransformation);
            const glm::mat3 normalTransform =
                glm::mat3(glm::transpose(glm::inverse(transform)));
            for (unsigned i = 0; i < node->mNumMeshes; ++i) {
                const aiMesh* source = scene->mMeshes[node->mMeshes[i]];
                StaticMeshSubMeshData subMesh;
                subMesh.material = source->mMaterialIndex < imported.materials.size()
                    ? static_cast<std::int32_t>(source->mMaterialIndex) : -1;
                subMesh.vertices.reserve(
                    static_cast<std::size_t>(source->mNumVertices)
                    * kStaticMeshVertexStride);
                for (unsigned vertexIndex = 0;
                     vertexIndex < source->mNumVertices; ++vertexIndex) {
                    const glm::vec4 transformed = transform * glm::vec4(
                        source->mVertices[vertexIndex].x,
                        source->mVertices[vertexIndex].y,
                        source->mVertices[vertexIndex].z, 1.0f);
                    const glm::vec3 position =
                        glm::vec3(transformed) * options.uniformScale;
                    glm::vec3 normal(0.0f, 1.0f, 0.0f);
                    glm::vec3 tangent(1.0f, 0.0f, 0.0f);
                    glm::vec2 uv(0.0f);
                    if (source->HasNormals()) {
                        normal = glm::normalize(normalTransform * glm::vec3(
                            source->mNormals[vertexIndex].x,
                            source->mNormals[vertexIndex].y,
                            source->mNormals[vertexIndex].z));
                    }
                    if (source->HasTangentsAndBitangents()) {
                        tangent = glm::normalize(normalTransform * glm::vec3(
                            source->mTangents[vertexIndex].x,
                            source->mTangents[vertexIndex].y,
                            source->mTangents[vertexIndex].z));
                    }
                    if (source->HasTextureCoords(0)) {
                        uv = {source->mTextureCoords[0][vertexIndex].x,
                              source->mTextureCoords[0][vertexIndex].y};
                    }
                    subMesh.vertices.insert(subMesh.vertices.end(), {
                        position.x, position.y, position.z,
                        normal.x, normal.y, normal.z,
                        uv.x, uv.y, tangent.x, tangent.y, tangent.z});
                    minimum = glm::min(minimum, position);
                    maximum = glm::max(maximum, position);
                }
                subMesh.indices.reserve(
                    static_cast<std::size_t>(source->mNumFaces) * 3u);
                for (unsigned faceIndex = 0;
                     faceIndex < source->mNumFaces; ++faceIndex) {
                    const aiFace& face = source->mFaces[faceIndex];
                    if (face.mNumIndices != 3) continue;
                    subMesh.indices.insert(subMesh.indices.end(), {
                        face.mIndices[0], face.mIndices[1], face.mIndices[2]});
                }
                if (!subMesh.vertices.empty() && !subMesh.indices.empty()) {
                    vertexTotal += source->mNumVertices;
                    triangleTotal += subMesh.indices.size() / 3u;
                    imported.subMeshes.push_back(std::move(subMesh));
                }
            }
            for (unsigned i = 0; i < node->mNumChildren; ++i)
                visit(node->mChildren[i], transform);
        };
    visit(scene->mRootNode, glm::mat4(1.0f));

    if (imported.subMeshes.empty()) {
        SetError(error, "Static mesh source contains no triangle geometry: " + sourcePath);
        return false;
    }
    imported.minimum = {{minimum.x, minimum.y, minimum.z}};
    imported.maximum = {{maximum.x, maximum.y, maximum.z}};
    if (!ValidateStaticMeshAsset(imported, error)) return false;

    if (result) {
        result->id = imported.header.id;
        result->sourceHash = imported.header.sourceHash;
        result->subMeshCount = imported.subMeshes.size();
        result->vertexCount = vertexTotal;
        result->triangleCount = triangleTotal;
        result->embeddedTextureCount = imported.textures.size();
    }
    *asset = std::move(imported);
    SetError(error, {});
    return true;
}

bool ImportStaticMeshToAsset(const std::string& sourcePath,
                             const std::string& destinationPath,
                             const std::string& contentRoot,
                             const StaticMeshImportOptions& options,
                             AssetRegistry* registry,
                             StaticMeshImportResult* result,
                             std::string* error) {
    StaticMeshAssetData imported;
    StaticMeshImportResult importResult;
    if (!ImportStaticMeshSource(
            sourcePath, options, &imported, &importResult, error))
        return false;

    NativeAssetHeader existing;
    std::error_code ec;
    if (std::filesystem::is_regular_file(destinationPath, ec)) {
        std::string headerError;
        if (!ReadNativeAssetHeaderFile(destinationPath, &existing, &headerError)
            || existing.type != AssetType::StaticMesh) {
            SetError(error, "Cannot reimport over an incompatible asset: "
                + destinationPath);
            return false;
        }
        imported.header.id = existing.id;
        importResult.id = existing.id;
    }

    AssetRegistry updated;
    AssetRegistryEntry entry;
    if (registry) {
        updated = *registry;
        ec.clear();
        const std::filesystem::path absoluteContent =
            std::filesystem::absolute(contentRoot, ec).lexically_normal();
        if (ec) {
            SetError(error, "Could not resolve the Content directory.");
            return false;
        }
        const std::filesystem::path absoluteDestination =
            std::filesystem::absolute(destinationPath, ec).lexically_normal();
        const std::filesystem::path relative =
            absoluteDestination.lexically_relative(absoluteContent);
        const bool escapesContent = relative.empty() || relative.is_absolute()
            || (relative.begin() != relative.end()
                && relative.begin()->generic_string() == "..");
        if (ec || escapesContent) {
            SetError(error, "Static mesh destination must be inside Content.");
            return false;
        }
        entry.id = imported.header.id;
        entry.type = AssetType::StaticMesh;
        entry.virtualPath = AssetRegistry::NormalizeVirtualPath(
            relative.generic_string());
        entry.sourcePath =
            std::filesystem::absolute(sourcePath, ec).lexically_normal().string();
        if (ec) entry.sourcePath = sourcePath;
        entry.sourceHash = imported.header.sourceHash;
        entry.importerVersion = imported.header.importerVersion;
        entry.dependencies = imported.header.dependencies;
        if (!updated.Register(entry, error)) return false;
    }

    if (!SaveStaticMeshAsset(destinationPath, std::move(imported), error))
        return false;
    if (registry) {
        if (!updated.Save(AssetRegistry::DefaultRegistryPath(contentRoot), error))
            return false;
        *registry = std::move(updated);
    }
    importResult.outputPath = destinationPath;
    if (result) *result = std::move(importResult);
    SetError(error, {});
    return true;
}

} // namespace engine
