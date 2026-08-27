#include "engine/assets/AssetIdentity.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <type_traits>
#include <utility>

namespace engine {
namespace {

constexpr std::array<char, 8> kAssetMagic{{'3', 'D', 'G', 'A', 'S', 'S', 'E', 'T'}};
constexpr std::uint32_t kMaximumDependencies = 4096;

std::uint64_t Mix(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31u);
}

template<typename T>
bool WriteInteger(std::ostream& output, T value) {
    static_assert(std::is_unsigned<T>::value, "native asset integers are unsigned");
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        output.put(static_cast<char>((value >> (i * 8u)) & static_cast<T>(0xffu)));
    }
    return static_cast<bool>(output);
}

template<typename T>
bool ReadInteger(std::istream& input, T* value) {
    static_assert(std::is_unsigned<T>::value, "native asset integers are unsigned");
    if (!value) return false;
    *value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        const int byte = input.get();
        if (byte == std::char_traits<char>::eof()) return false;
        *value |= static_cast<T>(static_cast<unsigned char>(byte)) << (i * 8u);
    }
    return true;
}

std::string LowerExtension(std::string extension) {
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!extension.empty() && extension.front() != '.') extension.insert(extension.begin(), '.');
    return extension;
}

void SetError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

} // namespace

AssetHandle AssetHandle::Generate() {
    static std::atomic<std::uint64_t> counter{1};
    static const std::uint64_t seed = [] {
        std::random_device random;
        const std::uint64_t clock = static_cast<std::uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        return Mix((static_cast<std::uint64_t>(random()) << 32u)
                   ^ static_cast<std::uint64_t>(random()) ^ clock);
    }();

    const std::uint64_t sequence = counter.fetch_add(1, std::memory_order_relaxed);
    AssetHandle result{Mix(seed ^ sequence), Mix(seed + sequence * 2u + 1u)};
    if (!result.Valid()) result.low = sequence == 0 ? 1 : sequence;
    return result;
}

bool AssetHandle::Parse(const std::string& text, AssetHandle* output) {
    if (!output) return false;
    std::string hex;
    hex.reserve(32);
    for (char c : text) {
        if (c == '-' || c == '{' || c == '}') continue;
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
        hex.push_back(c);
    }
    if (hex.size() != 32) return false;

    try {
        AssetHandle parsed;
        parsed.high = std::stoull(hex.substr(0, 16), nullptr, 16);
        parsed.low = std::stoull(hex.substr(16, 16), nullptr, 16);
        if (!parsed.Valid()) return false;
        *output = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

std::string AssetHandle::ToString() const {
    std::ostringstream output;
    output << std::hex << std::setfill('0')
           << std::setw(16) << high << std::setw(16) << low;
    return output.str();
}

std::size_t AssetHandleHash::operator()(const AssetHandle& handle) const noexcept {
    const std::uint64_t combined =
        handle.high ^ (handle.low + 0x9e3779b97f4a7c15ull
                       + (handle.high << 6u) + (handle.high >> 2u));
    return static_cast<std::size_t>(combined);
}

const char* AssetTypeName(AssetType type) {
    switch (type) {
        case AssetType::Unknown: return "Unknown";
        case AssetType::StaticMesh: return "Static Mesh";
        case AssetType::SkeletalMesh: return "Skeletal Mesh";
        case AssetType::Skeleton: return "Skeleton";
        case AssetType::Animation: return "Animation";
        case AssetType::Material: return "Material";
        case AssetType::Texture: return "Texture";
        case AssetType::Audio: return "Audio";
        case AssetType::Shader: return "Shader";
        case AssetType::Particle: return "Particle";
        case AssetType::ParticleEffect: return "Particle Effect";
        case AssetType::Hud: return "HUD";
        case AssetType::Character: return "Character";
        case AssetType::AnimationClip: return "Animation Clip";
        case AssetType::AnimationGraph: return "Animation Graph";
        case AssetType::BehaviorTree: return "Behavior Tree";
        case AssetType::Scene: return "Scene";
        case AssetType::Script: return "Script";
        case AssetType::Terrain: return "Terrain";
        case AssetType::Font: return "Font";
        case AssetType::World: return "World";
        case AssetType::Foliage: return "Foliage";
        case AssetType::Ragdoll: return "Ragdoll Physics";
        case AssetType::AnimationRetarget: return "Animation Retarget Profile";
        case AssetType::Ability: return "Ability";
        case AssetType::Prefab: return "Prefab";
        case AssetType::Weather: return "Weather";
        case AssetType::Building: return "Procedural Building";
        case AssetType::Road: return "Road";
        case AssetType::ScatterGraph: return "Procedural Scatter Graph";
        case AssetType::Biome: return "Biome";
        case AssetType::DayNightTimeline: return "Day/Night Timeline";
        case AssetType::Cave: return "Cave / Tunnel";
        case AssetType::FenceWall: return "Fence / Wall";
        case AssetType::Destruction: return "Destruction";
    }
    return "Unknown";
}

bool IsKnownAssetType(AssetType type) {
    return type > AssetType::Unknown && type <= AssetType::Destruction;
}

const char* NativeAssetExtension(AssetType type) {
    switch (type) {
        case AssetType::StaticMesh: return ".3dgmesh";
        case AssetType::SkeletalMesh: return ".3dgskmesh";
        case AssetType::Skeleton: return ".3dgskel";
        case AssetType::Animation: return ".3dganim";
        case AssetType::Texture: return ".3dgtex";
        case AssetType::Terrain: return ".3dgterrain";
        case AssetType::World: return ".3dgworld";
        case AssetType::Foliage: return ".3dgfoliage";
        case AssetType::ScatterGraph: return ".3dgscatter";
        case AssetType::Biome: return ".3dgbiome";
        case AssetType::DayNightTimeline: return ".3dgdaynight";
        case AssetType::Cave: return ".3dgcave";
        default: return "";
    }
}

AssetType NativeAssetTypeFromExtension(const std::string& extension) {
    const std::string lower = LowerExtension(extension);
    if (lower == ".3dgmesh") return AssetType::StaticMesh;
    if (lower == ".3dgskmesh") return AssetType::SkeletalMesh;
    if (lower == ".3dgskel") return AssetType::Skeleton;
    if (lower == ".3dganim") return AssetType::Animation;
    if (lower == ".3dgtex") return AssetType::Texture;
    if (lower == ".3dgterrain") return AssetType::Terrain;
    if (lower == ".3dgworld") return AssetType::World;
    if (lower == ".3dgfoliage") return AssetType::Foliage;
    if (lower == ".3dgscatter") return AssetType::ScatterGraph;
    if (lower == ".3dgbiome") return AssetType::Biome;
    if (lower == ".3dgdaynight") return AssetType::DayNightTimeline;
    if (lower == ".3dgcave") return AssetType::Cave;
    return AssetType::Unknown;
}

bool WriteNativeAssetHeader(std::ostream& output, const NativeAssetHeader& header,
                            std::string* error) {
    if (!header.id.Valid()) {
        SetError(error, "Native asset handle is invalid.");
        return false;
    }
    if (!IsKnownAssetType(header.type)) {
        SetError(error, "Native asset type is unknown.");
        return false;
    }
    if (header.containerVersion != NativeAssetHeader::CurrentContainerVersion) {
        SetError(error, "Native asset container version is unsupported.");
        return false;
    }
    if (header.dependencies.size() > kMaximumDependencies) {
        SetError(error, "Native asset has too many dependencies.");
        return false;
    }

    output.write(kAssetMagic.data(), static_cast<std::streamsize>(kAssetMagic.size()));
    const bool wrote =
        WriteInteger(output, header.containerVersion)
        && WriteInteger(output, static_cast<std::uint32_t>(header.type))
        && WriteInteger(output, header.id.high)
        && WriteInteger(output, header.id.low)
        && WriteInteger(output, header.assetVersion)
        && WriteInteger(output, header.importerVersion)
        && WriteInteger(output, header.sourceHash)
        && WriteInteger(output, header.payloadSize)
        && WriteInteger(output, header.flags)
        && WriteInteger(output, static_cast<std::uint32_t>(header.dependencies.size()));
    if (!wrote) {
        SetError(error, "Could not write native asset header.");
        return false;
    }
    for (const AssetHandle dependency : header.dependencies) {
        if (!dependency.Valid()
            || !WriteInteger(output, dependency.high)
            || !WriteInteger(output, dependency.low)) {
            SetError(error, "Could not write native asset dependency.");
            return false;
        }
    }
    SetError(error, {});
    return true;
}

bool ReadNativeAssetHeader(std::istream& input, NativeAssetHeader* header,
                           std::string* error) {
    if (!header) {
        SetError(error, "Native asset header output is null.");
        return false;
    }

    std::array<char, 8> magic{};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!input || magic != kAssetMagic) {
        SetError(error, "File is not a 3DG native asset.");
        return false;
    }

    NativeAssetHeader loaded;
    std::uint32_t type = 0;
    std::uint32_t dependencyCount = 0;
    if (!ReadInteger(input, &loaded.containerVersion)
        || !ReadInteger(input, &type)
        || !ReadInteger(input, &loaded.id.high)
        || !ReadInteger(input, &loaded.id.low)
        || !ReadInteger(input, &loaded.assetVersion)
        || !ReadInteger(input, &loaded.importerVersion)
        || !ReadInteger(input, &loaded.sourceHash)
        || !ReadInteger(input, &loaded.payloadSize)
        || !ReadInteger(input, &loaded.flags)
        || !ReadInteger(input, &dependencyCount)) {
        SetError(error, "Native asset header is truncated.");
        return false;
    }
    loaded.type = static_cast<AssetType>(type);
    if (loaded.containerVersion != NativeAssetHeader::CurrentContainerVersion) {
        SetError(error, "Native asset container version is unsupported.");
        return false;
    }
    if (!loaded.id.Valid() || !IsKnownAssetType(loaded.type)) {
        SetError(error, "Native asset identity or type is invalid.");
        return false;
    }
    if (dependencyCount > kMaximumDependencies) {
        SetError(error, "Native asset dependency list is too large.");
        return false;
    }
    loaded.dependencies.resize(dependencyCount);
    for (AssetHandle& dependency : loaded.dependencies) {
        if (!ReadInteger(input, &dependency.high)
            || !ReadInteger(input, &dependency.low)
            || !dependency.Valid()) {
            SetError(error, "Native asset dependency list is invalid or truncated.");
            return false;
        }
    }

    *header = std::move(loaded);
    SetError(error, {});
    return true;
}

bool ReadNativeAssetHeaderFile(const std::string& path, NativeAssetHeader* header,
                               std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        SetError(error, "Could not open native asset: " + path);
        return false;
    }
    return ReadNativeAssetHeader(input, header, error);
}

} // namespace engine
