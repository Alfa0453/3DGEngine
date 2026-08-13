#include "MeshGeometryOperations.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

void AddVertex(engine::StaticMeshSubMeshData& mesh, float x, float y, float z,
               float u, float v, float red) {
    const float vertex[engine::kStaticMeshVertexStride] = {
        x, y, z, 0.0f, 0.0f, 1.0f, u, v, 1.0f, 0.0f, 0.0f};
    mesh.vertices.insert(mesh.vertices.end(), std::begin(vertex), std::end(vertex));
    mesh.vertexColors.insert(mesh.vertexColors.end(), {red, 0.5f, 0.25f, 1.0f});
}

engine::StaticMeshAssetData Triangle() {
    engine::StaticMeshAssetData asset;
    asset.subMeshes.resize(1);
    auto& mesh = asset.subMeshes[0];
    AddVertex(mesh, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    AddVertex(mesh, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f);
    AddVertex(mesh, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    mesh.indices = {0, 1, 2};
    MeshGeometryOperations::RecalculateBounds(asset);
    return asset;
}

bool PaintMatches(const engine::StaticMeshAssetData& asset) {
    for (const auto& mesh : asset.subMeshes) {
        const std::size_t vertices =
            mesh.vertices.size() / engine::kStaticMeshVertexStride;
        if (!mesh.vertexColors.empty() && mesh.vertexColors.size() != vertices * 4u)
            return false;
    }
    return true;
}

} // namespace

int main() {
    std::string error;
    {
        auto asset = Triangle();
        const auto report = MeshGeometryOperations::ExtrudeFaces(asset, {0}, 0.5f);
        Check(report.changed == 1 && report.verticesAfter == 6
                  && report.trianglesAfter == 7,
              "face extrusion creates a cap and three quad sides");
        Check(asset.maximum[2] == 0.5f && PaintMatches(asset)
                  && MeshGeometryOperations::Validate(asset, &error),
              "extrusion updates bounds and preserves vertex attributes");
    }
    {
        auto asset = Triangle();
        const auto report = MeshGeometryOperations::InsetFaces(asset, {0}, 0.25f);
        Check(report.changed == 1 && report.verticesAfter == 6
                  && report.trianglesAfter == 7 && PaintMatches(asset),
              "face inset creates a center face and connected ring");
    }
    {
        auto asset = Triangle();
        const auto report = MeshGeometryOperations::SubdivideFaces(asset, {0});
        Check(report.changed == 1 && report.verticesAfter == 6
                  && report.trianglesAfter == 4 && PaintMatches(asset),
              "triangle subdivision creates four triangles and interpolates paint");
        Check(MeshGeometryOperations::DeleteFaces(asset, {0}).trianglesAfter == 3,
              "face deletion removes only the selected global face");
    }
    {
        auto asset = Triangle();
        auto& mesh = asset.subMeshes[0];
        AddVertex(mesh, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        mesh.indices.insert(mesh.indices.end(), {3, 3, 1});
        Check(MeshGeometryOperations::WeldVertices(asset, 0.0001f).changed == 1,
              "weld merges coincident vertices");
        Check(asset.subMeshes[0].vertices.size()
                  == 3u * engine::kStaticMeshVertexStride
                  && asset.subMeshes[0].indices.size() == 3u
                  && PaintMatches(asset),
              "weld compacts attributes and removes resulting degenerates");
    }
    {
        auto asset = Triangle();
        auto& mesh = asset.subMeshes[0];
        AddVertex(mesh, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f);
        Check(MeshGeometryOperations::BridgeLoops(asset, 0, {0, 1}, {2, 3})
                      .trianglesAfter == 3,
              "bridge joins equal vertex chains with a quad");
        Check(MeshGeometryOperations::Validate(asset, &error),
              "bridged mesh remains valid");
        const auto topology = MeshGeometryOperations::AnalyzeTopology(asset);
        Check(topology.boundaryEdges > 0 && topology.nonManifoldEdges == 0,
              "topology analysis reports open boundaries without false non-manifold edges");
    }
    {
        auto asset = Triangle();
        asset.subMeshes[0].indices.insert(asset.subMeshes[0].indices.end(), {0, 0, 1});
        Check(MeshGeometryOperations::AnalyzeTopology(asset).degenerateFaces == 1,
              "topology analysis detects degenerate faces");
        Check(MeshGeometryOperations::RemoveDegenerate(asset, 1e-8f).changed == 1
                  && asset.subMeshes[0].indices.size() == 3u,
              "mesh repair removes degenerate faces");
    }

    std::cout << "mesh geometry operation tests passed\n";
    return 0;
}
