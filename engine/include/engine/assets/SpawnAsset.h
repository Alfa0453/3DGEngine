#pragma once
#include "engine/assets/AssetIdentity.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>
namespace engine {
inline constexpr std::uint32_t kSpawnAssetVersion=1;
enum class SpawnShape:std::uint8_t{Point,Box,Sphere};
struct SpawnEntry{std::string name="Enemy";std::string group="Default";std::string prototypeName;std::string prefabPath;AssetHandle prefabId;float weight=1.0f;float minimumDifficulty=0.0f,maximumDifficulty=10.0f;int maximumAlive=8,poolLimit=8;};
struct SpawnWave{std::string name="Wave 1";std::string group="Default";int count=3;float delayBefore=0.0f,spawnInterval=0.5f,delayAfter=1.0f;bool waitForClear=true;};
struct SpawnAssetData{NativeAssetHeader header;std::string name="SpawnEncounter";SpawnShape shape=SpawnShape::Box;float radius=8.0f;float height=3.0f;glm::vec3 boxExtents{8.0f,3.0f,8.0f};bool autoStart=true,triggerOnPlayerEnter=false,oneShot=true,loopWaves=false,randomYaw=true,recycleDead=true;float retriggerCooldown=5.0f;int maximumConcurrent=16,maximumTotal=64;std::uint32_t seed=1337;std::vector<SpawnEntry> entries;std::vector<SpawnWave> waves;};
const char* SpawnShapeName(SpawnShape shape);
void NormalizeSpawnAsset(SpawnAssetData& asset);
bool ValidateSpawnAsset(const SpawnAssetData& asset,std::string* error=nullptr);
bool SaveSpawnAsset(const std::string& path,SpawnAssetData asset,std::string* error=nullptr);
bool LoadSpawnAsset(const std::string& path,SpawnAssetData* asset,std::string* error=nullptr);
}
