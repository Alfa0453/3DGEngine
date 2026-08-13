#include "engine/assets/RagdollAsset.h"

#include <filesystem>
#include <fstream>
#include <iomanip>

namespace engine {

bool SaveRagdollAsset(const std::string& path, RagdollAssetData& asset,
                      std::string* error) {
    std::error_code ec;
    const std::filesystem::path file(path);
    if (file.has_parent_path())
        std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        if (error) *error = "Could not write ragdoll asset: " + path;
        return false;
    }
    if (!asset.assetId.Valid()) asset.assetId = AssetHandle::Generate();
    out << "3DG_RAGDOLL 3 " << asset.assetId.ToString() << "\n"
        << std::quoted(asset.name) << ' ' << std::quoted(asset.skeletonPath) << '\n'
        << asset.totalMass << ' ' << asset.linearDamping << ' '
        << asset.angularDamping << ' ' << asset.deathImpulse << ' '
        << asset.blendInDuration << ' ' << asset.blendOutDuration << ' '
        << asset.recoverWhenRevived << '\n'
        << "BODIES " << asset.bodies.size() << '\n';
    for (const auto& body : asset.bodies) {
        out << std::quoted(body.boneName) << ' ' << static_cast<int>(body.shape) << ' '
            << body.enabled << ' ' << body.localPosition.x << ' ' << body.localPosition.y << ' '
            << body.localPosition.z << ' ' << body.localRotationDegrees.x << ' '
            << body.localRotationDegrees.y << ' ' << body.localRotationDegrees.z << ' '
            << body.halfExtents.x << ' ' << body.halfExtents.y << ' ' << body.halfExtents.z << ' '
            << body.radius << ' ' << body.halfHeight << ' ' << body.massWeight << '\n';
    }
    out << "CONSTRAINTS " << asset.constraints.size() << '\n';
    for (const auto& joint : asset.constraints) {
        out << std::quoted(joint.parentBoneName) << ' ' << std::quoted(joint.childBoneName) << ' '
            << static_cast<int>(joint.type) << ' ' << joint.axis.x << ' ' << joint.axis.y << ' '
            << joint.axis.z << ' ' << joint.swingLimitDegrees << ' ' << joint.twistMinDegrees << ' '
            << joint.twistMaxDegrees << ' ' << joint.collideConnected << '\n';
    }
    if (!out) {
        if (error) *error = "Could not finish ragdoll asset: " + path;
        return false;
    }
    return true;
}

bool LoadRagdollAsset(const std::string& path, RagdollAssetData* asset,
                      std::string* error) {
    if (!asset) { if (error) *error = "No ragdoll output was provided."; return false; }
    std::ifstream in(path);
    std::string magic;
    int version = 0;
    if (!(in >> magic >> version) || magic != "3DG_RAGDOLL"
        || version < 1 || version > 3) {
        if (error) *error = "Invalid ragdoll asset: " + path;
        return false;
    }
    RagdollAssetData loaded;
    if (version >= 3) {
        std::string id;
        if (!(in >> id) || !AssetHandle::Parse(id, &loaded.assetId)) {
            if (error) *error = "Ragdoll asset ID is invalid: " + path;
            return false;
        }
    } else loaded.assetId = AssetHandle::Generate();
    if (!(in >> std::quoted(loaded.name) >> std::quoted(loaded.skeletonPath)
          >> loaded.totalMass >> loaded.linearDamping
          >> loaded.angularDamping >> loaded.deathImpulse)) {
        if (error) *error = "Ragdoll header is incomplete: " + path;
        return false;
    }
    if (version >= 2
        && !(in >> loaded.blendInDuration >> loaded.blendOutDuration
             >> loaded.recoverWhenRevived)) {
        if (error) *error = "Ragdoll blend settings are incomplete: " + path;
        return false;
    }
    std::string tag;
    std::size_t count = 0;
    if (!(in >> tag >> count) || tag != "BODIES" || count > 256) {
        if (error) *error = "Ragdoll body list is invalid: " + path;
        return false;
    }
    loaded.bodies.resize(count);
    for (auto& body : loaded.bodies) {
        int shape = 0;
        if (!(in >> std::quoted(body.boneName) >> shape >> body.enabled
              >> body.localPosition.x >> body.localPosition.y >> body.localPosition.z
              >> body.localRotationDegrees.x >> body.localRotationDegrees.y
              >> body.localRotationDegrees.z >> body.halfExtents.x >> body.halfExtents.y
              >> body.halfExtents.z >> body.radius >> body.halfHeight >> body.massWeight)
            || shape < 0 || shape > 2 || body.boneName.empty()) {
            if (error) *error = "Ragdoll body data is invalid: " + path;
            return false;
        }
        body.shape = static_cast<RagdollBodyShape>(shape);
    }
    if (!(in >> tag >> count) || tag != "CONSTRAINTS" || count > 512) {
        if (error) *error = "Ragdoll constraint list is invalid: " + path;
        return false;
    }
    loaded.constraints.resize(count);
    for (auto& joint : loaded.constraints) {
        int type = 0;
        if (!(in >> std::quoted(joint.parentBoneName) >> std::quoted(joint.childBoneName)
              >> type >> joint.axis.x >> joint.axis.y >> joint.axis.z
              >> joint.swingLimitDegrees >> joint.twistMinDegrees
              >> joint.twistMaxDegrees >> joint.collideConnected)
            || type < 0 || type > 1 || joint.childBoneName.empty()) {
            if (error) *error = "Ragdoll constraint data is invalid: " + path;
            return false;
        }
        joint.type = static_cast<RagdollJointType>(type);
    }
    *asset = std::move(loaded);
    return true;
}

void ApplyRagdollAsset(const RagdollAssetData& asset, Ragdoll* ragdoll) {
    if (!ragdoll) return;
    ragdoll->skeletonPath = asset.skeletonPath;
    ragdoll->totalMass = asset.totalMass;
    ragdoll->linearDamping = asset.linearDamping;
    ragdoll->angularDamping = asset.angularDamping;
    ragdoll->deathImpulse = asset.deathImpulse;
    ragdoll->blendInDuration = asset.blendInDuration;
    ragdoll->blendOutDuration = asset.blendOutDuration;
    ragdoll->recoverWhenRevived = asset.recoverWhenRevived;
    ragdoll->bodies = asset.bodies;
    ragdoll->constraints = asset.constraints;
    ragdoll->maxBodies = static_cast<int>(asset.bodies.size());
}

} // namespace engine
