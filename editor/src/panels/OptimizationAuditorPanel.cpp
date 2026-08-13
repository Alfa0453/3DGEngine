#include "OptimizationAuditorPanel.h"

#include "EditorPanels.h"
#include "EditorScene.h"

#include <engine/assets/RuntimeAssetManager.h>
#include <engine/ecs/Components.h>
#include <engine/graphics/Model.h>
#include <engine/graphics/SkinnedModel.h>
#include <engine/graphics/Texture.h>
#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace {
using editor::optimization::Finding;
using editor::optimization::Severity;

ImVec4 SeverityColor(Severity severity) {
    switch (severity) {
    case Severity::Info: return ImVec4(0.38f, 0.72f, 1.0f, 1.0f);
    case Severity::Warning: return ImVec4(1.0f, 0.72f, 0.18f, 1.0f);
    case Severity::Critical: return ImVec4(1.0f, 0.25f, 0.2f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
}

bool AssetExists(const std::string& path, const std::string& root) {
    if (path.empty()) return true;
    if (path.rfind("/Game/", 0) == 0 || path.rfind("Game/", 0) == 0)
        return true;
    std::error_code ec;
    const std::filesystem::path input(path);
    if (input.is_absolute()) return std::filesystem::is_regular_file(input, ec);
    if (std::filesystem::is_regular_file(std::filesystem::path(root) / input, ec))
        return true;
    ec.clear();
    return std::filesystem::is_regular_file(
        std::filesystem::path(root).parent_path() / input, ec);
}

std::string CountText(std::size_t value) {
    std::ostringstream out;
    out << value;
    return out.str();
}
} // namespace

void OptimizationAuditorPanel::Scan(const EditorScene& scene,
                                    engine::RuntimeAssetManager& assets,
                                    const std::string& assetRoot) {
    m_findings.clear();
    m_selected = -1;
    m_scanned = true;

    std::size_t totalTriangles = 0;
    std::size_t totalVertices = 0;
    std::size_t estimatedDrawCalls = 0;
    std::size_t shadowLights = 0;
    std::size_t colliders = 0;
    std::size_t totalParticles = 0;
    std::size_t totalFoliage = 0;
    std::unordered_set<std::string> auditedMaterials;
    std::unordered_set<std::string> auditedTextures;

    auto add = [&](Severity severity, const std::string& category, int index,
                   const std::string& message, const std::string& recommendation,
                   double impact = 0.0, int quickFix = None) {
        Finding finding;
        finding.severity = severity;
        finding.category = category;
        finding.objectIndex = index;
        finding.objectName = index >= 0
            && index < static_cast<int>(scene.Objects().size())
            ? scene.Objects()[static_cast<std::size_t>(index)].name : "Scene";
        finding.message = message;
        finding.recommendation = recommendation;
        finding.estimatedImpact = impact;
        finding.quickFix = quickFix;
        m_findings.push_back(std::move(finding));
    };

    auto auditTexture = [&](const std::string& path, int objectIndex) {
        if (path.empty() || !auditedTextures.insert(path).second) return;
        if (!AssetExists(path, assetRoot)) {
            add(Severity::Critical, "Assets", objectIndex,
                "Texture asset is missing: " + path,
                "Reimport the texture or repair the material reference.", 100.0);
            return;
        }
        std::string error;
        const engine::Texture* texture = assets.LoadTexture(path, &error);
        if (!texture) {
            add(Severity::Critical, "Assets", objectIndex,
                "Texture could not be loaded: " + path,
                error.empty() ? "Reimport this texture." : error, 100.0);
            return;
        }
        const int largest = std::max(texture->Width(), texture->Height());
        if (largest >= 8192)
            add(Severity::Critical, "Textures", objectIndex,
                "Texture is " + std::to_string(texture->Width()) + " x "
                    + std::to_string(texture->Height()) + ".",
                "Reduce it to 4096 or smaller unless it is a justified hero asset.",
                static_cast<double>(largest));
        else if (largest >= 4096)
            add(Severity::Warning, "Textures", objectIndex,
                "Texture is " + std::to_string(texture->Width()) + " x "
                    + std::to_string(texture->Height()) + ".",
                "Consider 2048 for ordinary props and tiled surfaces.",
                static_cast<double>(largest));
    };

    auto auditMaterial = [&](const std::string& path, int objectIndex) {
        if (path.empty() || !auditedMaterials.insert(path).second) return;
        if (!AssetExists(path, assetRoot)) {
            add(Severity::Critical, "Assets", objectIndex,
                "Material asset is missing: " + path,
                "Repair or replace the missing material reference.", 100.0);
            return;
        }
        if (std::filesystem::path(path).extension() != ".3dgmat") {
            auditTexture(path, objectIndex);
            return;
        }
        std::string error;
        const engine::RuntimeMaterialAsset* material = assets.LoadMaterial(path, &error);
        if (!material) {
            add(Severity::Critical, "Assets", objectIndex,
                "Material could not be decoded: " + path,
                error.empty() ? "Open and resave this material." : error, 100.0);
            return;
        }
        auditTexture(material->albedoMapPath, objectIndex);
        auditTexture(material->normalMapPath, objectIndex);
        auditTexture(material->metalRoughMapPath, objectIndex);
        auditTexture(material->heightMapPath, objectIndex);
        if (!material->shaderPath.empty() && !AssetExists(material->shaderPath, assetRoot))
            add(Severity::Critical, "Assets", objectIndex,
                "Material custom shader is missing: " + material->shaderPath,
                "Repair the shader reference or return the material to Standard PBR.",
                100.0);
    };

    const EditorScene::Environment& environment = scene.GetEnvironment();
    for (int index = 0; index < static_cast<int>(scene.Objects().size()); ++index) {
        const EditorScene::Object& object =
            scene.Objects()[static_cast<std::size_t>(index)];
        if (!object.visible) continue;

        std::size_t triangles = 0;
        std::size_t vertices = 0;
        std::size_t materialSlots = 1;
        bool hasLods = false;
        bool geometryLoaded = true;
        if (!object.modelAssetPath.empty()) {
            if (!AssetExists(object.modelAssetPath, assetRoot)) {
                geometryLoaded = false;
                add(Severity::Critical, "Assets", index,
                    "Mesh asset is missing: " + object.modelAssetPath,
                    "Reimport the mesh or repair the object reference.", 100.0);
            } else {
                std::string error;
                if (object.skeletalModel) {
                    if (const engine::SkinnedModel* model =
                            assets.LoadSkinnedModel(object.modelAssetPath, &error)) {
                        triangles = model->TriangleCount();
                        vertices = model->VertexCount();
                        materialSlots = model->SubMeshCount();
                    } else geometryLoaded = false;
                } else if (const engine::Model* model =
                               assets.LoadModel(object.modelAssetPath, &error)) {
                    triangles = model->TriangleCount();
                    vertices = model->VertexCount();
                    materialSlots = model->SubMeshCount();
                    hasLods = !model->SubMeshes().empty()
                        && std::all_of(model->SubMeshes().begin(), model->SubMeshes().end(),
                            [](const engine::SubMesh& sub) { return sub.mesh.MaxLod() > 0; });
                } else geometryLoaded = false;
                if (!geometryLoaded)
                    add(Severity::Critical, "Assets", index,
                        "Mesh could not be loaded: " + object.modelAssetPath,
                        error.empty() ? "Reimport this mesh." : error, 100.0);
            }
        } else if (const engine::ecs::MeshRenderer* renderer =
                       scene.TryGetMeshRenderer(object.entity)) {
            if (renderer->mesh) {
                triangles = renderer->mesh->TriangleCount();
                vertices = renderer->mesh->VertexCount();
                hasLods = renderer->mesh->MaxLod() > 0;
            }
        }
        if (geometryLoaded) {
            totalTriangles += triangles;
            totalVertices += vertices;
            estimatedDrawCalls += std::max<std::size_t>(materialSlots, 1);
            const Severity meshSeverity = editor::optimization::ClassifyCount(
                triangles, 100000, 500000);
            if (meshSeverity != Severity::Info)
                add(meshSeverity, "Geometry", index,
                    "Mesh contributes " + CountText(triangles) + " triangles.",
                    "Reduce topology, split distant detail, or author lower LODs.",
                    static_cast<double>(triangles));
            if (!object.skeletalModel && triangles >= 50000 && !hasLods)
                add(Severity::Warning, "LODs", index,
                    "High-detail mesh has no lower LOD levels.",
                    "Generate or import at least one reduced LOD.",
                    static_cast<double>(triangles));
            if (materialSlots > 4)
                add(materialSlots >= 9 ? Severity::Critical : Severity::Warning,
                    "Draw Calls", index,
                    "Mesh uses " + CountText(materialSlots)
                        + " submeshes/material sections.",
                    "Merge compatible material sections or atlas their textures.",
                    static_cast<double>(materialSlots));
        }

        auditMaterial(object.materialAssetPath, index);

        if (object.light) {
            bool casts = false;
            using Type = engine::ecs::Light::Type;
            if (object.lightData.type == Type::Directional)
                casts = environment.directionalShadows;
            else if (object.lightData.type == Type::Point)
                casts = environment.pointShadows;
            else if (object.lightData.type == Type::Spot)
                casts = environment.spotShadows;
            if (casts) {
                ++shadowLights;
                estimatedDrawCalls += object.lightData.type == Type::Point ? 6 : 1;
            }
        }
        if (object.colliderEnabled) {
            ++colliders;
            if (object.rigidBodyEnabled
                && (object.collider.shape == engine::ecs::ColliderShape::Torus
                    || object.collider.shape == engine::ecs::ColliderShape::Staircase))
                add(Severity::Warning, "Physics", index,
                    "Dynamic body uses a comparatively expensive collider shape.",
                    "Prefer a capsule, box, sphere, or a small compound proxy.", 25.0);
        }
        if (object.isTerrain) {
            const std::size_t terrainTriangles = object.terrainRes > 1
                ? static_cast<std::size_t>(object.terrainRes - 1)
                    * static_cast<std::size_t>(object.terrainRes - 1) * 2u : 0u;
            if (object.terrainRes >= 1024)
                add(Severity::Critical, "Terrain", index,
                    "Terrain resolution is " + std::to_string(object.terrainRes)
                        + " (about " + CountText(terrainTriangles) + " triangles).",
                    "Use terrain chunks and distance LODs, or reduce resolution to 512.",
                    static_cast<double>(terrainTriangles));
            else if (object.terrainRes >= 512)
                add(Severity::Warning, "Terrain", index,
                    "Terrain resolution is " + std::to_string(object.terrainRes) + ".",
                    "Profile sculpting and rendering; consider a 256-512 base landscape.",
                    static_cast<double>(terrainTriangles));
        }
        if (object.isWater && object.waterResolution >= 256)
            add(object.waterResolution >= 512 ? Severity::Critical : Severity::Warning,
                "Water", index,
                "Water resolution is " + std::to_string(object.waterResolution) + ".",
                "Reduce tessellation or divide large water into distance-aware regions.",
                static_cast<double>(object.waterResolution));
        if (object.particleSystemEnabled) {
            totalParticles += static_cast<std::size_t>(object.particleConfig.maxParticles);
            if (object.particleConfig.maxParticles >= 20000)
                add(object.particleConfig.maxParticles >= 50000
                        ? Severity::Critical : Severity::Warning,
                    "Particles", index,
                    "Emitter permits "
                        + std::to_string(object.particleConfig.maxParticles)
                        + " live particles.",
                    "Cap this emitter at 5,000, then raise it only after profiling.",
                    static_cast<double>(object.particleConfig.maxParticles), CapParticles);
            if (object.particleConfig.rate >= 5000.0f)
                add(Severity::Warning, "Particles", index,
                    "Spawn rate is " + std::to_string(
                        static_cast<int>(object.particleConfig.rate)) + " particles/sec.",
                    "Lower continuous rate and use short bursts where possible.",
                    object.particleConfig.rate);
        }
        if (object.isFoliage) {
            totalFoliage += object.foliageInstances.size();
            if (object.foliageInstances.size() >= 50000)
                add(Severity::Critical, "Foliage", index,
                    "Foliage actor contains " + CountText(object.foliageInstances.size())
                        + " instances.",
                    "Split into streamed cells and use aggressive culling/LODs.",
                    static_cast<double>(object.foliageInstances.size()));
            else if (object.foliageInstances.size() >= 15000)
                add(Severity::Warning, "Foliage", index,
                    "Foliage actor contains " + CountText(object.foliageInstances.size())
                        + " instances.",
                    "Verify cull distance, LOD meshes, and shadow settings.",
                    static_cast<double>(object.foliageInstances.size()));
        }
    }

    if (shadowLights >= 8)
        add(Severity::Critical, "Lighting", -1,
            "Scene has " + CountText(shadowLights) + " shadow-casting lights.",
            "Disable shadows on decorative lights and limit overlapping point lights.",
            static_cast<double>(shadowLights));
    else if (shadowLights >= 4)
        add(Severity::Warning, "Lighting", -1,
            "Scene has " + CountText(shadowLights) + " shadow-casting lights.",
            "Profile shadow passes and keep only gameplay-relevant shadows.",
            static_cast<double>(shadowLights));
    if (estimatedDrawCalls >= 2000)
        add(Severity::Critical, "Draw Calls", -1,
            "Estimated scene draw calls: " + CountText(estimatedDrawCalls) + ".",
            "Use instancing, merged static geometry, fewer material sections, and culling.",
            static_cast<double>(estimatedDrawCalls));
    else if (estimatedDrawCalls >= 800)
        add(Severity::Warning, "Draw Calls", -1,
            "Estimated scene draw calls: " + CountText(estimatedDrawCalls) + ".",
            "Batch repeated assets and reduce material sections.",
            static_cast<double>(estimatedDrawCalls));
    if (colliders >= 1000)
        add(colliders >= 3000 ? Severity::Critical : Severity::Warning,
            "Physics", -1, "Scene has " + CountText(colliders) + " colliders.",
            "Use collision only where gameplay needs it and stream distant regions.",
            static_cast<double>(colliders));
    if (totalTriangles >= 5000000)
        add(Severity::Critical, "Geometry", -1,
            "Visible scene contains about " + CountText(totalTriangles) + " triangles.",
            "Add LODs, occlusion/streaming cells, and reduce high-cost meshes.",
            static_cast<double>(totalTriangles));
    else if (totalTriangles >= 2000000)
        add(Severity::Warning, "Geometry", -1,
            "Visible scene contains about " + CountText(totalTriangles) + " triangles.",
            "Review the largest objects and missing LOD findings.",
            static_cast<double>(totalTriangles));

    editor::optimization::SortFindings(&m_findings);
    int critical = 0, warnings = 0;
    for (const Finding& finding : m_findings) {
        if (finding.severity == Severity::Critical) ++critical;
        else if (finding.severity == Severity::Warning) ++warnings;
    }
    std::ostringstream summary;
    summary << scene.Objects().size() << " objects | " << totalTriangles
            << " triangles | " << totalVertices << " vertices | ~"
            << estimatedDrawCalls << " draw calls | " << totalParticles
            << " particle capacity | " << totalFoliage << " foliage | "
            << critical << " critical | " << warnings << " warnings";
    m_summary = summary.str();
    m_status = "Scan completed.";
}

bool OptimizationAuditorPanel::ApplyQuickFix(
    EditorScene& scene, const Finding& finding) {
    if (finding.objectIndex < 0
        || finding.objectIndex >= static_cast<int>(scene.Objects().size())) return false;
    scene.SelectIndex(finding.objectIndex);
    const EditorScene::Object* object = scene.SelectedObject();
    if (!object || object->locked) return false;
    if (finding.quickFix == CapParticles && object->particleSystemEnabled) {
        engine::ParticleSystemComponent settings;
        settings.assetId = object->particleAssetId;
        settings.config = object->particleConfig;
        settings.config.maxParticles = std::min(settings.config.maxParticles, 5000);
        settings.enabled = true;
        settings.autoplay = object->particleAutoplay;
        settings.loop = object->particleLoop;
        settings.prewarm = object->particlePrewarm;
        settings.duration = object->particleDuration;
        settings.startDelay = object->particleStartDelay;
        settings.simulationSpeed = object->particleSimulationSpeed;
        settings.localSpace = object->particleLocalSpace;
        settings.burstCount = object->particleBurstCount;
        settings.burstInterval = object->particleBurstInterval;
        return scene.SetSelectedParticleSystem(true, settings);
    }
    return false;
}

bool OptimizationAuditorPanel::Export(const std::string& assetRoot, bool json) {
    const std::filesystem::path path = std::filesystem::path(assetRoot)
        / "Reports" / (json ? "OptimizationAudit.json" : "OptimizationAudit.txt");
    std::string error;
    const bool saved = json
        ? editor::optimization::WriteJsonReport(path.string(), m_findings, m_summary, &error)
        : editor::optimization::WriteTextReport(path.string(), m_findings, m_summary, &error);
    m_status = saved ? "Saved report: " + path.string() : error;
    return saved;
}

int OptimizationAuditorPanel::ConsumeFrameRequest() {
    const int request = m_frameRequest;
    m_frameRequest = -1;
    return request;
}

void OptimizationAuditorPanel::Draw(EditorScene& scene,
                                    engine::RuntimeAssetManager& assets,
                                    const std::string& assetRoot, bool* open) {
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::OptimizationAuditor), open)) {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("On-demand level performance scan. It never reloads or "
                       "walks the project every frame.");
    if (ImGui::Button("Scan Level")) Scan(scene, assets, assetRoot);
    if (!m_scanned) ImGui::BeginDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Export Text")) Export(assetRoot, false);
    ImGui::SameLine();
    if (ImGui::Button("Export JSON")) Export(assetRoot, true);
    if (!m_scanned) ImGui::EndDisabled();

    ImGui::TextWrapped("%s", m_summary.c_str());
    int critical = 0, warnings = 0, info = 0;
    for (const Finding& finding : m_findings) {
        if (finding.severity == Severity::Critical) ++critical;
        else if (finding.severity == Severity::Warning) ++warnings;
        else ++info;
    }
    ImGui::TextColored(SeverityColor(Severity::Critical), "%d critical", critical);
    ImGui::SameLine();
    ImGui::TextColored(SeverityColor(Severity::Warning), "%d warnings", warnings);
    ImGui::SameLine();
    ImGui::TextColored(SeverityColor(Severity::Info), "%d info", info);
    ImGui::Checkbox("Critical", &m_showCritical); ImGui::SameLine();
    ImGui::Checkbox("Warnings", &m_showWarnings); ImGui::SameLine();
    ImGui::Checkbox("Info", &m_showInfo);

    if (ImGui::BeginChild("##OptimizationFindings", ImVec2(0, -155.0f), true)) {
        for (int i = 0; i < static_cast<int>(m_findings.size()); ++i) {
            const Finding& finding = m_findings[static_cast<std::size_t>(i)];
            if ((finding.severity == Severity::Critical && !m_showCritical)
                || (finding.severity == Severity::Warning && !m_showWarnings)
                || (finding.severity == Severity::Info && !m_showInfo)) continue;
            ImGui::PushID(i);
            ImGui::TextColored(SeverityColor(finding.severity), "[%s]",
                editor::optimization::SeverityName(finding.severity));
            ImGui::SameLine();
            const std::string label = finding.category + " | " + finding.objectName
                + " | " + finding.message;
            if (ImGui::Selectable(label.c_str(), m_selected == i)) m_selected = i;
            ImGui::PopID();
        }
        if (m_scanned && m_findings.empty())
            ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.5f, 1.0f),
                               "No threshold violations found.");
    }
    ImGui::EndChild();

    if (m_selected >= 0 && m_selected < static_cast<int>(m_findings.size())) {
        const Finding finding = m_findings[static_cast<std::size_t>(m_selected)];
        ImGui::TextWrapped("%s", finding.message.c_str());
        ImGui::TextWrapped("Recommended: %s", finding.recommendation.c_str());
        if (finding.objectIndex >= 0) {
            if (ImGui::Button("Select Object")) scene.SelectIndex(finding.objectIndex);
            ImGui::SameLine();
            if (ImGui::Button("Select + Frame")) {
                scene.SelectIndex(finding.objectIndex);
                m_frameRequest = finding.objectIndex;
            }
        }
        if (finding.quickFix != None) {
            if (finding.objectIndex >= 0) ImGui::SameLine();
            if (ImGui::Button("Apply Safe Fix")) {
                m_status = ApplyQuickFix(scene, finding)
                    ? "Safe fix applied. Scan again to refresh results."
                    : "Could not apply fix; the object may be locked.";
            }
        }
    } else ImGui::TextDisabled("Select a finding to see its recommended fix.");
    if (!m_status.empty()) ImGui::TextWrapped("%s", m_status.c_str());
    ImGui::TextDisabled("Reports are saved under Content/Reports.");
    ImGui::End();
}
