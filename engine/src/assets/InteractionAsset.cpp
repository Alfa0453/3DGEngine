#include "engine/assets/InteractionAsset.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>

namespace engine {
namespace {
void Error(std::string* error, const std::string& value) { if (error) *error = value; }
void AddDependency(std::vector<AssetHandle>& deps, AssetHandle id) {
    if (id.Valid() && std::find(deps.begin(), deps.end(), id) == deps.end()) deps.push_back(id);
}
}

const char* InteractionMotionTypeName(InteractionMotionType type) {
    switch (type) {
    case InteractionMotionType::HingedDoor: return "Hinged Door";
    case InteractionMotionType::SlidingDoor: return "Sliding Door";
    case InteractionMotionType::Gate: return "Gate";
    case InteractionMotionType::Elevator: return "Elevator";
    case InteractionMotionType::MovingPlatform: return "Moving Platform";
    }
    return "Hinged Door";
}

const char* InteractionEasingName(InteractionEasing easing) {
    switch (easing) {
    case InteractionEasing::Linear: return "Linear";
    case InteractionEasing::SmoothStep: return "Smooth Step";
    case InteractionEasing::EaseInOut: return "Ease In/Out";
    }
    return "Smooth Step";
}

void NormalizeInteractionAsset(InteractionAssetData& asset) {
    if (asset.name.empty()) asset.name = "InteractiveObject";
    asset.motion = static_cast<InteractionMotionType>(std::clamp(static_cast<int>(asset.motion), 0, 4));
    asset.easing = static_cast<InteractionEasing>(std::clamp(static_cast<int>(asset.easing), 0, 2));
    if (!std::isfinite(asset.hingeAxis.x) || !std::isfinite(asset.hingeAxis.y) ||
        !std::isfinite(asset.hingeAxis.z) || glm::dot(asset.hingeAxis, asset.hingeAxis) < 0.000001f)
        asset.hingeAxis = {0.0f, 1.0f, 0.0f};
    else asset.hingeAxis = glm::normalize(asset.hingeAxis);
    asset.localTranslation = glm::clamp(asset.localTranslation, glm::vec3(-10000.0f), glm::vec3(10000.0f));
    asset.pivotOffset = glm::clamp(asset.pivotOffset, glm::vec3(-10000.0f), glm::vec3(10000.0f));
    asset.openAngleDegrees = std::clamp(asset.openAngleDegrees, -360.0f, 360.0f);
    asset.openDuration = std::clamp(asset.openDuration, 0.01f, 3600.0f);
    asset.closeDuration = std::clamp(asset.closeDuration, 0.01f, 3600.0f);
    asset.holdOpenTime = std::clamp(asset.holdOpenTime, 0.0f, 3600.0f);
    asset.interactionRange = std::clamp(asset.interactionRange, 0.01f, 10000.0f);
    if (asset.prompt.empty()) asset.prompt = "Interact";
    if (asset.motion == InteractionMotionType::MovingPlatform) asset.loop = true;
}

bool ValidateInteractionAsset(const InteractionAssetData& asset, std::string* error) {
    if (asset.name.empty()) { Error(error, "Interaction asset needs a name."); return false; }
    if (asset.openDuration <= 0.0f || asset.closeDuration <= 0.0f) {
        Error(error, "Open and close durations must be greater than zero."); return false;
    }
    if (asset.motion != InteractionMotionType::HingedDoor &&
        glm::dot(asset.localTranslation, asset.localTranslation) < 0.000001f) {
        Error(error, "Sliding, gate, elevator, and platform motion needs a non-zero travel offset."); return false;
    }
    return true;
}

float EvaluateInteractionEasing(InteractionEasing easing, float alpha) {
    const float t = std::clamp(alpha, 0.0f, 1.0f);
    if (easing == InteractionEasing::Linear) return t;
    if (easing == InteractionEasing::EaseInOut)
        return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
    return t * t * (3.0f - 2.0f * t);
}

bool SaveInteractionAsset(const std::string& path, InteractionAssetData asset, std::string* error) {
    NormalizeInteractionAsset(asset);
    if (!ValidateInteractionAsset(asset, error)) return false;
    asset.header.type = AssetType::Interaction;
    asset.header.assetVersion = kInteractionAssetVersion;
    if (!asset.header.id.Valid()) asset.header.id = AssetHandle::Generate();
    asset.header.dependencies.clear();
    AddDependency(asset.header.dependencies, asset.openAudioId);
    AddDependency(asset.header.dependencies, asset.closeAudioId);
    AddDependency(asset.header.dependencies, asset.lockedAudioId);
    AddDependency(asset.header.dependencies, asset.openAnimationId);
    AddDependency(asset.header.dependencies, asset.closeAnimationId);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path);
    if (!out) { Error(error, "Could not create interaction asset: " + path); return false; }
    out << "3DG_INTERACTION " << kInteractionAssetVersion << ' ' << asset.header.id.ToString() << '\n';
    out << "ASSET_DEPS " << asset.header.dependencies.size();
    for (AssetHandle id : asset.header.dependencies) out << ' ' << id.ToString();
    out << '\n' << std::quoted(asset.name) << ' ' << static_cast<int>(asset.motion) << ' '
        << static_cast<int>(asset.easing) << '\n';
    out << asset.localTranslation.x << ' ' << asset.localTranslation.y << ' ' << asset.localTranslation.z << ' '
        << asset.hingeAxis.x << ' ' << asset.hingeAxis.y << ' ' << asset.hingeAxis.z << ' '
        << asset.pivotOffset.x << ' ' << asset.pivotOffset.y << ' ' << asset.pivotOffset.z << '\n';
    out << asset.openAngleDegrees << ' ' << asset.openDuration << ' ' << asset.closeDuration << ' '
        << asset.holdOpenTime << ' ' << asset.interactionRange << '\n';
    out << asset.startsOpen << ' ' << asset.autoClose << ' ' << asset.loop << ' ' << asset.locked << ' '
        << asset.oneShot << ' ' << std::quoted(asset.accessTag) << ' ' << std::quoted(asset.prompt) << '\n';
    out << std::quoted(asset.openAudioPath) << ' ' << std::quoted(asset.closeAudioPath) << ' '
        << std::quoted(asset.lockedAudioPath) << ' ' << std::quoted(asset.openAnimationPath) << ' '
        << std::quoted(asset.closeAnimationPath) << '\n';
    auto writeId = [&](AssetHandle id) { out << (id.Valid() ? id.ToString() : std::string("-")) << ' '; };
    writeId(asset.openAudioId); writeId(asset.closeAudioId); writeId(asset.lockedAudioId);
    writeId(asset.openAnimationId); writeId(asset.closeAnimationId); out << '\n';
    if (!out) { Error(error, "Failed while writing interaction asset."); return false; }
    return true;
}

bool LoadInteractionAsset(const std::string& path, InteractionAssetData* output, std::string* error) {
    if (!output) { Error(error, "Interaction output is null."); return false; }
    std::ifstream in(path);
    if (!in) { Error(error, "Could not open interaction asset: " + path); return false; }
    InteractionAssetData asset;
    std::string magic, text;
    std::uint32_t version = 0;
    if (!(in >> magic >> version >> text) || magic != "3DG_INTERACTION" ||
        version != kInteractionAssetVersion || !AssetHandle::Parse(text, &asset.header.id)) {
        Error(error, "Invalid interaction asset header."); return false;
    }
    std::string deps;
    std::size_t count = 0;
    if (!(in >> deps >> count) || deps != "ASSET_DEPS" || count > 64) {
        Error(error, "Interaction dependency metadata is invalid."); return false;
    }
    asset.header.dependencies.resize(count);
    for (auto& id : asset.header.dependencies) {
        in >> text;
        if (!AssetHandle::Parse(text, &id)) { Error(error, "Invalid interaction dependency ID."); return false; }
    }
    int motion = 0, easing = 0;
    in >> std::quoted(asset.name) >> motion >> easing;
    asset.motion = static_cast<InteractionMotionType>(motion);
    asset.easing = static_cast<InteractionEasing>(easing);
    in >> asset.localTranslation.x >> asset.localTranslation.y >> asset.localTranslation.z
       >> asset.hingeAxis.x >> asset.hingeAxis.y >> asset.hingeAxis.z
       >> asset.pivotOffset.x >> asset.pivotOffset.y >> asset.pivotOffset.z;
    in >> asset.openAngleDegrees >> asset.openDuration >> asset.closeDuration
       >> asset.holdOpenTime >> asset.interactionRange;
    in >> asset.startsOpen >> asset.autoClose >> asset.loop >> asset.locked >> asset.oneShot
       >> std::quoted(asset.accessTag) >> std::quoted(asset.prompt);
    in >> std::quoted(asset.openAudioPath) >> std::quoted(asset.closeAudioPath)
       >> std::quoted(asset.lockedAudioPath) >> std::quoted(asset.openAnimationPath)
       >> std::quoted(asset.closeAnimationPath);
    auto readId = [&](AssetHandle& id) { in >> text; if (text != "-" && !AssetHandle::Parse(text, &id)) in.setstate(std::ios::failbit); };
    readId(asset.openAudioId); readId(asset.closeAudioId); readId(asset.lockedAudioId);
    readId(asset.openAnimationId); readId(asset.closeAnimationId);
    if (!in) { Error(error, "Interaction asset is truncated or corrupt."); return false; }
    asset.header.type = AssetType::Interaction;
    asset.header.assetVersion = version;
    NormalizeInteractionAsset(asset);
    if (!ValidateInteractionAsset(asset, error)) return false;
    *output = std::move(asset);
    return true;
}

} // namespace engine
