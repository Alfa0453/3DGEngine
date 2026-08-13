#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace engine {

// Stable 128-bit identity stored in native assets and referenced by the asset
// registry. Moving or renaming an asset does not change this handle.
struct AssetHandle {
    std::uint64_t high = 0;
    std::uint64_t low = 0;

    bool Valid() const { return high != 0 || low != 0; }
    explicit operator bool() const { return Valid(); }

    static AssetHandle Generate();
    static bool Parse(const std::string& text, AssetHandle* output);
    std::string ToString() const;

    friend bool operator==(const AssetHandle& a, const AssetHandle& b) {
        return a.high == b.high && a.low == b.low;
    }
    friend bool operator!=(const AssetHandle& a, const AssetHandle& b) {
        return !(a == b);
    }
    friend bool operator<(const AssetHandle& a, const AssetHandle& b) {
        return a.high < b.high || (a.high == b.high && a.low < b.low);
    }
};

struct AssetHandleHash {
    std::size_t operator()(const AssetHandle& handle) const noexcept;
};

enum class AssetType : std::uint32_t {
    Unknown = 0,
    StaticMesh,
    SkeletalMesh,
    Skeleton,
    Animation,
    Material,
    Texture,
    Audio,
    Shader,
    Particle,
    ParticleEffect,
    Hud,
    Character,
    AnimationClip,
    AnimationGraph,
    BehaviorTree,
    Scene,
    Script,
    Terrain,
    Font,
    World,  // a streamed world manifest referencing Scene levels (.3dgworld)
    Foliage, // instanced static-mesh foliage palette (.3dgfoliage)
    Ragdoll,  // authored per-bone physics asset (.3dgragdoll)
    AnimationRetarget, // skeleton mapping profile (.3dgretarget)
    Ability, // data-driven gameplay ability (.3dgability)
    Prefab, // reusable editor/runtime object template (.3dgprefab)
    Weather, // reusable environment and precipitation preset (.3dgweather)
    Building, // editable procedural building definition (.3dgbuilding)
    Road, // editable spline road definition (.3dgroad)
    ScatterGraph, // deterministic procedural placement graph (.3dgscatter)
    Biome, // reusable terrain/environment population preset (.3dgbiome)
    DayNightTimeline, // environment timeline (.3dgdaynight)
    Cave // spline cave/tunnel definition (.3dgcave)
};

const char* AssetTypeName(AssetType type);
bool IsKnownAssetType(AssetType type);
const char* NativeAssetExtension(AssetType type);
AssetType NativeAssetTypeFromExtension(const std::string& extension);

// Logical representation of the preamble common to every binary engine-owned
// asset. Type-specific payload data begins immediately after this header.
struct NativeAssetHeader {
    static constexpr std::uint32_t CurrentContainerVersion = 1;

    std::uint32_t containerVersion = CurrentContainerVersion;
    AssetType type = AssetType::Unknown;
    AssetHandle id;
    std::uint32_t assetVersion = 1;
    std::uint32_t importerVersion = 1;
    std::uint64_t sourceHash = 0;
    std::uint64_t payloadSize = 0;
    std::uint32_t flags = 0;
    std::vector<AssetHandle> dependencies;
};

bool WriteNativeAssetHeader(std::ostream& output, const NativeAssetHeader& header,
                            std::string* error = nullptr);
bool ReadNativeAssetHeader(std::istream& input, NativeAssetHeader* header,
                           std::string* error = nullptr);
bool ReadNativeAssetHeaderFile(const std::string& path, NativeAssetHeader* header,
                               std::string* error = nullptr);

} // namespace engine
