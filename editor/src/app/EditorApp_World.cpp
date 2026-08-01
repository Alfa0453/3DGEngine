// EditorApp - the World Editor panel + world cook.
//
// Composes a streamed world (.3dgworld) from editor level scenes: an always-resident
// persistent level plus streamed levels, each placed at a world offset with a streaming
// rule. "Cook World" exports every referenced editor scene to a runtime scene and writes
// a cooked .3dgworld the runtime can boot (see RuntimePlayerApp + LevelStreamingManager).

#include "EditorApp.h"
#include "EditorScene.h"
#include "NativeDialog.h"
#include "RuntimeSceneExporter.h"

#include <engine/assets/AssetCooker.h>
#include <engine/scene/WorldManifest.h>

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unordered_set>

namespace {

// Editable text field backed by a std::string (copied in/out of a scratch buffer).
bool EditPath(const char* label, std::string& value) {
    std::array<char, 512> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
    if (ImGui::InputText(label, buffer.data(), buffer.size())) {
        value = buffer.data();
        return true;
    }
    return false;
}

const char* kRuleNames[] = { "Distance", "Always Loaded", "Manual" };

void EditLevelPlacement(engine::LevelRef& level) {
    glm::vec3 translation(level.worldTransform[3]);
    glm::vec3 scale(1.0f);
    glm::mat3 rotationBasis(level.worldTransform);
    for (int axis = 0; axis < 3; ++axis) {
        scale[axis] = glm::length(rotationBasis[axis]);
        if (scale[axis] > 0.000001f)
            rotationBasis[axis] /= scale[axis];
        else
            rotationBasis[axis] = glm::mat3(1.0f)[axis];
    }
    if (glm::determinant(rotationBasis) < 0.0f) {
        scale.x = -scale.x;
        rotationBasis[0] = -rotationBasis[0];
    }
    const glm::quat rotation =
        glm::normalize(glm::quat_cast(rotationBasis));
    glm::vec3 eulerDegrees = glm::degrees(glm::eulerAngles(rotation));
    bool changed = ImGui::DragFloat3(
        "Location", &translation.x, 0.1f);
    changed |= ImGui::DragFloat3(
        "Rotation", &eulerDegrees.x, 0.25f, -360.0f, 360.0f, "%.2f deg");
    changed |= ImGui::DragFloat3(
        "Scale", &scale.x, 0.01f, -1000.0f, 1000.0f);
    if (changed) {
        for (int axis = 0; axis < 3; ++axis) {
            if (glm::abs(scale[axis]) < 0.001f)
                scale[axis] = scale[axis] < 0.0f ? -0.001f : 0.001f;
        }
        level.worldTransform =
            glm::translate(glm::mat4(1.0f), translation)
            * glm::mat4_cast(glm::quat(glm::radians(eulerDegrees)))
            * glm::scale(glm::mat4(1.0f), scale);
    }
}

}  // namespace

void EditorApp::DrawWorldEditorPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::WorldEditor)) return;

    bool open = true;
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::WorldEditor), &open)) {
        ImGui::End();
        m_panels.SetOpen(EditorPanels::Panel::WorldEditor, open);
        return;
    }

    ImGui::TextDisabled("Compose a streamed world from editor level scenes, then Cook it "
                        "into a runnable .3dgworld.");
    ImGui::Separator();

    // Authoring file: save/load the world composition (level scene references + placement).
    if (ImGui::Button("New World")) {
        m_worldAuthoring = engine::WorldManifest{};
        m_worldAuthoring.id = engine::AssetHandle::Generate();
        m_worldAuthoringPath =
            (std::filesystem::path(m_project.AssetRoot())
             / "Assets" / "Worlds" / "NewWorld.3dgworld").string();
        if (m_worldCookOutputDir.empty()) {
            m_worldCookOutputDir =
                (std::filesystem::path(m_project.AssetRoot()).parent_path()
                 / "Build" / "Cooked" / "World").string();
        }
        m_worldStatus = "New world. Choose a persistent scene and save it.";
    }
    ImGui::SameLine();
    EditPath("World File (.3dgworld)", m_worldAuthoringPath);
    ImGui::SameLine();
    if (ImGui::Button("Browse##world")) {
        const std::string path =
            editor::OpenFileDialog("Open streamed world", "3DG World", "3dgworld");
        if (!path.empty()) m_worldAuthoringPath = path;
    }
    if (ImGui::Button("Save World") && !m_worldAuthoringPath.empty()) {
        std::string err;
        std::error_code ec;
        std::filesystem::create_directories(
            std::filesystem::path(m_worldAuthoringPath).parent_path(), ec);
        if (ec) {
            m_worldStatus = "Save failed: " + ec.message();
        } else {
            if (!m_worldAuthoring.id.Valid())
                m_worldAuthoring.id = engine::AssetHandle::Generate();
            m_worldStatus =
                engine::SaveWorldManifest(
                    m_worldAuthoringPath, m_worldAuthoring, &err)
                ? "Saved " + m_worldAuthoringPath
                : "Save failed: " + err;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load World") && !m_worldAuthoringPath.empty()) {
        std::string err;
        m_worldStatus = engine::LoadWorldManifest(m_worldAuthoringPath, &m_worldAuthoring, &err)
            ? "Loaded " + m_worldAuthoringPath
            : "Load failed: " + err;
    }

    ImGui::SeparatorText("Persistent Level");
    EditPath("Scene##persistent", m_worldAuthoring.persistentScenePath);
    ImGui::SameLine();
    if (ImGui::Button("Browse##persistent")) {
        const std::string path =
            editor::OpenFileDialog("Choose persistent level", "3DG Scene", "scene");
        if (!path.empty()) m_worldAuthoring.persistentScenePath = path;
    }
    ImGui::TextDisabled("Always resident (player start, global systems). Loaded once at boot.");

    ImGui::SeparatorText("Streamed Levels");
    int removeIndex = -1;
    for (std::size_t i = 0; i < m_worldAuthoring.levels.size(); ++i) {
        engine::LevelRef& level = m_worldAuthoring.levels[i];
        ImGui::PushID(static_cast<int>(i));

        std::string header = level.scenePath.empty()
            ? ("Level " + std::to_string(i))
            : std::filesystem::path(level.scenePath).filename().string();
        if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            EditPath("Scene", level.scenePath);
            ImGui::SameLine();
            if (ImGui::Button("Browse")) {
                const std::string path =
                    editor::OpenFileDialog("Choose streamed level", "3DG Scene", "scene");
                if (!path.empty()) level.scenePath = path;
            }

            EditLevelPlacement(level);

            int rule = static_cast<int>(level.rule);
            if (ImGui::Combo("Rule", &rule, kRuleNames, IM_ARRAYSIZE(kRuleNames))) {
                level.rule = static_cast<engine::LevelStreamRule>(rule);
            }
            if (level.rule == engine::LevelStreamRule::Distance) {
                ImGui::DragFloat("Load Radius", &level.loadRadius, 0.5f, 0.0f, 10000.0f, "%.1f");
                ImGui::DragFloat("Unload Radius", &level.unloadRadius, 0.5f, 0.0f, 10000.0f, "%.1f");
                if (level.unloadRadius < level.loadRadius) level.unloadRadius = level.loadRadius;
            }
            if (ImGui::SmallButton("Remove")) removeIndex = static_cast<int>(i);
            ImGui::TreePop();
        }
        ImGui::PopID();
        ImGui::Separator();
    }
    if (removeIndex >= 0) {
        m_worldAuthoring.levels.erase(m_worldAuthoring.levels.begin() + removeIndex);
    }
    if (ImGui::Button("+ Add Level")) {
        m_worldAuthoring.levels.push_back(engine::LevelRef{});
    }

    ImGui::SeparatorText("Cook");
    EditPath("Output Folder", m_worldCookOutputDir);
    ImGui::SameLine();
    if (ImGui::Button("Browse##cook")) {
        const std::string path =
            editor::PickFolderDialog("Choose world package output");
        if (!path.empty()) m_worldCookOutputDir = path;
    }
    if (ImGui::Button("Cook World")) {
        if (m_worldCookOutputDir.empty()) {
            m_worldStatus = "Set an output folder first.";
        } else {
            m_content.Refresh(m_assets, m_project, m_log);
            const std::string authoringDir =
                std::filesystem::path(m_worldAuthoringPath).parent_path().string();
            std::string err;
            m_worldStatus = CookWorld(m_worldAuthoring, authoringDir, m_worldCookOutputDir, &err)
                ? "Cooked world to " + m_worldCookOutputDir
                : "Cook failed: " + err;
        }
    }
    if (!m_worldStatus.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", m_worldStatus.c_str());
    }

    ImGui::End();
    m_panels.SetOpen(EditorPanels::Panel::WorldEditor, open);
}

bool EditorApp::CookWorld(const engine::WorldManifest& authoring, const std::string& authoringDir,
                          const std::string& outputDir, std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path output = fs::absolute(outputDir).lexically_normal();
    const fs::path sourceDir =
        output.parent_path() / (output.filename().string() + ".worldsource");
    fs::remove_all(sourceDir, ec);
    ec.clear();
    fs::create_directories(sourceDir, ec);
    if (ec) {
        if (error) *error = "could not create world cook staging folder: " + ec.message();
        return false;
    }

    const auto resolveIn = [&](const std::string& rel) -> std::string {
        const fs::path p(rel);
        if (p.is_absolute() || authoringDir.empty()) return rel;
        return (fs::path(authoringDir) / rel).string();
    };

    std::unordered_set<std::string> runtimeNames;

    // Load an editor scene, export it to the source staging folder, and report the
    // cooked file's relative name plus the scene's local AABB (for streaming distance).
    const auto cookScene = [&](const std::string& editorScenePath, std::string* cookedRel,
                               engine::AssetHandle* sceneId, glm::vec3* boundsMin,
                               glm::vec3* boundsMax, std::string* err) -> bool {
        if (editorScenePath.empty()) { if (err) *err = "empty scene path"; return false; }
        EditorScene scene;
        if (!scene.Load(resolveIn(editorScenePath), *m_cube, *m_plane, *m_sphere, *m_capsule,
                        *m_cylinder, *m_cone, *m_pyramid, *m_torus, *m_staircase, err)) {
            return false;
        }
        glm::vec3 mn(0.0f), mx(0.0f);
        bool any = false;
        for (const EditorScene::Object& object : scene.Objects()) {
            const engine::ecs::Transform* t = scene.TryGetTransform(object.entity);
            if (!t) continue;
            const glm::vec3 p = t->position;
            // Conservative bounds from object scale. This is intentionally a little
            // generous: early loading is preferable to a visible streaming pop.
            const glm::vec3 extent =
                glm::max(glm::abs(t->scale) * 0.5f, glm::vec3(0.25f));
            const glm::vec3 objectMin = p - extent;
            const glm::vec3 objectMax = p + extent;
            mn = any ? glm::min(mn, objectMin) : objectMin;
            mx = any ? glm::max(mx, objectMax) : objectMax;
            any = true;
        }
        if (any) { mn -= glm::vec3(2.0f); mx += glm::vec3(2.0f); }   // small margin
        *boundsMin = mn;
        *boundsMax = mx;
        *sceneId = scene.AssetId();

        const std::string cookedName =
            fs::path(editorScenePath).stem().string() + ".runtime.scene";
        if (!runtimeNames.insert(cookedName).second) {
            if (err) *err = "two levels produce the same runtime file name: " + cookedName;
            return false;
        }
        const std::string cookedFull = (sourceDir / cookedName).string();
        if (!RuntimeSceneExporter::Export(scene, cookedFull, err)) return false;
        *cookedRel = cookedName;
        return true;
    };

    engine::WorldManifest cooked;
    cooked.id = authoring.id.Valid() ? authoring.id : engine::AssetHandle::Generate();

    // Persistent level.
    {
        std::string rel, err;
        engine::AssetHandle sceneId;
        glm::vec3 mn, mx;
        if (!cookScene(
                authoring.persistentScenePath, &rel, &sceneId, &mn, &mx, &err)) {
            fs::remove_all(sourceDir, ec);
            if (error) *error = "persistent level: " + err;
            return false;
        }
        cooked.persistentScenePath = rel;
    }

    // Streamed levels (carry over placement/rule/radii, replace path + bounds).
    for (const engine::LevelRef& level : authoring.levels) {
        std::string rel, err;
        engine::AssetHandle sceneId;
        glm::vec3 mn, mx;
        if (!cookScene(level.scenePath, &rel, &sceneId, &mn, &mx, &err)) {
            fs::remove_all(sourceDir, ec);
            if (error) *error = "level '" + level.scenePath + "': " + err;
            return false;
        }
        engine::LevelRef out = level;
        out.scenePath = rel;
        out.sceneId = sceneId;
        out.boundsMin = mn;
        out.boundsMax = mx;
        out.loadRadius = std::max(out.loadRadius, 0.0f);
        out.unloadRadius = std::max(out.unloadRadius, out.loadRadius);
        cooked.levels.push_back(out);
    }

    const fs::path sourceWorldPath = sourceDir / "world.3dgworld";
    if (!engine::SaveWorldManifest(sourceWorldPath.string(), cooked, error)) {
        fs::remove_all(sourceDir, ec);
        return false;
    }
    engine::AssetCookResult result;
    const bool succeeded = engine::AssetCooker::CookRuntimeWorld(
        m_project.AssetRoot(), sourceWorldPath.string(), output.string(),
        m_assetRegistry, &result, error);
    fs::remove_all(sourceDir, ec);
    if (!succeeded) return false;
    m_log.Info("Cooked streamed world startup: " + result.runtimeScenePath);
    m_log.Info("Cooked " + std::to_string(cooked.levels.size())
        + " streamed level(s), " + std::to_string(result.assets.size())
        + " required engine asset(s)");
    if (error) error->clear();
    return true;
}
