// EditorApp — "Merge to Single Mesh" (UE5 "Merge Actors" equivalent).
//
// Takes the current multi-selection of static objects (primitives and static
// .3dgmesh model assets), bakes their world geometry into ONE combined .3dgmesh
// asset, saves it under <project>/Content/Meshes, then replaces the selected
// objects with a single new object that references the baked mesh. The result is
// one entity / one mesh asset — cheapest to render and moved/spawned as a unit —
// exactly the "assemble a house from parts and drop it as one actor" workflow.
//
// Fidelity notes (v1):
//   * Geometry is exact. Each part is transformed by its object world matrix (and,
//     for models, the render-only Model Offset/Orientation) into the merged mesh's
//     local space, pivoted on the selection centroid.
//   * Materials: model parts keep their embedded per-submesh materials + textures;
//     primitive parts become a flat-coloured material from their object colour.
//     A .3dgmat override assigned to an object is NOT baked (the .3dgmesh material
//     format is Phong + embedded maps); re-assign materials on the merged object if
//     needed. Metallic/roughness are not represented by the mesh format.
//   * Skinned/animated meshes, terrain, water, foliage, splines and lights are
//     skipped (they are not static bakeable geometry).

#include "EditorApp.h"

#include <engine/animation/AnimatedModel.h>
#include <engine/assets/AssetIdentity.h>
#include <engine/assets/StaticMeshAsset.h>
#include <engine/ecs/Components.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace {

// The primitive engine::Mesh backing a given object primitive kind.
const engine::Mesh* PrimitiveMeshFor(
    EditorScene::Primitive primitive,
    const std::optional<engine::Mesh>& cube, const std::optional<engine::Mesh>& plane,
    const std::optional<engine::Mesh>& sphere, const std::optional<engine::Mesh>& capsule,
    const std::optional<engine::Mesh>& cylinder, const std::optional<engine::Mesh>& cone,
    const std::optional<engine::Mesh>& pyramid, const std::optional<engine::Mesh>& torus,
    const std::optional<engine::Mesh>& staircase) {
    switch (primitive) {
        case EditorScene::Primitive::Plane:     return plane ? &*plane : nullptr;
        case EditorScene::Primitive::Sphere:    return sphere ? &*sphere : nullptr;
        case EditorScene::Primitive::Capsule:   return capsule ? &*capsule : nullptr;
        case EditorScene::Primitive::Cylinder:  return cylinder ? &*cylinder : nullptr;
        case EditorScene::Primitive::Cone:      return cone ? &*cone : nullptr;
        case EditorScene::Primitive::Pyramid:   return pyramid ? &*pyramid : nullptr;
        case EditorScene::Primitive::Torus:     return torus ? &*torus : nullptr;
        case EditorScene::Primitive::Staircase: return staircase ? &*staircase : nullptr;
        default:                                return cube ? &*cube : nullptr;
    }
}

std::filesystem::path UniqueMeshPath(std::filesystem::path candidate) {
    std::error_code ec;
    if (!std::filesystem::exists(candidate, ec)) return candidate;
    const std::filesystem::path dir = candidate.parent_path();
    const std::string stem = candidate.stem().string();
    for (int i = 1; i < 1000; ++i) {
        std::filesystem::path next = dir / (stem + "_" + std::to_string(i) + ".3dgmesh");
        if (!std::filesystem::exists(next, ec)) return next;
    }
    return candidate;
}

} // namespace

bool EditorApp::MergeSelectedToSingleMesh() {
    const std::vector<int>& selection = m_scene.SelectedIndices();
    const std::vector<EditorScene::Object>& objects = m_scene.Objects();

    // Partition the selection into bakeable static geometry vs. everything skipped.
    std::vector<int> bakeIndices;
    std::vector<std::string> skipped;
    for (int index : selection) {
        if (index < 0 || index >= static_cast<int>(objects.size())) continue;
        const EditorScene::Object& o = objects[static_cast<std::size_t>(index)];
        const bool isModel = !o.modelAssetPath.empty() && !o.skeletalModel;
        const bool isPrimitive = o.modelAssetPath.empty() && !o.light && !o.isTerrain
            && !o.isWater && !o.isFoliage && !o.isSpline && !o.navMeshBoundsVolume
            && !o.skeletalModel && o.primitive != EditorScene::Primitive::Empty;
        if (isModel || isPrimitive) bakeIndices.push_back(index);
        else skipped.push_back(o.name.empty() ? std::string("(unnamed)") : o.name);
    }

    if (bakeIndices.size() < 2) {
        m_log.Warning("Merge to Single Mesh: select at least two static objects "
                      "(primitives or static models) to merge.");
        return false;
    }

    // Pivot = centroid of the parts' world positions; baked geometry is stored
    // relative to it so the new object sits naturally at that point.
    glm::vec3 pivot(0.0f);
    int pivotCount = 0;
    for (int index : bakeIndices) {
        const engine::ecs::Transform* t =
            m_scene.TryGetTransform(objects[static_cast<std::size_t>(index)].entity);
        if (t) { pivot += t->position; ++pivotCount; }
    }
    if (pivotCount > 0) pivot /= static_cast<float>(pivotCount);

    engine::StaticMeshAssetData out;
    out.header.id = engine::AssetHandle::Generate();
    out.header.importerVersion = engine::kStaticMeshImporterVersion;

    glm::vec3 gmin(std::numeric_limits<float>::max());
    glm::vec3 gmax(std::numeric_limits<float>::lowest());

    // Append one submesh: read `strideFloats`-wide interleaved vertices (pos3/norm3/
    // uv2[/tangent3]), transform into pivot-local space, and store as stride-11.
    auto appendSubmesh = [&](const std::vector<float>& src, std::size_t strideFloats,
                             const std::vector<std::uint32_t>& indices,
                             const glm::mat4& worldMat, int materialIndex) {
        if (src.empty() || indices.empty() || strideFloats < 8) return;
        const glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(worldMat)));
        engine::StaticMeshSubMeshData sm;
        sm.material = materialIndex;
        const std::size_t vertexCount = src.size() / strideFloats;
        sm.vertices.reserve(vertexCount * engine::kStaticMeshVertexStride);
        for (std::size_t i = 0; i < vertexCount; ++i) {
            const std::size_t b = i * strideFloats;
            const glm::vec3 p(src[b + 0], src[b + 1], src[b + 2]);
            const glm::vec3 n(src[b + 3], src[b + 4], src[b + 5]);
            const glm::vec2 uv(src[b + 6], src[b + 7]);
            glm::vec3 tan = strideFloats >= 11
                ? glm::vec3(src[b + 8], src[b + 9], src[b + 10])
                : glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 wp = glm::vec3(worldMat * glm::vec4(p, 1.0f)) - pivot;
            glm::vec3 wn = normalMat * n;
            wn = glm::length(wn) > 1e-6f ? glm::normalize(wn) : glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 wt = glm::mat3(worldMat) * tan;
            wt = glm::length(wt) > 1e-6f ? glm::normalize(wt) : glm::vec3(1.0f, 0.0f, 0.0f);
            sm.vertices.insert(sm.vertices.end(),
                { wp.x, wp.y, wp.z, wn.x, wn.y, wn.z, uv.x, uv.y, wt.x, wt.y, wt.z });
            gmin = glm::min(gmin, wp);
            gmax = glm::max(gmax, wp);
        }
        sm.indices = indices;
        out.subMeshes.push_back(std::move(sm));
    };

    int bakedModels = 0, bakedPrimitives = 0;
    for (int index : bakeIndices) {
        const EditorScene::Object& o = objects[static_cast<std::size_t>(index)];
        const engine::ecs::Transform* t = m_scene.TryGetTransform(o.entity);
        const glm::mat4 world = t ? t->Model() : glm::mat4(1.0f);

        if (!o.modelAssetPath.empty()) {
            // Static model part: load its CPU geometry + embedded materials/textures.
            engine::StaticMeshAssetData src;
            std::string err;
            if (!engine::LoadStaticMeshAsset(o.modelAssetPath, &src, &err)) {
                std::error_code ec;
                const std::filesystem::path alt =
                    std::filesystem::path(m_project.AssetRoot()) / o.modelAssetPath;
                if (!engine::LoadStaticMeshAsset(alt.string(), &src, &err)) {
                    m_log.Warning("Merge: skipped '" + o.name + "' (could not load model: "
                                  + err + ")");
                    continue;
                }
            }
            const glm::vec3 center(
                (src.minimum[0] + src.maximum[0]) * 0.5f,
                (src.minimum[1] + src.maximum[1]) * 0.5f,
                (src.minimum[2] + src.maximum[2]) * 0.5f);
            const glm::mat4 modelMat = world * engine::MakeModelRenderOffset(
                o.modelOffsetPosition, o.modelOrientationEuler, o.modelOffsetScale, center);

            const int texBase = static_cast<int>(out.textures.size());
            for (const engine::StaticMeshTextureData& tex : src.textures)
                out.textures.push_back(tex);
            const int matBase = static_cast<int>(out.materials.size());
            for (engine::StaticMeshMaterialData mat : src.materials) {
                if (mat.diffuseMap >= 0)  mat.diffuseMap  += texBase;
                if (mat.normalMap >= 0)   mat.normalMap   += texBase;
                if (mat.specularMap >= 0) mat.specularMap += texBase;
                if (mat.emissiveMap >= 0) mat.emissiveMap += texBase;
                out.materials.push_back(std::move(mat));
            }
            for (const engine::StaticMeshSubMeshData& sub : src.subMeshes) {
                const int mat = sub.material >= 0 ? sub.material + matBase : -1;
                appendSubmesh(sub.vertices, engine::kStaticMeshVertexStride,
                              sub.indices, modelMat, mat);
            }
            ++bakedModels;
        } else {
            // Primitive part: read the shared primitive mesh back from the GPU and
            // bake a flat material from the object's colour.
            const engine::Mesh* pm = PrimitiveMeshFor(o.primitive, m_cube, m_plane,
                m_sphere, m_capsule, m_cylinder, m_cone, m_pyramid, m_torus, m_staircase);
            if (!pm) { continue; }
            const std::vector<float> verts = pm->ReadbackVertices();
            const std::vector<std::uint32_t> idx = pm->ReadbackIndices();
            if (verts.empty() || idx.empty()) {
                m_log.Warning("Merge: skipped primitive '" + o.name + "' (no geometry).");
                continue;
            }
            glm::vec3 color(0.8f);
            if (const engine::ecs::MeshRenderer* mr =
                    m_scene.Registry().TryGet<engine::ecs::MeshRenderer>(o.entity)) {
                color = mr->color;
            }
            engine::StaticMeshMaterialData mat;
            mat.name = (o.name.empty() ? std::string("Part") : o.name) + "_mat";
            mat.diffuse = { color.r, color.g, color.b };
            const int matIndex = static_cast<int>(out.materials.size());
            out.materials.push_back(std::move(mat));
            appendSubmesh(verts, pm->VertexStrideFloats(), idx, world, matIndex);
            ++bakedPrimitives;
        }
    }

    if (out.subMeshes.empty()) {
        m_log.Error("Merge to Single Mesh: no geometry was baked.");
        return false;
    }

    out.minimum = { gmin.x, gmin.y, gmin.z };
    out.maximum = { gmax.x, gmax.y, gmax.z };

    // Save the baked asset under the project's content.
    std::error_code ec;
    const std::filesystem::path dir =
        std::filesystem::path(m_project.AssetRoot()) / "Meshes";
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path path = UniqueMeshPath(dir / "MergedMesh.3dgmesh");
    std::string saveError;
    if (!engine::SaveStaticMeshAsset(path.string(), out, &saveError)) {
        m_log.Error("Merge to Single Mesh: save failed: " + saveError);
        return false;
    }

    // Replace the source objects with one object referencing the baked mesh under a
    // single undo entry: DeleteSelected() captures one pre-merge snapshot, then the
    // rest of the mutations run with undo suppressed so they fold into that entry.
    m_scene.SelectIndex(bakeIndices.front());
    for (std::size_t i = 1; i < bakeIndices.size(); ++i) {
        m_scene.ToggleSelection(bakeIndices[i]);
    }
    m_scene.DeleteSelected();   // pushes the single undo snapshot

    m_scene.SuppressUndo(true);
    // Create the merged object and point it at the new asset.
    m_scene.AddCube(*m_cube);
    m_scene.SetSelectedName(path.stem().string());
    engine::ecs::Transform mergedTransform;
    mergedTransform.position = pivot;
    m_scene.SetSelectedTransform(mergedTransform);
    m_scene.SetSelectedModelAsset(path.string(), out.header.id);
    m_scene.SuppressUndo(false);

    // Resolve so the baked mesh loads and renders immediately, and refresh the
    // content browser so the new asset shows up.
    m_editAssets.ResolveRegistryAssets(m_scene.Registry());
    std::string refreshError;
    m_assets.Refresh(m_project.AssetRoot(), &refreshError);

    std::string summary = "Merged " + std::to_string(bakedModels) + " model(s) + "
        + std::to_string(bakedPrimitives) + " primitive(s) into "
        + path.filename().string();
    if (!skipped.empty()) {
        summary += "  (skipped " + std::to_string(skipped.size()) + ": ";
        for (std::size_t i = 0; i < skipped.size(); ++i) {
            summary += skipped[i];
            if (i + 1 < skipped.size()) summary += ", ";
        }
        summary += ")";
    }
    m_log.Info(summary);
    return true;
}
