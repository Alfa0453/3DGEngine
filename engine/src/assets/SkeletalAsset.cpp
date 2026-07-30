#include "engine/assets/SkeletalAsset.h"

#include "engine/assets/AssetRegistry.h"
#include "engine/graphics/ImageDecode.h"
#include "engine/graphics/SkinnedModel.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace engine {
namespace {

constexpr std::uint32_t kMaximumRecords = 100000;
constexpr std::uint64_t kMaximumElements = 500000000ull;
constexpr std::uint64_t kMaximumTextureBytes = 1024ull * 1024ull * 1024ull;

void SetError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

template<typename T>
bool WriteU(std::ostream& output, T value) {
    static_assert(std::is_unsigned<T>::value, "unsigned type required");
    for (std::size_t i = 0; i < sizeof(T); ++i)
        output.put(static_cast<char>((value >> (i * 8u)) & static_cast<T>(0xffu)));
    return static_cast<bool>(output);
}

template<typename T>
bool ReadU(std::istream& input, T* value) {
    static_assert(std::is_unsigned<T>::value, "unsigned type required");
    if (!value) return false;
    *value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        const int byte = input.get();
        if (byte == std::char_traits<char>::eof()) return false;
        *value |= static_cast<T>(static_cast<unsigned char>(byte)) << (i * 8u);
    }
    return true;
}

bool WriteI(std::ostream& output, std::int32_t value) {
    std::uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    return WriteU(output, raw);
}

bool ReadI(std::istream& input, std::int32_t* value) {
    std::uint32_t raw = 0;
    if (!value || !ReadU(input, &raw)) return false;
    std::memcpy(value, &raw, sizeof(raw));
    return true;
}

bool WriteF(std::ostream& output, float value) {
    std::uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    return WriteU(output, raw);
}

bool ReadF(std::istream& input, float* value) {
    std::uint32_t raw = 0;
    if (!value || !ReadU(input, &raw)) return false;
    std::memcpy(value, &raw, sizeof(raw));
    return true;
}

bool WriteString(std::ostream& output, const std::string& value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()
        || !WriteU(output, static_cast<std::uint32_t>(value.size())))
        return false;
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    return static_cast<bool>(output);
}

bool ReadString(std::istream& input, std::string* value) {
    std::uint32_t size = 0;
    if (!value || !ReadU(input, &size) || size > 1024u * 1024u) return false;
    value->resize(size);
    input.read(value->data(), static_cast<std::streamsize>(size));
    return static_cast<bool>(input);
}

bool WriteHandle(std::ostream& output, AssetHandle value) {
    return WriteU(output, value.high) && WriteU(output, value.low);
}

bool ReadHandle(std::istream& input, AssetHandle* value) {
    return value && ReadU(input, &value->high) && ReadU(input, &value->low)
        && value->Valid();
}

bool WriteMat4(std::ostream& output, const glm::mat4& value) {
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!WriteF(output, value[column][row])) return false;
    return true;
}

bool ReadMat4(std::istream& input, glm::mat4* value) {
    if (!value) return false;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!ReadF(input, &(*value)[column][row])) return false;
    return true;
}

bool WriteSkeleton(std::ostream& output, const Skeleton& skeleton) {
    if (skeleton.bones.size() > std::numeric_limits<std::uint32_t>::max()
        || !WriteMat4(output, skeleton.globalInverse)
        || !WriteU(output, static_cast<std::uint32_t>(skeleton.bones.size())))
        return false;
    for (const Bone& bone : skeleton.bones) {
        if (!WriteString(output, bone.name)
            || !WriteI(output, static_cast<std::int32_t>(bone.parent))
            || !WriteMat4(output, bone.offset)
            || !WriteMat4(output, bone.localBind))
            return false;
    }
    return true;
}

bool ReadSkeleton(std::istream& input, Skeleton* skeleton) {
    std::uint32_t count = 0;
    if (!skeleton || !ReadMat4(input, &skeleton->globalInverse)
        || !ReadU(input, &count) || count > SkinnedModel::kMaxBones)
        return false;
    skeleton->bones.resize(count);
    for (Bone& bone : skeleton->bones) {
        std::int32_t parent = -1;
        if (!ReadString(input, &bone.name) || !ReadI(input, &parent)
            || !ReadMat4(input, &bone.offset)
            || !ReadMat4(input, &bone.localBind))
            return false;
        bone.parent = parent;
    }
    return true;
}

bool WriteChannel(std::ostream& output, const BoneChannel& channel) {
    if (channel.positions.size() > std::numeric_limits<std::uint32_t>::max()
        || channel.rotations.size() > std::numeric_limits<std::uint32_t>::max()
        || channel.scales.size() > std::numeric_limits<std::uint32_t>::max()
        || !WriteU(output, static_cast<std::uint32_t>(channel.positions.size())))
        return false;
    for (const VecKey& key : channel.positions)
        if (!WriteF(output, key.time) || !WriteF(output, key.value.x)
            || !WriteF(output, key.value.y) || !WriteF(output, key.value.z))
            return false;
    if (!WriteU(output, static_cast<std::uint32_t>(channel.rotations.size())))
        return false;
    for (const QuatKey& key : channel.rotations)
        if (!WriteF(output, key.time) || !WriteF(output, key.value.w)
            || !WriteF(output, key.value.x) || !WriteF(output, key.value.y)
            || !WriteF(output, key.value.z))
            return false;
    if (!WriteU(output, static_cast<std::uint32_t>(channel.scales.size())))
        return false;
    for (const VecKey& key : channel.scales)
        if (!WriteF(output, key.time) || !WriteF(output, key.value.x)
            || !WriteF(output, key.value.y) || !WriteF(output, key.value.z))
            return false;
    return true;
}

bool ReadChannel(std::istream& input, BoneChannel* channel) {
    std::uint32_t positions = 0, rotations = 0, scales = 0;
    if (!channel || !ReadU(input, &positions) || positions > kMaximumRecords)
        return false;
    channel->positions.resize(positions);
    for (VecKey& key : channel->positions)
        if (!ReadF(input, &key.time) || !ReadF(input, &key.value.x)
            || !ReadF(input, &key.value.y) || !ReadF(input, &key.value.z))
            return false;
    if (!ReadU(input, &rotations) || rotations > kMaximumRecords) return false;
    channel->rotations.resize(rotations);
    for (QuatKey& key : channel->rotations)
        if (!ReadF(input, &key.time) || !ReadF(input, &key.value.w)
            || !ReadF(input, &key.value.x) || !ReadF(input, &key.value.y)
            || !ReadF(input, &key.value.z))
            return false;
    if (!ReadU(input, &scales) || scales > kMaximumRecords) return false;
    channel->scales.resize(scales);
    for (VecKey& key : channel->scales)
        if (!ReadF(input, &key.time) || !ReadF(input, &key.value.x)
            || !ReadF(input, &key.value.y) || !ReadF(input, &key.value.z))
            return false;
    return true;
}

bool WriteClips(std::ostream& output,
                const std::vector<NamedAnimationClipData>& clips) {
    if (clips.size() > std::numeric_limits<std::uint32_t>::max()
        || !WriteU(output, static_cast<std::uint32_t>(clips.size())))
        return false;
    for (const NamedAnimationClipData& clip : clips) {
        if (!WriteString(output, clip.animation.name)
            || !WriteF(output, clip.animation.duration)
            || !WriteF(output, clip.animation.ticksPerSecond)
            || clip.animation.channels.size() != clip.channelBoneNames.size()
            || clip.animation.channels.size()
                   > std::numeric_limits<std::uint32_t>::max()
            || !WriteU(output, static_cast<std::uint32_t>(
                clip.animation.channels.size())))
            return false;
        for (std::size_t i = 0; i < clip.animation.channels.size(); ++i)
            if (!WriteString(output, clip.channelBoneNames[i])
                || !WriteChannel(output, clip.animation.channels[i]))
                return false;
    }
    return true;
}

bool ReadClips(std::istream& input, std::vector<NamedAnimationClipData>* clips) {
    std::uint32_t clipCount = 0;
    if (!clips || !ReadU(input, &clipCount) || clipCount > kMaximumRecords)
        return false;
    clips->resize(clipCount);
    for (NamedAnimationClipData& clip : *clips) {
        std::uint32_t channelCount = 0;
        if (!ReadString(input, &clip.animation.name)
            || !ReadF(input, &clip.animation.duration)
            || !ReadF(input, &clip.animation.ticksPerSecond)
            || !ReadU(input, &channelCount)
            || channelCount > SkinnedModel::kMaxBones)
            return false;
        clip.animation.channels.resize(channelCount);
        clip.channelBoneNames.resize(channelCount);
        for (std::uint32_t i = 0; i < channelCount; ++i)
            if (!ReadString(input, &clip.channelBoneNames[i])
                || !ReadChannel(input, &clip.animation.channels[i]))
                return false;
    }
    return true;
}

bool WriteMaterials(std::ostream& output,
                    const std::vector<StaticMeshMaterialData>& materials) {
    if (materials.size() > std::numeric_limits<std::uint32_t>::max()
        || !WriteU(output, static_cast<std::uint32_t>(materials.size())))
        return false;
    for (const auto& material : materials) {
        if (!WriteString(output, material.name)) return false;
        for (float value : material.diffuse) if (!WriteF(output, value)) return false;
        for (float value : material.specular) if (!WriteF(output, value)) return false;
        for (float value : material.emissive) if (!WriteF(output, value)) return false;
        if (!WriteF(output, material.shininess)
            || !WriteI(output, material.diffuseMap)
            || !WriteI(output, material.normalMap)
            || !WriteI(output, material.specularMap)
            || !WriteI(output, material.emissiveMap))
            return false;
    }
    return true;
}

bool ReadMaterials(std::istream& input,
                   std::vector<StaticMeshMaterialData>* materials) {
    std::uint32_t count = 0;
    if (!materials || !ReadU(input, &count) || count > kMaximumRecords) return false;
    materials->resize(count);
    for (auto& material : *materials) {
        if (!ReadString(input, &material.name)) return false;
        for (float& value : material.diffuse) if (!ReadF(input, &value)) return false;
        for (float& value : material.specular) if (!ReadF(input, &value)) return false;
        for (float& value : material.emissive) if (!ReadF(input, &value)) return false;
        if (!ReadF(input, &material.shininess)
            || !ReadI(input, &material.diffuseMap)
            || !ReadI(input, &material.normalMap)
            || !ReadI(input, &material.specularMap)
            || !ReadI(input, &material.emissiveMap))
            return false;
    }
    return true;
}

bool WriteTextures(std::ostream& output,
                   const std::vector<StaticMeshTextureData>& textures) {
    if (textures.size() > std::numeric_limits<std::uint32_t>::max()
        || !WriteU(output, static_cast<std::uint32_t>(textures.size())))
        return false;
    for (const auto& texture : textures) {
        if (!WriteString(output, texture.name) || !WriteU(output, texture.width)
            || !WriteU(output, texture.height)
            || !WriteU(output, static_cast<std::uint64_t>(texture.rgba.size())))
            return false;
        output.write(reinterpret_cast<const char*>(texture.rgba.data()),
                     static_cast<std::streamsize>(texture.rgba.size()));
        if (!output) return false;
    }
    return true;
}

bool ReadTextures(std::istream& input,
                  std::vector<StaticMeshTextureData>* textures) {
    std::uint32_t count = 0;
    if (!textures || !ReadU(input, &count) || count > kMaximumRecords) return false;
    textures->resize(count);
    for (auto& texture : *textures) {
        std::uint64_t bytes = 0;
        if (!ReadString(input, &texture.name) || !ReadU(input, &texture.width)
            || !ReadU(input, &texture.height) || !ReadU(input, &bytes)
            || bytes > kMaximumTextureBytes
            || bytes > std::numeric_limits<std::size_t>::max())
            return false;
        texture.rgba.resize(static_cast<std::size_t>(bytes));
        input.read(reinterpret_cast<char*>(texture.rgba.data()),
                   static_cast<std::streamsize>(texture.rgba.size()));
        if (!input) return false;
    }
    return true;
}

template<typename Writer>
bool SaveNative(const std::string& path, NativeAssetHeader header,
                Writer writer, std::string* error) {
    const std::filesystem::path destination(path);
    const std::filesystem::path payload = destination.string() + ".payload.tmp";
    const std::filesystem::path temporary = destination.string() + ".tmp";
    std::error_code ec;
    if (destination.has_parent_path())
        std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
        SetError(error, "Could not create native asset directory: " + ec.message());
        return false;
    }
    {
        std::ofstream output(payload, std::ios::binary | std::ios::trunc);
        if (!output || !writer(output)) {
            std::filesystem::remove(payload, ec);
            SetError(error, "Could not write native asset payload.");
            return false;
        }
    }
    header.payloadSize = std::filesystem::file_size(payload, ec);
    if (ec) {
        std::filesystem::remove(payload, ec);
        SetError(error, "Could not measure native asset payload.");
        return false;
    }
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        std::ifstream input(payload, std::ios::binary);
        if (!output || !input || !WriteNativeAssetHeader(output, header, error)) {
            std::filesystem::remove(payload, ec);
            std::filesystem::remove(temporary, ec);
            return false;
        }
        output << input.rdbuf();
        if (!output) {
            std::filesystem::remove(payload, ec);
            std::filesystem::remove(temporary, ec);
            SetError(error, "Could not assemble native asset.");
            return false;
        }
    }
    std::filesystem::remove(payload, ec);
    const std::filesystem::path backup = destination.string() + ".bak";
    std::filesystem::remove(backup, ec);
    ec.clear();
    if (std::filesystem::exists(destination, ec)) {
        std::filesystem::rename(destination, backup, ec);
        if (ec) {
            std::filesystem::remove(temporary);
            SetError(error, "Could not replace native asset: " + ec.message());
            return false;
        }
    }
    ec.clear();
    std::filesystem::rename(temporary, destination, ec);
    if (ec) {
        std::error_code rollback;
        if (std::filesystem::exists(backup, rollback))
            std::filesystem::rename(backup, destination, rollback);
        SetError(error, "Could not commit native asset: " + ec.message());
        return false;
    }
    std::filesystem::remove(backup, ec);
    SetError(error, {});
    return true;
}

template<typename Reader>
bool LoadNative(const std::string& path, AssetType expected,
                std::uint32_t version, NativeAssetHeader* header,
                Reader reader, std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input || !ReadNativeAssetHeader(input, header, error)) {
        if (!input) SetError(error, "Could not open native asset: " + path);
        return false;
    }
    if (header->type != expected || header->assetVersion != version) {
        SetError(error, "Native asset type or version is unsupported.");
        return false;
    }
    const std::streampos start = input.tellg();
    if (!reader(input)) {
        SetError(error, "Native asset payload is invalid or truncated.");
        return false;
    }
    const std::streampos end = input.tellg();
    if (start < 0 || end < start
        || static_cast<std::uint64_t>(end - start) != header->payloadSize) {
        SetError(error, "Native asset payload size does not match its header.");
        return false;
    }
    return true;
}

bool FiniteMat4(const glm::mat4& matrix) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (!std::isfinite(matrix[c][r])) return false;
    return true;
}

bool ValidateSkeleton(const Skeleton& skeleton, std::string* error) {
    if (skeleton.bones.empty()
        || skeleton.bones.size() > SkinnedModel::kMaxBones
        || !FiniteMat4(skeleton.globalInverse)) {
        SetError(error, "Skeleton has an invalid bone count or root transform.");
        return false;
    }
    std::unordered_map<std::string, bool> names;
    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        const Bone& bone = skeleton.bones[i];
        if (bone.name.empty() || names.find(bone.name) != names.end()
            || bone.parent >= static_cast<int>(i) || bone.parent < -1
            || !FiniteMat4(bone.offset) || !FiniteMat4(bone.localBind)) {
            SetError(error, "Skeleton contains an invalid or duplicate bone.");
            return false;
        }
        names[bone.name] = true;
    }
    return true;
}

bool ValidateClips(const std::vector<NamedAnimationClipData>& clips,
                   std::string* error) {
    for (const auto& clip : clips) {
        if (clip.animation.name.empty() || !std::isfinite(clip.animation.duration)
            || !std::isfinite(clip.animation.ticksPerSecond)
            || clip.animation.duration < 0.0f
            || clip.animation.ticksPerSecond <= 0.0f
            || clip.animation.channels.size() != clip.channelBoneNames.size()
            || clip.animation.channels.size() > SkinnedModel::kMaxBones) {
            SetError(error, "Animation clip metadata is invalid.");
            return false;
        }
        for (std::size_t i = 0; i < clip.animation.channels.size(); ++i) {
            if (clip.channelBoneNames[i].empty()) {
                SetError(error, "Animation channel has no bone name.");
                return false;
            }
            const BoneChannel& channel = clip.animation.channels[i];
            auto finiteVec = [](const VecKey& key) {
                return std::isfinite(key.time) && std::isfinite(key.value.x)
                    && std::isfinite(key.value.y) && std::isfinite(key.value.z);
            };
            auto finiteQuat = [](const QuatKey& key) {
                return std::isfinite(key.time) && std::isfinite(key.value.w)
                    && std::isfinite(key.value.x) && std::isfinite(key.value.y)
                    && std::isfinite(key.value.z);
            };
            if (!std::all_of(channel.positions.begin(), channel.positions.end(), finiteVec)
                || !std::all_of(channel.scales.begin(), channel.scales.end(), finiteVec)
                || !std::all_of(channel.rotations.begin(), channel.rotations.end(), finiteQuat)) {
                SetError(error, "Animation channel contains non-finite keys.");
                return false;
            }
        }
    }
    return true;
}

bool ValidTextureIndex(std::int32_t index, std::size_t textureCount) {
    return index == -1
        || (index >= 0 && static_cast<std::size_t>(index) < textureCount);
}

bool ValidateSkeletalMeshPayload(const SkeletalMeshAssetData& asset,
                                 std::string* error) {
    if (!asset.header.id.Valid() || !asset.skeletonId.Valid()
        || !ValidateSkeleton(asset.skeleton, error)
        || !ValidateClips(asset.embeddedAnimations, error)
        || asset.subMeshes.empty()
        || asset.materials.size() > kMaximumRecords
        || asset.textures.size() > kMaximumRecords
        || asset.subMeshes.size() > kMaximumRecords) {
        if (error && error->empty())
            *error = "Skeletal mesh identity or payload is invalid.";
        return false;
    }
    for (float value : asset.minimum)
        if (!std::isfinite(value)) {
            SetError(error, "Skeletal mesh minimum bounds are invalid.");
            return false;
        }
    for (float value : asset.maximum)
        if (!std::isfinite(value)) {
            SetError(error, "Skeletal mesh maximum bounds are invalid.");
            return false;
        }
    for (const StaticMeshTextureData& texture : asset.textures) {
        const std::uint64_t expected =
            static_cast<std::uint64_t>(texture.width) * texture.height * 4u;
        if (texture.width == 0 || texture.height == 0
            || expected != texture.rgba.size()
            || expected > kMaximumTextureBytes) {
            SetError(error, "Skeletal mesh contains an invalid embedded texture.");
            return false;
        }
    }
    for (const StaticMeshMaterialData& material : asset.materials) {
        if (!std::isfinite(material.shininess)
            || !ValidTextureIndex(material.diffuseMap, asset.textures.size())
            || !ValidTextureIndex(material.normalMap, asset.textures.size())
            || !ValidTextureIndex(material.specularMap, asset.textures.size())
            || !ValidTextureIndex(material.emissiveMap, asset.textures.size())) {
            SetError(error, "Skeletal mesh contains an invalid material.");
            return false;
        }
    }
    for (const auto& subMesh : asset.subMeshes) {
        if (subMesh.vertices.empty()
            || subMesh.vertices.size() % kSkeletalMeshVertexStride != 0
            || subMesh.indices.empty() || subMesh.indices.size() % 3u != 0
            || subMesh.material < -1
            || (subMesh.material >= 0
                && static_cast<std::size_t>(subMesh.material)
                       >= asset.materials.size())) {
            SetError(error, "Skeletal mesh contains invalid geometry or material indices.");
            return false;
        }
        const std::size_t vertexCount =
            subMesh.vertices.size() / kSkeletalMeshVertexStride;
        for (float value : subMesh.vertices)
            if (!std::isfinite(value)) {
                SetError(error, "Skeletal mesh contains a non-finite vertex value.");
                return false;
            }
        for (std::size_t offset = 0; offset < subMesh.vertices.size();
             offset += kSkeletalMeshVertexStride) {
            float weightSum = 0.0f;
            for (std::size_t component = 0; component < 4; ++component) {
                const float bone = subMesh.vertices[offset + 8u + component];
                const float weight = subMesh.vertices[offset + 12u + component];
                if (bone < 0.0f || bone >= asset.skeleton.bones.size()
                    || std::floor(bone) != bone || weight < 0.0f) {
                    SetError(error, "Skeletal mesh contains invalid bone influences.");
                    return false;
                }
                weightSum += weight;
            }
            if (!std::isfinite(weightSum) || std::abs(weightSum - 1.0f) > 0.002f) {
                SetError(error, "Skeletal mesh vertex weights are not normalized.");
                return false;
            }
        }
        for (std::uint32_t index : subMesh.indices)
            if (index >= vertexCount) {
                SetError(error, "Skeletal mesh index is out of range.");
                return false;
            }
    }
    return true;
}

glm::mat4 ToGlm(const aiMatrix4x4& value) {
    return glm::mat4(value.a1, value.b1, value.c1, value.d1,
                     value.a2, value.b2, value.c2, value.d2,
                     value.a3, value.b3, value.c3, value.d3,
                     value.a4, value.b4, value.c4, value.d4);
}

void FlipRows(std::vector<std::uint8_t>& rgba, std::uint32_t width,
              std::uint32_t height) {
    const std::size_t row = static_cast<std::size_t>(width) * 4u;
    for (std::uint32_t y = 0; y < height / 2u; ++y)
        std::swap_ranges(rgba.begin() + static_cast<std::ptrdiff_t>(y * row),
                         rgba.begin() + static_cast<std::ptrdiff_t>((y + 1u) * row),
                         rgba.begin() + static_cast<std::ptrdiff_t>(
                             (height - 1u - y) * row));
}

bool DecodeEmbedded(const aiTexture* embedded, StaticMeshTextureData* texture) {
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
            if (hint == "png" || (size > 2 && bytes[0] == 0x89 && bytes[1] == 'P'))
                decoded = image::DecodePNGFromMemeory(bytes, size);
            else if (hint == "jpg" || hint == "jpeg"
                     || (size > 2 && bytes[0] == 0xff && bytes[1] == 0xd8))
                decoded = image::DecodeJPEGFromMemory(bytes, size);
            else return false;
            texture->width = static_cast<std::uint32_t>(decoded.width);
            texture->height = static_cast<std::uint32_t>(decoded.height);
            texture->rgba.assign(decoded.rgba.begin(), decoded.rgba.end());
        } else {
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
        }
        FlipRows(texture->rgba, texture->width, texture->height);
        return texture->width > 0 && texture->height > 0;
    } catch (...) {
        return false;
    }
}

bool DecodeExternal(const std::filesystem::path& path,
                    StaticMeshTextureData* texture) {
    try {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        image::Image decoded = extension == ".png"
            ? image::DecodePNG(path.string()) : image::DecodeJPEG(path.string());
        texture->width = static_cast<std::uint32_t>(decoded.width);
        texture->height = static_cast<std::uint32_t>(decoded.height);
        texture->rgba.assign(decoded.rgba.begin(), decoded.rgba.end());
        FlipRows(texture->rgba, texture->width, texture->height);
        return texture->width > 0 && texture->height > 0;
    } catch (...) {
        return false;
    }
}

NamedAnimationClipData BuildClip(
    const aiAnimation* source, const Skeleton& skeleton,
    const std::map<std::string, int>& boneIndex) {
    NamedAnimationClipData result;
    result.animation.name = source->mName.length > 0
        ? source->mName.C_Str() : "Animation";
    result.animation.duration = static_cast<float>(source->mDuration);
    result.animation.ticksPerSecond = source->mTicksPerSecond > 0.0
        ? static_cast<float>(source->mTicksPerSecond) : 25.0f;
    result.animation.channels.resize(skeleton.bones.size());
    result.channelBoneNames.reserve(skeleton.bones.size());
    for (const Bone& bone : skeleton.bones)
        result.channelBoneNames.push_back(bone.name);
    for (unsigned i = 0; i < source->mNumChannels; ++i) {
        const aiNodeAnim* input = source->mChannels[i];
        const auto found = boneIndex.find(input->mNodeName.C_Str());
        if (found == boneIndex.end()) continue;
        BoneChannel& channel =
            result.animation.channels[static_cast<std::size_t>(found->second)];
        for (unsigned k = 0; k < input->mNumPositionKeys; ++k) {
            const auto& key = input->mPositionKeys[k];
            channel.positions.push_back({
                static_cast<float>(key.mTime),
                {key.mValue.x, key.mValue.y, key.mValue.z}});
        }
        for (unsigned k = 0; k < input->mNumRotationKeys; ++k) {
            const auto& key = input->mRotationKeys[k];
            channel.rotations.push_back({
                static_cast<float>(key.mTime),
                glm::quat(key.mValue.w, key.mValue.x,
                          key.mValue.y, key.mValue.z)});
        }
        for (unsigned k = 0; k < input->mNumScalingKeys; ++k) {
            const auto& key = input->mScalingKeys[k];
            channel.scales.push_back({
                static_cast<float>(key.mTime),
                {key.mValue.x, key.mValue.y, key.mValue.z}});
        }
    }
    return result;
}

std::string SanitizeName(std::string value) {
    for (char& character : value) {
        const unsigned char c = static_cast<unsigned char>(character);
        if (!std::isalnum(c) && character != '_' && character != '-')
            character = '_';
    }
    while (!value.empty() && value.back() == '_') value.pop_back();
    return value.empty() ? "Animation" : value;
}

AssetHandle ExistingId(const std::string& path, AssetType type) {
    NativeAssetHeader header;
    std::string ignored;
    return ReadNativeAssetHeaderFile(path, &header, &ignored) && header.type == type
        ? header.id : AssetHandle{};
}

bool VirtualPathFor(const std::string& path, const std::string& contentRoot,
                    std::string* virtualPath) {
    std::error_code ec;
    const auto root = std::filesystem::absolute(contentRoot, ec).lexically_normal();
    if (ec) return false;
    const auto absolute = std::filesystem::absolute(path, ec).lexically_normal();
    if (ec) return false;
    const auto relative = absolute.lexically_relative(root);
    if (relative.empty() || relative.is_absolute()
        || (relative.begin() != relative.end()
            && relative.begin()->generic_string() == ".."))
        return false;
    *virtualPath = AssetRegistry::NormalizeVirtualPath(relative.generic_string());
    return true;
}

} // namespace

bool SaveSkeletonAsset(const std::string& path, SkeletonAssetData asset,
                       std::string* error) {
    asset.header.type = AssetType::Skeleton;
    asset.header.assetVersion = kSkeletonAssetVersion;
    if (!asset.header.id.Valid() || !ValidateSkeleton(asset.skeleton, error))
        return false;
    return SaveNative(path, asset.header,
        [&](std::ostream& output) { return WriteSkeleton(output, asset.skeleton); },
        error);
}

bool LoadSkeletonAsset(const std::string& path, SkeletonAssetData* asset,
                       std::string* error) {
    if (!asset) {
        SetError(error, "Skeleton output is null.");
        return false;
    }
    SkeletonAssetData loaded;
    if (!LoadNative(path, AssetType::Skeleton, kSkeletonAssetVersion,
            &loaded.header,
            [&](std::istream& input) {
                return ReadSkeleton(input, &loaded.skeleton);
            }, error)
        || !ValidateSkeleton(loaded.skeleton, error))
        return false;
    *asset = std::move(loaded);
    SetError(error, {});
    return true;
}

bool SaveAnimationAsset(const std::string& path, AnimationAssetData asset,
                        std::string* error) {
    asset.header.type = AssetType::Animation;
    asset.header.assetVersion = kAnimationAssetVersion;
    if (!asset.header.id.Valid() || !asset.skeletonId.Valid()
        || asset.clips.empty() || !ValidateClips(asset.clips, error)) {
        if (error && error->empty()) *error = "Animation asset identity is invalid.";
        return false;
    }
    asset.header.dependencies = {asset.skeletonId};
    return SaveNative(path, asset.header, [&](std::ostream& output) {
        return WriteHandle(output, asset.skeletonId)
            && WriteClips(output, asset.clips);
    }, error);
}

bool LoadAnimationAsset(const std::string& path, AnimationAssetData* asset,
                        std::string* error) {
    if (!asset) {
        SetError(error, "Animation output is null.");
        return false;
    }
    AnimationAssetData loaded;
    if (!LoadNative(path, AssetType::Animation, kAnimationAssetVersion,
            &loaded.header, [&](std::istream& input) {
                return ReadHandle(input, &loaded.skeletonId)
                    && ReadClips(input, &loaded.clips);
            }, error)
        || !loaded.skeletonId.Valid() || loaded.clips.empty()
        || !ValidateClips(loaded.clips, error))
        return false;
    *asset = std::move(loaded);
    SetError(error, {});
    return true;
}

bool SaveSkeletalMeshAsset(const std::string& path, SkeletalMeshAssetData asset,
                           std::string* error) {
    asset.header.type = AssetType::SkeletalMesh;
    asset.header.assetVersion = kSkeletalMeshAssetVersion;
    if (!ValidateSkeletalMeshPayload(asset, error)) return false;
    return SaveNative(path, asset.header, [&](std::ostream& output) {
        if (!WriteHandle(output, asset.skeletonId)) return false;
        for (float value : asset.minimum) if (!WriteF(output, value)) return false;
        for (float value : asset.maximum) if (!WriteF(output, value)) return false;
        if (!WriteSkeleton(output, asset.skeleton)
            || !WriteClips(output, asset.embeddedAnimations)
            || !WriteMaterials(output, asset.materials)
            || !WriteTextures(output, asset.textures)
            || asset.subMeshes.size() > std::numeric_limits<std::uint32_t>::max()
            || !WriteU(output, static_cast<std::uint32_t>(asset.subMeshes.size())))
            return false;
        for (const auto& subMesh : asset.subMeshes) {
            if (!WriteI(output, subMesh.material)
                || !WriteU(output, static_cast<std::uint64_t>(subMesh.vertices.size())))
                return false;
            for (float value : subMesh.vertices) if (!WriteF(output, value)) return false;
            if (!WriteU(output, static_cast<std::uint64_t>(subMesh.indices.size())))
                return false;
            for (std::uint32_t index : subMesh.indices)
                if (!WriteU(output, index)) return false;
        }
        return true;
    }, error);
}

bool LoadSkeletalMeshAsset(const std::string& path, SkeletalMeshAssetData* asset,
                           std::string* error) {
    if (!asset) {
        SetError(error, "Skeletal mesh output is null.");
        return false;
    }
    SkeletalMeshAssetData loaded;
    if (!LoadNative(path, AssetType::SkeletalMesh, kSkeletalMeshAssetVersion,
            &loaded.header, [&](std::istream& input) {
                if (!ReadHandle(input, &loaded.skeletonId)) return false;
                for (float& value : loaded.minimum) if (!ReadF(input, &value)) return false;
                for (float& value : loaded.maximum) if (!ReadF(input, &value)) return false;
                if (!ReadSkeleton(input, &loaded.skeleton)
                    || !ReadClips(input, &loaded.embeddedAnimations)
                    || !ReadMaterials(input, &loaded.materials)
                    || !ReadTextures(input, &loaded.textures))
                    return false;
                std::uint32_t count = 0;
                if (!ReadU(input, &count) || count > kMaximumRecords) return false;
                loaded.subMeshes.resize(count);
                for (auto& subMesh : loaded.subMeshes) {
                    std::uint64_t vertices = 0, indices = 0;
                    if (!ReadI(input, &subMesh.material) || !ReadU(input, &vertices)
                        || vertices > kMaximumElements
                        || vertices > std::numeric_limits<std::size_t>::max())
                        return false;
                    subMesh.vertices.resize(static_cast<std::size_t>(vertices));
                    for (float& value : subMesh.vertices)
                        if (!ReadF(input, &value)) return false;
                    if (!ReadU(input, &indices) || indices > kMaximumElements
                        || indices > std::numeric_limits<std::size_t>::max())
                        return false;
                    subMesh.indices.resize(static_cast<std::size_t>(indices));
                    for (std::uint32_t& index : subMesh.indices)
                        if (!ReadU(input, &index)) return false;
                }
                return true;
            }, error))
        return false;
    if (!ValidateSkeletalMeshPayload(loaded, error)) return false;
    *asset = std::move(loaded);
    SetError(error, {});
    return true;
}

bool ImportSkeletalSource(
    const std::string& sourcePath, const SkeletalImportOptions& options,
    SkeletalMeshAssetData* mesh, SkeletonAssetData* skeleton,
    std::vector<AnimationAssetData>* animations, SkeletalImportResult* result,
    std::string* error) {
    if (!mesh || !skeleton || !animations || !std::isfinite(options.uniformScale)
        || options.uniformScale <= 0.0f) {
        SetError(error, "Skeletal import settings are invalid.");
        return false;
    }
    unsigned flags = aiProcess_Triangulate | aiProcess_LimitBoneWeights
        | aiProcess_GenUVCoords | aiProcess_SortByPType
        | aiProcess_ValidateDataStructure;
    if (options.generateSmoothNormals) flags |= aiProcess_GenSmoothNormals;
    if (options.joinIdenticalVertices) flags |= aiProcess_JoinIdenticalVertices;
    if (options.flipUVs) flags |= aiProcess_FlipUVs;
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(sourcePath, flags);
    if (!scene || !scene->mRootNode) {
        SetError(error, "Skeletal import failed for '" + sourcePath
            + "': " + importer.GetErrorString());
        return false;
    }
    bool hasBones = false;
    for (unsigned i = 0; i < scene->mNumMeshes; ++i)
        hasBones = hasBones || scene->mMeshes[i]->HasBones();
    if (!hasBones && scene->mNumAnimations == 0) {
        SetError(error, "The source contains neither skinning nor animation data.");
        return false;
    }
    std::string hashError;
    const std::uint64_t sourceHash = HashAssetSourceFile(sourcePath, &hashError);
    if (!hashError.empty()) {
        SetError(error, hashError);
        return false;
    }

    SkeletonAssetData importedSkeleton;
    importedSkeleton.header.type = AssetType::Skeleton;
    importedSkeleton.header.id = AssetHandle::Generate();
    importedSkeleton.header.assetVersion = kSkeletonAssetVersion;
    importedSkeleton.header.importerVersion = kSkeletalImporterVersion;
    importedSkeleton.header.sourceHash = sourceHash;
    // FBX scenes contain cameras, mesh containers and Assimp helper nodes in the
    // same hierarchy as the actual rig. Keep only nodes needed by skinning or
    // animation (plus their ancestors), otherwise those helpers can exhaust the
    // runtime bone palette and duplicate names are common.
    std::unordered_set<std::string> requiredNames;
    for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
        const aiMesh* sourceMesh = scene->mMeshes[i];
        for (unsigned bone = 0; bone < sourceMesh->mNumBones; ++bone)
            requiredNames.insert(sourceMesh->mBones[bone]->mName.C_Str());
    }
    for (unsigned i = 0; i < scene->mNumAnimations; ++i) {
        const aiAnimation* sourceAnimation = scene->mAnimations[i];
        for (unsigned channel = 0; channel < sourceAnimation->mNumChannels;
             ++channel)
            requiredNames.insert(
                sourceAnimation->mChannels[channel]->mNodeName.C_Str());
    }
    std::unordered_set<const aiNode*> requiredNodes;
    std::function<void(const aiNode*)> findRequired =
        [&](const aiNode* node) {
            if (requiredNames.find(node->mName.C_Str()) != requiredNames.end()) {
                for (const aiNode* current = node; current;
                     current = current->mParent)
                    requiredNodes.insert(current);
            }
            for (unsigned child = 0; child < node->mNumChildren; ++child)
                findRequired(node->mChildren[child]);
        };
    findRequired(scene->mRootNode);
    requiredNodes.insert(scene->mRootNode);

    std::map<std::string, int> boneIndex;
    std::unordered_map<std::string, unsigned> storedNameCounts;
    std::function<void(const aiNode*, int)> buildBones =
        [&](const aiNode* node, int parent) {
            if (requiredNodes.find(node) == requiredNodes.end()) return;
            const std::string sourceName = node->mName.C_Str();
            const unsigned occurrence = ++storedNameCounts[sourceName];
            Bone bone;
            bone.name = occurrence == 1
                ? sourceName
                : sourceName + "#" + std::to_string(occurrence);
            bone.parent = parent;
            bone.localBind = ToGlm(node->mTransformation);
            const int index =
                static_cast<int>(importedSkeleton.skeleton.bones.size());
            importedSkeleton.skeleton.bones.push_back(std::move(bone));
            // Match Assimp mesh/animation channels by their source node name.
            // If a malformed source duplicates a required name, this mirrors
            // Assimp's existing last-match behavior while stored names stay
            // unique for stable native animation dependencies.
            boneIndex[sourceName] = index;
            for (unsigned i = 0; i < node->mNumChildren; ++i)
                buildBones(node->mChildren[i], index);
        };
    buildBones(scene->mRootNode, -1);
    importedSkeleton.skeleton.globalInverse =
        glm::inverse(ToGlm(scene->mRootNode->mTransformation));
    if (!ValidateSkeleton(importedSkeleton.skeleton, error)) return false;

    SkeletalMeshAssetData importedMesh;
    importedMesh.header.type = AssetType::SkeletalMesh;
    importedMesh.header.id = AssetHandle::Generate();
    importedMesh.header.assetVersion = kSkeletalMeshAssetVersion;
    importedMesh.header.importerVersion = kSkeletalImporterVersion;
    importedMesh.header.sourceHash = sourceHash;
    importedMesh.skeletonId = importedSkeleton.header.id;
    importedMesh.skeleton = importedSkeleton.skeleton;

    const std::filesystem::path sourceDirectory =
        std::filesystem::path(sourcePath).parent_path();
    std::map<std::string, int> textureCache;
    auto loadTexture = [&](const aiMaterial* material, aiTextureType type) -> int {
        if (material->GetTextureCount(type) == 0) return -1;
        aiString texturePath;
        material->GetTexture(type, 0, &texturePath);
        const std::string key = texturePath.C_Str();
        const auto cached = textureCache.find(key);
        if (cached != textureCache.end()) return cached->second;
        StaticMeshTextureData texture;
        texture.name = std::filesystem::path(key).filename().string();
        bool decoded = false;
        if (const aiTexture* embedded = scene->GetEmbeddedTexture(key.c_str()))
            decoded = DecodeEmbedded(embedded, &texture);
        else {
            std::filesystem::path full(key);
            if (full.is_relative()) full = sourceDirectory / full;
            decoded = DecodeExternal(full.lexically_normal(), &texture);
        }
        const int index = decoded
            ? static_cast<int>(importedMesh.textures.size()) : -1;
        if (decoded) importedMesh.textures.push_back(std::move(texture));
        textureCache[key] = index;
        return index;
    };
    importedMesh.materials.reserve(scene->mNumMaterials);
    for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
        const aiMaterial* input = scene->mMaterials[i];
        StaticMeshMaterialData material;
        aiString name;
        if (input->Get(AI_MATKEY_NAME, name) == AI_SUCCESS)
            material.name = name.C_Str();
        aiColor3D color;
        if (input->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
            material.diffuse = {{color.r, color.g, color.b}};
        if (input->Get(AI_MATKEY_COLOR_SPECULAR, color) == AI_SUCCESS)
            material.specular = {{color.r, color.g, color.b}};
        if (input->Get(AI_MATKEY_COLOR_EMISSIVE, color) == AI_SUCCESS)
            material.emissive = {{color.r, color.g, color.b}};
        float shine = 0.0f;
        if (input->Get(AI_MATKEY_SHININESS, shine) == AI_SUCCESS && shine > 0.0f)
            material.shininess = shine;
        material.diffuseMap = loadTexture(input, aiTextureType_DIFFUSE);
        material.normalMap = loadTexture(input, aiTextureType_NORMALS);
        if (material.normalMap < 0)
            material.normalMap = loadTexture(input, aiTextureType_HEIGHT);
        material.specularMap = loadTexture(input, aiTextureType_SPECULAR);
        material.emissiveMap = loadTexture(input, aiTextureType_EMISSIVE);
        importedMesh.materials.push_back(std::move(material));
    }

    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());
    std::size_t vertexCount = 0, triangleCount = 0;
    for (unsigned meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        const aiMesh* input = scene->mMeshes[meshIndex];
        if (input->mNumVertices == 0) continue;
        std::vector<glm::ivec4> ids(input->mNumVertices, glm::ivec4(0));
        std::vector<glm::vec4> weights(input->mNumVertices, glm::vec4(0.0f));
        std::vector<int> slots(input->mNumVertices, 0);
        for (unsigned i = 0; i < input->mNumBones; ++i) {
            const aiBone* sourceBone = input->mBones[i];
            const auto found = boneIndex.find(sourceBone->mName.C_Str());
            if (found == boneIndex.end()) continue;
            const int index = found->second;
            importedSkeleton.skeleton.bones[static_cast<std::size_t>(index)].offset =
                ToGlm(sourceBone->mOffsetMatrix);
            importedMesh.skeleton.bones[static_cast<std::size_t>(index)].offset =
                ToGlm(sourceBone->mOffsetMatrix);
            for (unsigned w = 0; w < sourceBone->mNumWeights; ++w) {
                const unsigned vertex = sourceBone->mWeights[w].mVertexId;
                if (vertex >= input->mNumVertices || slots[vertex] >= 4) continue;
                ids[vertex][slots[vertex]] = index;
                weights[vertex][slots[vertex]] = sourceBone->mWeights[w].mWeight;
                ++slots[vertex];
            }
        }
        SkeletalMeshSubMeshData output;
        output.material = input->mMaterialIndex < importedMesh.materials.size()
            ? static_cast<std::int32_t>(input->mMaterialIndex) : -1;
        output.vertices.reserve(
            static_cast<std::size_t>(input->mNumVertices)
            * kSkeletalMeshVertexStride);
        for (unsigned i = 0; i < input->mNumVertices; ++i) {
            const glm::vec3 position(
                input->mVertices[i].x * options.uniformScale,
                input->mVertices[i].y * options.uniformScale,
                input->mVertices[i].z * options.uniformScale);
            const aiVector3D normal = input->HasNormals()
                ? input->mNormals[i] : aiVector3D(0.0f, 1.0f, 0.0f);
            const aiVector3D uv = input->HasTextureCoords(0)
                ? input->mTextureCoords[0][i] : aiVector3D();
            output.vertices.insert(output.vertices.end(), {
                position.x, position.y, position.z,
                normal.x, normal.y, normal.z, uv.x, uv.y});
            for (int component = 0; component < 4; ++component)
                output.vertices.push_back(
                    static_cast<float>(ids[i][component]));
            glm::vec4 normalized = weights[i];
            const float sum = normalized.x + normalized.y
                + normalized.z + normalized.w;
            normalized = sum > 1e-5f
                ? normalized / sum : glm::vec4(1, 0, 0, 0);
            for (int component = 0; component < 4; ++component)
                output.vertices.push_back(normalized[component]);
            minimum = glm::min(minimum, position);
            maximum = glm::max(maximum, position);
        }
        for (unsigned faceIndex = 0; faceIndex < input->mNumFaces; ++faceIndex) {
            const aiFace& face = input->mFaces[faceIndex];
            if (face.mNumIndices == 3)
                output.indices.insert(output.indices.end(), {
                    face.mIndices[0], face.mIndices[1], face.mIndices[2]});
        }
        if (!output.indices.empty()) {
            vertexCount += input->mNumVertices;
            triangleCount += output.indices.size() / 3u;
            importedMesh.subMeshes.push_back(std::move(output));
        }
    }
    if (!importedMesh.subMeshes.empty()) {
        importedMesh.minimum = {{minimum.x, minimum.y, minimum.z}};
        importedMesh.maximum = {{maximum.x, maximum.y, maximum.z}};
    }

    if (options.uniformScale != 1.0f) {
        auto scaleTranslation = [&](glm::mat4& matrix) {
            matrix[3][0] *= options.uniformScale;
            matrix[3][1] *= options.uniformScale;
            matrix[3][2] *= options.uniformScale;
        };
        scaleTranslation(importedSkeleton.skeleton.globalInverse);
        for (Bone& bone : importedSkeleton.skeleton.bones) {
            scaleTranslation(bone.localBind);
            scaleTranslation(bone.offset);
        }
    }
    importedMesh.skeleton = importedSkeleton.skeleton;

    animations->clear();
    if (options.importEmbeddedAnimations) {
        for (unsigned i = 0; i < scene->mNumAnimations; ++i) {
            NamedAnimationClipData clip =
                BuildClip(scene->mAnimations[i], importedSkeleton.skeleton, boneIndex);
            if (options.uniformScale != 1.0f) {
                for (BoneChannel& channel : clip.animation.channels)
                    for (VecKey& key : channel.positions)
                        key.value *= options.uniformScale;
            }
            AnimationAssetData animation;
            animation.header.type = AssetType::Animation;
            animation.header.id = AssetHandle::Generate();
            animation.header.assetVersion = kAnimationAssetVersion;
            animation.header.importerVersion = kSkeletalImporterVersion;
            animation.header.sourceHash = sourceHash;
            animation.skeletonId = importedSkeleton.header.id;
            animation.clips.push_back(clip);
            animations->push_back(std::move(animation));
            importedMesh.embeddedAnimations.push_back(std::move(clip));
        }
    }
    if (importedMesh.subMeshes.empty() && animations->empty()) {
        SetError(error, "Skeletal source contains no mesh or importable animation.");
        return false;
    }
    *mesh = std::move(importedMesh);
    *skeleton = std::move(importedSkeleton);
    if (result) {
        result->skeletalMeshId = mesh->header.id;
        result->skeletonId = skeleton->header.id;
        result->sourceHash = sourceHash;
        result->boneCount = skeleton->skeleton.bones.size();
        result->vertexCount = vertexCount;
        result->triangleCount = triangleCount;
    }
    SetError(error, {});
    return true;
}

bool InspectModelSource(const std::string& sourcePath, ModelSourceInfo* info,
                        std::string* error) {
    if (!info) {
        SetError(error, "Model source inspection output is null.");
        return false;
    }
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        sourcePath, aiProcess_ValidateDataStructure);
    if (!scene || !scene->mRootNode) {
        SetError(error, "Could not inspect model source '" + sourcePath
            + "': " + importer.GetErrorString());
        return false;
    }
    ModelSourceInfo inspected;
    inspected.meshCount = scene->mNumMeshes;
    inspected.animationCount = scene->mNumAnimations;
    for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
        if (scene->mMeshes[i] && scene->mMeshes[i]->HasBones()) {
            inspected.hasBones = true;
            break;
        }
    }
    *info = inspected;
    SetError(error, {});
    return true;
}

bool ImportSkeletalAssetsToContent(
    const std::string& sourcePath, const std::string& destinationBase,
    const std::string& contentRoot, const SkeletalImportOptions& options,
    AssetRegistry* registry, SkeletalImportResult* result, std::string* error) {
    SkeletalMeshAssetData mesh;
    SkeletonAssetData skeleton;
    std::vector<AnimationAssetData> animations;
    SkeletalImportResult output;
    if (!ImportSkeletalSource(
            sourcePath, options, &mesh, &skeleton, &animations, &output, error))
        return false;

    const std::string skeletonPath = destinationBase + ".3dgskel";
    const std::string meshPath = destinationBase + ".3dgskmesh";
    if (const AssetHandle id = ExistingId(skeletonPath, AssetType::Skeleton))
        skeleton.header.id = id;
    mesh.skeletonId = skeleton.header.id;
    mesh.skeleton = skeleton.skeleton;
    if (const AssetHandle id = ExistingId(meshPath, AssetType::SkeletalMesh))
        mesh.header.id = id;

    std::vector<std::string> animationPaths;
    std::unordered_map<std::string, int> usedNames;
    for (std::size_t i = 0; i < animations.size(); ++i) {
        animations[i].skeletonId = skeleton.header.id;
        std::string name = SanitizeName(animations[i].clips[0].animation.name);
        const int suffix = usedNames[name]++;
        if (suffix > 0) name += "_" + std::to_string(suffix + 1);
        const std::string path = destinationBase + "_" + name + ".3dganim";
        if (const AssetHandle id = ExistingId(path, AssetType::Animation))
            animations[i].header.id = id;
        animationPaths.push_back(path);
    }
    mesh.header.dependencies.clear();
    mesh.header.dependencies.push_back(skeleton.header.id);
    for (const auto& animation : animations)
        mesh.header.dependencies.push_back(animation.header.id);

    AssetRegistry updated;
    if (registry) updated = *registry;
    auto registerAsset = [&](const std::string& path, AssetHandle id,
                             AssetType type, std::vector<AssetHandle> dependencies) {
        if (!registry) return true;
        std::string virtualPath;
        if (!VirtualPathFor(path, contentRoot, &virtualPath)) {
            SetError(error, "Skeletal asset destination must be inside Content.");
            return false;
        }
        AssetRegistryEntry entry;
        entry.id = id;
        entry.type = type;
        entry.virtualPath = virtualPath;
        std::error_code ec;
        entry.sourcePath =
            std::filesystem::absolute(sourcePath, ec).lexically_normal().string();
        if (ec) entry.sourcePath = sourcePath;
        entry.sourceHash = output.sourceHash;
        entry.importerVersion = kSkeletalImporterVersion;
        entry.dependencies = std::move(dependencies);
        return updated.Register(std::move(entry), error);
    };
    if (!registerAsset(skeletonPath, skeleton.header.id, AssetType::Skeleton, {}))
        return false;
    for (std::size_t i = 0; i < animations.size(); ++i)
        if (!registerAsset(animationPaths[i], animations[i].header.id,
                           AssetType::Animation, {skeleton.header.id}))
            return false;
    if (!mesh.subMeshes.empty()
        && !registerAsset(meshPath, mesh.header.id, AssetType::SkeletalMesh,
                          mesh.header.dependencies))
        return false;

    // A source can lose or rename animation takes between imports. Remove the
    // previous generated clips for this destination that are not part of the
    // new import, otherwise stale assets remain visible and can be selected.
    std::unordered_set<std::string> expectedAnimationVirtualPaths;
    for (const std::string& path : animationPaths) {
        std::string virtualPath;
        if (!VirtualPathFor(path, contentRoot, &virtualPath)) {
            SetError(error, "Animation asset destination must be inside Content.");
            return false;
        }
        expectedAnimationVirtualPaths.insert(
            AssetRegistry::NormalizeVirtualPath(virtualPath));
    }
    std::vector<std::filesystem::path> staleAnimationFiles;
    if (registry) {
        std::string skeletonVirtualPath;
        if (!VirtualPathFor(skeletonPath, contentRoot, &skeletonVirtualPath)) {
            SetError(error, "Skeleton asset destination must be inside Content.");
            return false;
        }
        const std::string extension = ".3dgskel";
        if (skeletonVirtualPath.size() >= extension.size())
            skeletonVirtualPath.erase(
                skeletonVirtualPath.size() - extension.size());
        const std::string generatedPrefix = skeletonVirtualPath + "_";
        std::vector<AssetHandle> staleIds;
        for (const AssetRegistryEntry& entry : updated.Entries()) {
            if (entry.type != AssetType::Animation
                || entry.virtualPath.rfind(generatedPrefix, 0) != 0
                || expectedAnimationVirtualPaths.find(entry.virtualPath)
                       != expectedAnimationVirtualPaths.end())
                continue;
            staleIds.push_back(entry.id);
            constexpr const char* gamePrefix = "/Game/";
            if (entry.virtualPath.rfind(gamePrefix, 0) == 0)
                staleAnimationFiles.push_back(
                    std::filesystem::path(contentRoot)
                    / entry.virtualPath.substr(std::char_traits<char>::length(
                          gamePrefix)));
        }
        for (AssetHandle id : staleIds) updated.Remove(id);
    }

    if (!SaveSkeletonAsset(skeletonPath, skeleton, error)) return false;
    for (std::size_t i = 0; i < animations.size(); ++i)
        if (!SaveAnimationAsset(animationPaths[i], animations[i], error))
            return false;
    if (!mesh.subMeshes.empty() && !SaveSkeletalMeshAsset(meshPath, mesh, error))
        return false;
    if (registry) {
        if (!updated.Save(AssetRegistry::DefaultRegistryPath(contentRoot), error))
            return false;
        *registry = std::move(updated);
    }
    for (const std::filesystem::path& stale : staleAnimationFiles) {
        std::error_code ignored;
        std::filesystem::remove(stale, ignored);
    }

    output.skeletonId = skeleton.header.id;
    output.skeletonPath = skeletonPath;
    output.skeletalMeshId = mesh.header.id;
    output.skeletalMeshPath = mesh.subMeshes.empty() ? std::string() : meshPath;
    output.animationPaths = animationPaths;
    for (const auto& animation : animations)
        output.animationIds.push_back(animation.header.id);
    if (result) *result = std::move(output);
    SetError(error, {});
    return true;
}

} // namespace engine
