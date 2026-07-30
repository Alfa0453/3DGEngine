// EditorApp — the Prefab Editor panel. Authors a reusable object template
// (.3dgprefab): capture a scene object's components, save, load, and (stage 4) stamp
// linked instances. No 3D preview is needed since a prefab is component configuration.

#include "EditorApp.h"

#include <imgui.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

void EditorApp::DrawPrefabEditorPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::Prefab)) return;

    bool open = true;
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::Prefab), &open)) {
        ImGui::End();
        m_panels.SetOpen(EditorPanels::Panel::Prefab, open);
        return;
    }

    ImGui::TextWrapped("Author a reusable object template. Capture a scene object's "
                       "components, save it as a .3dgprefab, then reuse it. Instances keep "
                       "their own position.");
    ImGui::Separator();

    // Name.
    std::array<char, 128> nameBuffer{};
    std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", m_prefabAsset.name.c_str());
    if (ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size())) {
        m_prefabAsset.name = nameBuffer.data();
    }
    ImGui::TextDisabled("File: %s", m_prefabPath.empty() ? "(unsaved)" : m_prefabPath.c_str());

    // New / Capture.
    if (ImGui::Button("New")) {
        m_prefabAsset = PrefabAsset{};
        m_prefabPath.clear();
    }
    ImGui::SameLine();

    const EditorScene::Object* selected = m_scene.SelectedObject();
    if (ImGui::Button("Capture from Selected")) {
        if (selected) {
            m_prefabAsset.Capture(*selected);
            if (m_prefabAsset.name.empty() || m_prefabAsset.name == "Prefab") {
                m_prefabAsset.name = selected->name.empty() ? "Prefab" : selected->name;
            }
            m_log.Info("Captured prefab from " +
                (selected->name.empty() ? std::string("object") : selected->name));
        } else {
            m_log.Warning("Select an object to capture into a prefab.");
        }
    }
    ImGui::SameLine();
    if (selected) {
        ImGui::TextDisabled("(from '%s')", selected->name.c_str());
    } else {
        ImGui::TextDisabled("(no selection)");
    }

    // Save / Load.
    if (ImGui::Button("Save")) {
        std::error_code ec;
        const std::filesystem::path dir =
            std::filesystem::path(m_project.AssetRoot()) / "Prefabs";
        std::filesystem::create_directories(dir, ec);
        const std::string fileName =
            m_prefabAsset.name.empty() ? std::string("Prefab") : m_prefabAsset.name;
        const std::filesystem::path target = m_prefabPath.empty()
            ? (dir / (fileName + ".3dgprefab"))
            : std::filesystem::path(m_prefabPath);
        std::string err;
        if (m_prefabAsset.Save(target.string(), &err)) {
            m_prefabPath = target.string();
            m_log.Info("Saved prefab: " + m_prefabPath);
            std::string refreshErr;
            if (!m_assets.Refresh(m_project.AssetRoot(), &refreshErr)) {
                m_log.Warning(refreshErr);
            }
            SyncPrefabInstances(m_prefabPath, m_prefabAsset);   // push edits to linked instances
        } else {
            m_log.Error("Prefab save failed: " + err);
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo("##loadprefab", "Load...")) {
        std::error_code ec;
        const std::filesystem::path root(m_project.AssetRoot());
        if (std::filesystem::exists(root, ec)) {
            std::filesystem::recursive_directory_iterator it(root,
                std::filesystem::directory_options::skip_permission_denied, ec), end;
            bool any = false;
            while (!ec && it != end) {
                std::error_code fileEc;
                if (it->is_regular_file(fileEc)
                    && it->path().extension() == ".3dgprefab") {
                    any = true;
                    const std::string full = it->path().string();
                    ImGui::PushID(full.c_str());
                    if (ImGui::Selectable(it->path().filename().string().c_str())) {
                        std::string err;
                        if (m_prefabAsset.Load(full, &err)) {
                            m_prefabPath = full;
                            m_log.Info("Loaded prefab: " + full);
                        } else {
                            m_log.Error("Prefab load failed: " + err);
                        }
                    }
                    ImGui::PopID();
                }
                it.increment(ec);
            }
            if (!any) ImGui::TextDisabled("No .3dgprefab files yet.");
        }
        ImGui::EndCombo();
    }

    // Captured summary.
    ImGui::SeparatorText("Captured configuration");
    const EditorScene::Object& o = m_prefabAsset.object;
    ImGui::BulletText("Model: %s", o.modelAssetPath.empty() ? "-" : o.modelAssetPath.c_str());
    ImGui::BulletText("Material: %s", o.materialAssetPath.empty() ? "-" : o.materialAssetPath.c_str());
    const auto flag = [](const char* label, bool on) {
        ImGui::BulletText("%s: %s", label, on ? "yes" : "no");
    };
    flag("Collider", o.colliderEnabled);
    flag("RigidBody", o.rigidBodyEnabled);
    flag("Rotator", o.rotatorEnabled);
    flag("Mover", o.moverEnabled);
    flag("Linear Velocity", o.linearVelocityEnabled);
    flag("Angular Velocity", o.angularVelocityEnabled);
    flag("Health", o.healthEnabled);
    flag("Script", o.scriptEnabled);

    ImGui::Separator();
    if (ImGui::Button("Add to Scene")) {
        glm::vec3 spawn = m_camera.Position() + m_camera.Front() * 6.0f;
        spawn.y = 0.0f;
        AddPrefabToScene(m_prefabAsset, spawn, m_prefabPath);
    }
    ImGui::SameLine();
    if (m_prefabPath.empty()) {
        ImGui::TextDisabled("(save the prefab to enable live-sync of instances)");
    } else {
        ImGui::TextDisabled("(instances re-sync when you Save this prefab)");
    }

    ImGui::End();
    m_panels.SetOpen(EditorPanels::Panel::Prefab, open);
}

void EditorApp::AddPrefabToScene(const PrefabAsset& prefab, const glm::vec3& position,
                                 const std::string& assetPath) {
    if (!m_cube) {
        m_log.Error("Prefab add failed: editor meshes are not ready");
        return;
    }
    const EditorScene::Object& o = prefab.object;

    engine::ecs::Transform transform;
    transform.position = position;

    // Create the base object from the prefab's model if it has one, else the matching
    // primitive mesh, so it renders correctly; then stamp the prefab's components on.
    bool created = false;
    if (!o.modelAssetPath.empty()) {
        created = m_scene.AddModel(o.modelAssetPath, *m_cube, transform);
    }
    if (!created) {
        const engine::Mesh* mesh = nullptr;
        switch (o.primitive) {
        case EditorScene::Primitive::Plane:     mesh = m_plane     ? &*m_plane     : nullptr; break;
        case EditorScene::Primitive::Sphere:    mesh = m_sphere    ? &*m_sphere    : nullptr; break;
        case EditorScene::Primitive::Capsule:   mesh = m_capsule   ? &*m_capsule   : nullptr; break;
        case EditorScene::Primitive::Cylinder:  mesh = m_cylinder  ? &*m_cylinder  : nullptr; break;
        case EditorScene::Primitive::Cone:      mesh = m_cone      ? &*m_cone      : nullptr; break;
        case EditorScene::Primitive::Pyramid:   mesh = m_pyramid   ? &*m_pyramid   : nullptr; break;
        case EditorScene::Primitive::Torus:     mesh = m_torus     ? &*m_torus     : nullptr; break;
        case EditorScene::Primitive::Staircase: mesh = m_staircase ? &*m_staircase : nullptr; break;
        case EditorScene::Primitive::Cube:
        case EditorScene::Primitive::Empty:
        default:                                mesh = &*m_cube; break;
        }
        m_scene.AddConfiguredPrimitive(o.primitive, mesh ? *mesh : *m_cube, transform, nullptr);
        created = m_scene.SelectedObject() != nullptr;
    }
    if (!created) {
        m_log.Warning("Prefab add failed: could not create a scene object");
        return;
    }

    if (prefab.Apply(m_scene)) {
        if (!assetPath.empty()) {
            m_scene.SetSelectedPrefabAssetPath(assetPath, prefab.assetId);
        }
        m_log.Info("Added prefab to scene: "
            + (prefab.name.empty() ? std::string("Prefab") : prefab.name));
    } else {
        m_log.Warning("Prefab add failed: the new object is locked");
    }
}

void EditorApp::SyncPrefabInstances(const std::string& prefabPath, const PrefabAsset& prefab) {
    if (prefabPath.empty()) return;
    const int previousSelection = m_scene.SelectedIndex();
    m_scene.SuppressUndo(true);
    int synced = 0;
    const std::size_t count = m_scene.Objects().size();
    for (std::size_t i = 0; i < count; ++i) {
        if (m_scene.Objects()[i].prefabAssetPath != prefabPath) continue;
        m_scene.SelectIndex(static_cast<int>(i));
        if (prefab.Apply(m_scene)) ++synced;
    }
    m_scene.SelectIndex(previousSelection);
    m_scene.SuppressUndo(false);
    if (synced > 0) {
        m_log.Info("Synced " + std::to_string(synced) + " prefab instance(s)");
    }
}
