#pragma once

#include <engine/assets/StaticMeshAsset.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class MeshGeometryOperations {
public:
    struct Report {
        std::size_t verticesBefore = 0, verticesAfter = 0;
        std::size_t trianglesBefore = 0, trianglesAfter = 0;
        std::size_t changed = 0;
    };

    struct TopologyReport {
        std::size_t boundaryEdges = 0;
        std::size_t nonManifoldEdges = 0;
        std::size_t degenerateFaces = 0;
    };

    static Report ExtrudeFaces(engine::StaticMeshAssetData& asset,
                               const std::vector<std::size_t>& faces, float distance);
    static Report InsetFaces(engine::StaticMeshAssetData& asset,
                             const std::vector<std::size_t>& faces, float amount);
    static Report SubdivideFaces(engine::StaticMeshAssetData& asset,
                                 const std::vector<std::size_t>& faces);
    static Report DeleteFaces(engine::StaticMeshAssetData& asset,
                              const std::vector<std::size_t>& faces);
    static Report WeldVertices(engine::StaticMeshAssetData& asset, float tolerance);
    static Report RemoveDegenerate(engine::StaticMeshAssetData& asset, float areaTolerance);
    static Report BridgeLoops(engine::StaticMeshAssetData& asset, std::size_t subMesh,
                              const std::vector<std::uint32_t>& first,
                              const std::vector<std::uint32_t>& second);
    static void RecalculateNormalsAndTangents(engine::StaticMeshAssetData& asset);
    static void RecalculateBounds(engine::StaticMeshAssetData& asset);
    static TopologyReport AnalyzeTopology(const engine::StaticMeshAssetData& asset,
                                          float tolerance = 1e-8f);
    static bool Validate(const engine::StaticMeshAssetData& asset, std::string* error = nullptr);
};
