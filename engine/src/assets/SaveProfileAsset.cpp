#include "engine/assets/SaveProfileAsset.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <unordered_set>

namespace engine {
namespace {
void SetError(std::string* error, const std::string& message) {
    if (error) *error = message;
}
}

void NormalizeSaveProfile(SaveProfileAssetData& asset) {
    if (asset.name.empty()) asset.name = "SaveProfile";
    asset.maximumSlots = std::clamp(asset.maximumSlots, 1, 64);
    asset.defaultSlot = std::clamp(asset.defaultSlot, 0, asset.maximumSlots - 1);
    asset.autosaveIntervalSeconds = std::clamp(asset.autosaveIntervalSeconds, 5.0f, 86400.0f);
    asset.respawn.healthFraction = std::clamp(asset.respawn.healthFraction, 0.01f, 1.0f);
    for (std::size_t i = 0; i < asset.checkpoints.size(); ++i) {
        auto& checkpoint = asset.checkpoints[i];
        if (checkpoint.name.empty()) checkpoint.name = "Checkpoint " + std::to_string(i + 1);
        checkpoint.activationRadius = std::clamp(checkpoint.activationRadius, 0.1f, 10000.0f);
        checkpoint.slot = std::clamp(checkpoint.slot, 0, asset.maximumSlots - 1);
    }
}

bool ValidateSaveProfile(const SaveProfileAssetData& asset, std::string* error) {
    if (asset.name.empty()) { SetError(error, "Save profile needs a name."); return false; }
    if (asset.maximumSlots < 1 || asset.maximumSlots > 64) {
        SetError(error, "Save profile slot count must be between 1 and 64."); return false;
    }
    std::unordered_set<std::string> names;
    for (const auto& checkpoint : asset.checkpoints) {
        if (checkpoint.name.empty()) { SetError(error, "Every checkpoint needs a name."); return false; }
        if (!names.insert(checkpoint.name).second) {
            SetError(error, "Checkpoint names must be unique: " + checkpoint.name); return false;
        }
    }
    SetError(error, {});
    return true;
}

bool SaveSaveProfileAsset(const std::string& path, SaveProfileAssetData asset, std::string* error) {
    NormalizeSaveProfile(asset);
    if (!ValidateSaveProfile(asset, error)) return false;
    asset.header.type = AssetType::SaveProfile;
    asset.header.assetVersion = kSaveProfileAssetVersion;
    if (!asset.header.id.Valid()) asset.header.id = AssetHandle::Generate();
    asset.header.dependencies.clear();
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) { SetError(error, "Could not create save profile: " + path); return false; }
    out << "3DG_SAVE_PROFILE " << kSaveProfileAssetVersion << ' ' << asset.header.id.ToString() << '\n';
    out << "ASSET_DEPS 0\n";
    out << "PROFILE " << std::quoted(asset.name) << ' ' << asset.maximumSlots << ' '
        << asset.defaultSlot << ' ' << asset.loadLatestOnStart << ' ' << asset.autosaveEnabled << ' '
        << asset.autosaveIntervalSeconds << '\n';
    const auto& p = asset.persistence;
    out << "PERSIST " << p.transforms << ' ' << p.health << ' ' << p.velocity << ' '
        << p.abilities << ' ' << p.inventory << ' ' << p.quests << ' '
        << p.scriptValues << ' ' << p.streamedLevels << '\n';
    const auto& r = asset.respawn;
    out << "RESPAWN " << r.restoreAtCheckpoint << ' ' << r.restoreHealth << ' '
        << r.healthFraction << ' ' << r.clearVelocity << '\n';
    for (const auto& c : asset.checkpoints)
        out << "CHECKPOINT " << std::quoted(c.name) << ' ' << std::quoted(c.anchorObject) << ' '
            << c.position.x << ' ' << c.position.y << ' ' << c.position.z << ' '
            << c.rotationDegrees.x << ' ' << c.rotationDegrees.y << ' ' << c.rotationDegrees.z << ' '
            << c.activationRadius << ' ' << c.slot << ' ' << c.saveOnEnter << ' '
            << c.oneShot << ' ' << c.enabled << '\n';
    out << "END\n";
    return static_cast<bool>(out);
}

bool LoadSaveProfileAsset(const std::string& path, SaveProfileAssetData* output, std::string* error) {
    if (!output) { SetError(error, "Save profile output is null."); return false; }
    std::ifstream in(path);
    SaveProfileAssetData asset;
    std::string record, id;
    std::uint32_t version = 0;
    if (!(in >> record >> version >> id) || record != "3DG_SAVE_PROFILE"
        || version != kSaveProfileAssetVersion || !AssetHandle::Parse(id, &asset.header.id)) {
        SetError(error, "Invalid save profile header."); return false;
    }
    std::size_t dependencyCount = 0;
    if (!(in >> record >> dependencyCount) || record != "ASSET_DEPS" || dependencyCount > 4096) {
        SetError(error, "Invalid save profile dependencies."); return false;
    }
    for (std::size_t i = 0; i < dependencyCount; ++i) in >> id;
    while (in >> record && record != "END") {
        if (record == "PROFILE") {
            in >> std::quoted(asset.name) >> asset.maximumSlots >> asset.defaultSlot
               >> asset.loadLatestOnStart >> asset.autosaveEnabled >> asset.autosaveIntervalSeconds;
        } else if (record == "PERSIST") {
            auto& p = asset.persistence;
            in >> p.transforms >> p.health >> p.velocity >> p.abilities >> p.inventory
               >> p.quests >> p.scriptValues >> p.streamedLevels;
        } else if (record == "RESPAWN") {
            auto& r = asset.respawn;
            in >> r.restoreAtCheckpoint >> r.restoreHealth >> r.healthFraction >> r.clearVelocity;
        } else if (record == "CHECKPOINT") {
            CheckpointDefinition c;
            in >> std::quoted(c.name) >> std::quoted(c.anchorObject)
               >> c.position.x >> c.position.y >> c.position.z
               >> c.rotationDegrees.x >> c.rotationDegrees.y >> c.rotationDegrees.z
               >> c.activationRadius >> c.slot >> c.saveOnEnter >> c.oneShot >> c.enabled;
            asset.checkpoints.push_back(std::move(c));
        } else {
            SetError(error, "Unknown save profile record: " + record); return false;
        }
        if (!in) { SetError(error, "Save profile is truncated or corrupt."); return false; }
    }
    asset.header.type = AssetType::SaveProfile;
    asset.header.assetVersion = version;
    NormalizeSaveProfile(asset);
    if (!ValidateSaveProfile(asset, error)) return false;
    *output = std::move(asset);
    return true;
}

} // namespace engine
