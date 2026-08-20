#include "engine/assets/FenceWallAsset.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <tuple>

namespace engine {
namespace {
void SetError(std::string* error, const std::string& text) {
    if (error) *error = text;
}
void AddDependency(std::vector<AssetHandle>& dependencies, AssetHandle id) {
    if (id.Valid() && std::find(dependencies.begin(), dependencies.end(), id)
            == dependencies.end())
        dependencies.push_back(id);
}
glm::vec3 Snapped(glm::vec3 point, const FenceWallAssetData& asset) {
    if (!asset.snapToGrid) return point;
    const float grid = std::max(asset.gridSize, 0.001f);
    point.x = std::round(point.x / grid) * grid;
    point.y = std::round(point.y / grid) * grid;
    point.z = std::round(point.z / grid) * grid;
    return point;
}
glm::quat Orientation(glm::vec3 direction, bool followSlope) {
    if (!followSlope) direction.y = 0.0f;
    if (glm::dot(direction, direction) < 1.e-8f) direction = {0, 0, 1};
    direction = glm::normalize(direction);
    const float yaw = std::atan2(direction.x, direction.z);
    const float pitch = followSlope
        ? -std::asin(std::clamp(direction.y, -1.0f, 1.0f)) : 0.0f;
    return glm::angleAxis(yaw, glm::vec3(0, 1, 0))
        * glm::angleAxis(pitch, glm::vec3(1, 0, 0));
}
}

void NormalizeFenceWallAsset(FenceWallAssetData& asset) {
    asset.height = std::clamp(asset.height, 0.1f, 50.0f);
    asset.thickness = std::clamp(asset.thickness, 0.02f, 10.0f);
    asset.panelLength = std::clamp(asset.panelLength, 0.1f, 50.0f);
    asset.postSpacing = std::clamp(asset.postSpacing, 0.1f, 100.0f);
    asset.postWidth = std::clamp(asset.postWidth, 0.02f, 10.0f);
    asset.postHeightExtra = std::clamp(asset.postHeightExtra, 0.0f, 10.0f);
    asset.gateHeight = std::clamp(asset.gateHeight, 0.1f, 50.0f);
    asset.gridSize = std::clamp(asset.gridSize, 0.01f, 100.0f);
    for (glm::vec3& point : asset.points) point = Snapped(point, asset);
    const int segments = std::max(0, static_cast<int>(asset.points.size())
        - (asset.closed ? 0 : 1));
    for (FenceOpening& opening : asset.openings) {
        opening.segmentIndex = std::clamp(opening.segmentIndex, 0,
                                          std::max(0, segments - 1));
        opening.centerDistance = std::max(0.0f, opening.centerDistance);
        opening.width = std::clamp(opening.width, 0.1f, 50.0f);
    }
}

bool ValidateFenceWallAsset(const FenceWallAssetData& asset, std::string* error) {
    if (asset.name.empty()) { SetError(error, "Fence/wall name is empty."); return false; }
    if (asset.points.size() < 2) {
        SetError(error, "Fence/wall requires at least two points."); return false;
    }
    for (const glm::vec3& point : asset.points)
        if (!std::isfinite(point.x) || !std::isfinite(point.y)
            || !std::isfinite(point.z)) {
            SetError(error, "Fence/wall contains a non-finite point."); return false;
        }
    SetError(error, {});
    return true;
}

std::vector<FencePlacement> GenerateFenceWall(
    const FenceWallAssetData& source, FenceGenerationStats* stats,
    std::string* error) {
    FenceWallAssetData asset = source;
    NormalizeFenceWallAsset(asset);
    if (!ValidateFenceWallAsset(asset, error)) return {};
    std::vector<FencePlacement> result;
    FenceGenerationStats generated;
    const int segmentCount = static_cast<int>(asset.points.size())
        - (asset.closed ? 0 : 1);
    std::set<std::tuple<int, int, int>> postKeys;
    int panelNumber = 0, postNumber = 0, gateNumber = 0;

    for (int segment = 0; segment < segmentCount; ++segment) {
        const glm::vec3 a = asset.points[static_cast<std::size_t>(segment)];
        const glm::vec3 b = asset.points[(static_cast<std::size_t>(segment) + 1)
                                        % asset.points.size()];
        glm::vec3 direction = b - a;
        const float length = glm::length(direction);
        if (length < 0.001f) continue;
        direction /= length;
        generated.length += length;
        const glm::quat rotation = Orientation(direction, asset.followSlope);

        std::vector<std::pair<float, float>> gaps;
        for (const FenceOpening& opening : asset.openings) {
            if (opening.segmentIndex != segment) continue;
            const float center = std::clamp(opening.centerDistance, 0.0f, length);
            const float half = opening.width * 0.5f;
            gaps.emplace_back(std::max(0.0f, center-half),
                              std::min(length, center+half));
            if (opening.gate && gaps.back().second > gaps.back().first) {
                const float width = gaps.back().second - gaps.back().first;
                FencePlacement gate;
                gate.kind = FencePartKind::Gate;
                gate.suffix = "Gate_" + std::to_string(++gateNumber);
                gate.position = a + direction * ((gaps.back().first
                    + gaps.back().second) * 0.5f)
                    + rotation * glm::vec3(0, asset.gateHeight * 0.5f, 0);
                gate.scale = {asset.thickness, asset.gateHeight, width};
                gate.rotation = rotation;
                gate.collision = asset.createCollision;
                result.push_back(gate);
                ++generated.gates;
            }
        }
        std::sort(gaps.begin(), gaps.end());
        std::vector<std::pair<float, float>> merged;
        for (const auto& gap : gaps) {
            if (gap.second <= gap.first) continue;
            if (merged.empty() || gap.first > merged.back().second)
                merged.push_back(gap);
            else merged.back().second = std::max(merged.back().second, gap.second);
        }
        std::vector<std::pair<float, float>> solid;
        float cursor = 0.0f;
        for (const auto& gap : merged) {
            if (gap.first > cursor + 0.001f) solid.emplace_back(cursor, gap.first);
            cursor = std::max(cursor, gap.second);
        }
        if (cursor < length - 0.001f) solid.emplace_back(cursor, length);

        for (const auto& interval : solid) {
            const float intervalLength = interval.second - interval.first;
            const int pieces = std::max(1, static_cast<int>(
                std::ceil(intervalLength / asset.panelLength)));
            for (int piece = 0; piece < pieces; ++piece) {
                const float d0 = interval.first + intervalLength * piece / pieces;
                const float d1 = interval.first + intervalLength * (piece+1) / pieces;
                FencePlacement panel;
                panel.kind = FencePartKind::Panel;
                panel.suffix = "Panel_" + std::to_string(++panelNumber);
                panel.position = a + direction * ((d0+d1)*0.5f)
                    + rotation * glm::vec3(0, asset.height*0.5f, 0);
                panel.scale = {asset.thickness, asset.height, d1-d0};
                panel.rotation = rotation;
                panel.collision = asset.createCollision;
                result.push_back(panel);
                ++generated.panels;
            }
        }

        if (asset.createPosts) {
            std::vector<float> distances{0.0f, length};
            const int regular = static_cast<int>(std::floor(length / asset.postSpacing));
            for (int i = 1; i <= regular; ++i)
                if (i * asset.postSpacing < length - 0.001f)
                    distances.push_back(i * asset.postSpacing);
            for (const auto& gap : merged) {
                distances.push_back(gap.first); distances.push_back(gap.second);
            }
            for (float distance : distances) {
                bool insideGap = false;
                for (const auto& gap : merged)
                    if (distance > gap.first + 0.001f
                        && distance < gap.second - 0.001f) insideGap = true;
                if (insideGap) continue;
                const glm::vec3 base = a + direction * distance;
                const auto key = std::make_tuple(
                    static_cast<int>(std::round(base.x*1000.0f)),
                    static_cast<int>(std::round(base.y*1000.0f)),
                    static_cast<int>(std::round(base.z*1000.0f)));
                if (!postKeys.insert(key).second) continue;
                FencePlacement post;
                post.kind = FencePartKind::Post;
                post.suffix = "Post_" + std::to_string(++postNumber);
                const float height = asset.height + asset.postHeightExtra;
                post.position = base + glm::vec3(0, height*0.5f, 0);
                post.scale = {asset.postWidth, height, asset.postWidth};
                post.rotation = glm::quat(1,0,0,0);
                post.collision = asset.createCollision;
                result.push_back(post);
                ++generated.posts;
            }
        }
    }
    if (stats) *stats = generated;
    SetError(error, {});
    return result;
}

bool SaveFenceWallAsset(const std::string& path, FenceWallAssetData asset,
                        std::string* error) {
    NormalizeFenceWallAsset(asset);
    if (!ValidateFenceWallAsset(asset, error)) return false;
    asset.header.type = AssetType::FenceWall;
    asset.header.assetVersion = kFenceWallAssetVersion;
    if (!asset.header.id.Valid()) asset.header.id = AssetHandle::Generate();
    asset.header.dependencies.clear();
    for (AssetHandle id : {asset.panelMeshId, asset.postMeshId, asset.gateMeshId,
                           asset.panelMaterialId, asset.postMaterialId,
                           asset.gateMaterialId})
        AddDependency(asset.header.dependencies, id);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) { SetError(error, "Could not save fence/wall asset: " + path); return false; }
    out << "3DGFenceWall " << kFenceWallAssetVersion << '\n'
        << asset.header.id.ToString() << ' ' << std::quoted(asset.name) << ' '
        << static_cast<int>(asset.mode) << ' ' << asset.points.size() << ' '
        << asset.closed << '\n'
        << asset.height << ' ' << asset.thickness << ' ' << asset.panelLength << ' '
        << asset.postSpacing << ' ' << asset.postWidth << ' '
        << asset.postHeightExtra << ' ' << asset.gateHeight << ' '
        << asset.createPosts << ' ' << asset.createCollision << ' '
        << asset.followSlope << ' ' << asset.snapToGrid << ' ' << asset.gridSize << '\n';
    for (const glm::vec3& point : asset.points)
        out << point.x << ' ' << point.y << ' ' << point.z << '\n';
    out << asset.openings.size() << '\n';
    for (const FenceOpening& opening : asset.openings)
        out << opening.segmentIndex << ' ' << opening.centerDistance << ' '
            << opening.width << ' ' << opening.gate << '\n';
    auto reference = [&out](const std::string& value, AssetHandle id) {
        out << std::quoted(value) << ' ' << (id.Valid() ? id.ToString() : "-") << '\n';
    };
    reference(asset.panelMeshPath, asset.panelMeshId);
    reference(asset.postMeshPath, asset.postMeshId);
    reference(asset.gateMeshPath, asset.gateMeshId);
    reference(asset.panelMaterialPath, asset.panelMaterialId);
    reference(asset.postMaterialPath, asset.postMaterialId);
    reference(asset.gateMaterialPath, asset.gateMaterialId);
    out << "ASSET_DEPS " << asset.header.dependencies.size();
    for (AssetHandle id : asset.header.dependencies) out << ' ' << id.ToString();
    out << '\n';
    if (!out.good()) { SetError(error, "Failed while writing fence/wall asset."); return false; }
    SetError(error, {}); return true;
}

bool LoadFenceWallAsset(const std::string& path, FenceWallAssetData* output,
                        std::string* error) {
    if (!output) { SetError(error, "Fence/wall output is null."); return false; }
    std::ifstream in(path);
    std::string magic, id;
    int version = 0, mode = 0;
    std::size_t pointCount = 0, openingCount = 0;
    FenceWallAssetData asset;
    in >> magic >> version >> id >> std::quoted(asset.name) >> mode
       >> pointCount >> asset.closed;
    if (!in || magic != "3DGFenceWall" || version != 1
        || !AssetHandle::Parse(id, &asset.header.id)) {
        SetError(error, "Invalid fence/wall asset: " + path); return false;
    }
    asset.mode = mode == 1 ? FenceWallMode::Wall : FenceWallMode::Fence;
    in >> asset.height >> asset.thickness >> asset.panelLength
       >> asset.postSpacing >> asset.postWidth >> asset.postHeightExtra
       >> asset.gateHeight >> asset.createPosts >> asset.createCollision
       >> asset.followSlope >> asset.snapToGrid >> asset.gridSize;
    asset.points.resize(pointCount);
    for (glm::vec3& point : asset.points) in >> point.x >> point.y >> point.z;
    in >> openingCount;
    asset.openings.resize(openingCount);
    for (FenceOpening& opening : asset.openings)
        in >> opening.segmentIndex >> opening.centerDistance
           >> opening.width >> opening.gate;
    auto reference = [&in, &asset](std::string& value, AssetHandle& handle) {
        std::string text;
        in >> std::quoted(value) >> text;
        if (text != "-" && !AssetHandle::Parse(text, &handle))
            in.setstate(std::ios::failbit);
        AddDependency(asset.header.dependencies, handle);
    };
    reference(asset.panelMeshPath, asset.panelMeshId);
    reference(asset.postMeshPath, asset.postMeshId);
    reference(asset.gateMeshPath, asset.gateMeshId);
    reference(asset.panelMaterialPath, asset.panelMaterialId);
    reference(asset.postMaterialPath, asset.postMaterialId);
    reference(asset.gateMaterialPath, asset.gateMaterialId);
    if (!in) { SetError(error, "Incomplete fence/wall asset: " + path); return false; }
    asset.header.type = AssetType::FenceWall;
    asset.header.assetVersion = version;
    NormalizeFenceWallAsset(asset);
    if (!ValidateFenceWallAsset(asset, error)) return false;
    *output = std::move(asset);
    SetError(error, {}); return true;
}

} // namespace engine
