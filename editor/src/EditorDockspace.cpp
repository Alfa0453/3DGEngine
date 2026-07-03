#include "EditorDockspace.h"

#include <engine/ecs/Components.h>

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

const char* PrimitiveName(EditorScene::Primitive primitive) {
    return primitive == EditorScene::Primitive::Plane ? "Plane" : "Cube";
}

const char* ObjectTypeName(const EditorScene::Object& object) {
    if(object.light) {
        return "Light";
    }
    return object.modelAssetPath.empty() ? PrimitiveName(object.primitive) : "Model";
}

const char* LightTypeName(engine::ecs::Light::Type type) {
    switch (type) {
    case engine::ecs::Light::Type::Directional: return "Directional";
    case engine::ecs::Light::Type::Point: return "Point";
    case engine::ecs::Light::Type::Spot: return "Spot";
    case engine::ecs::Light::Type::Area: return "Area";
    }
    return "Point";
}

engine::ecs::Light::Type LightTypeFromIndex(int index) {
    switch (index) {
    case 0: return engine::ecs::Light::Type::Directional;
    case 1: return engine::ecs::Light::Type::Point;
    case 2: return engine::ecs::Light::Type::Spot;
    case 3: return engine::ecs::Light::Type::Area;
    }
    return engine::ecs::Light::Type::Point;
}

int LightTypeIndex(engine::ecs::Light::Type type) {
    switch (type) {
    case engine::ecs::Light::Type::Directional: return 0;
    case engine::ecs::Light::Type::Point: return 1;
    case engine::ecs::Light::Type::Spot: return 2;
    case engine::ecs::Light::Type::Area: return 3;
    }
    return 1;
}

bool IsMaterialDocument(const std::filesystem::path& path) {
    return path.extension() == ".3dgmat";
}

std::vector<EditorAssets::Asset> FindMaterialAssets(const EditorAssets& assets) {
    std::vector<EditorAssets::Asset> materials;
    std::error_code ec;
    const std::filesystem::path root = assets.RootPath();
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
        return materials;
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec) || !IsMaterialDocument(entry.path())) {
            continue;
        }

        EditorAssets::Asset material;
        material.relativePath = std::filesystem::relative(entry.path(), root, ec).string();
        if (ec) {
            continue;
        }
        std::replace(material.relativePath.begin(), material.relativePath.end(), '\\', '/');
        material.displayName = entry.path().filename().string();
        material.type = EditorAssets::Type::Material;
        materials.push_back(material);
    }

    std::sort(materials.begin(), materials.end(),
        [](const EditorAssets::Asset& a, const EditorAssets::Asset& b) {
            return a.relativePath < b.relativePath;
        });
    return materials;
}

ImVec4 LogColor(EditorLog::Level level) {
    switch (level) {
    case EditorLog::Level::Info: return ImVec4(0.78f, 0.84f, 0.92f, 1.0f);
    case EditorLog::Level::Warning : return ImVec4(1.0f, 0.78f, 0.30f, 1.0f);
    case EditorLog::Level::Error: return ImVec4(1.0f, 0.34f, 0.32f, 1.0f);
    }
    return ImVec4(0.78f, 0.84f, 0.92f, 1.0f);
}

void DrawWorldSettings(EditorScene& scene, bool* open) {
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::WorldSettings), open)) {
        ImGui::End();
        return;
    }

    EditorScene::Environment environment = scene.GetEnvironment();
    const EditorScene::Environment defaults{};
    bool changed = false;

    if (ImGui::CollapsingHeader("Presets", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Day")) {
            environment.timeOfDay = 0.50f;
            environment.skyLightIntensity = 1.0f;
            environment.driveSunLight = true;
            environment.sunIntensity = 1.0f;
            environment.fog = false;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Night")) {
            environment.timeOfDay = 0.0f;
            environment.skyLightIntensity = 0.25f;
            environment.driveSunLight = true;
            environment.sunIntensity = 0.45f;
            environment.fog = false;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Sunset")) {
            environment.timeOfDay = 0.75f;
            environment.skyLightIntensity = 0.8f;
            environment.driveSunLight = true;
            environment.sunIntensity = 1.2f;
            environment.fog = true;
            environment.fogColor = glm::vec3(0.95f, 0.54f, 0.34f);
            environment.fogDensity = 0.006f;
            environment.fogHeight = -0.25f;
            environment.fogHeightFalloff = 0.08f;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Foggy")) {
            environment.skyLightIntensity = 0.7f;
            environment.fog = true;
            environment.fogColor = glm::vec3(0.55f, 0.62f, 0.68f);
            environment.fogDensity = 0.026f;
            environment.fogHeight = -0.10f;
            environment.fogHeightFalloff = 0.18f;
            changed = true;
        }
    }

    if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SmallButton("Reset Lighting")) {
            environment.timeOfDay = defaults.timeOfDay;
            environment.skyLightIntensity = defaults.skyLightIntensity;
            environment.driveSunLight = defaults.driveSunLight;
            environment.sunIntensity = defaults.sunIntensity;
            changed = true;
        }
        changed |= ImGui::SliderFloat("Time Of Day", &environment.timeOfDay, 0.0f, 1.0f);
        changed |= ImGui::DragFloat("Sky Light", &environment.skyLightIntensity, 0.02f, 0.0f, 8.0f);
        changed |= ImGui::Checkbox("Time Sun", &environment.driveSunLight);
        changed |= ImGui::DragFloat("Sun Intensity", &environment.sunIntensity, 0.02f, 0.0f, 8.0f);
    }

    if (ImGui::CollapsingHeader("Render Features", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SmallButton("Reset Render Features")) {
            environment.ibl = defaults.ibl;
            environment.ssao = defaults.ssao;
            environment.ssaoRadius = defaults.ssaoRadius;
            environment.ssaoBias = defaults.ssaoBias;
            environment.ssr = defaults.ssr;
            environment.ssrIntensity = defaults.ssrIntensity;
            changed = true;
        }
        changed |= ImGui::Checkbox("IBL", &environment.ibl);
        changed |= ImGui::Checkbox("SSAO", &environment.ssao);
        changed |= ImGui::DragFloat("SSAO Radius", &environment.ssaoRadius, 0.01f, 0.05f, 5.0f, "%.2f");
        changed |= ImGui::DragFloat("SSAO Bias", &environment.ssaoBias, 0.001f, 0.0f, 0.2f, "%.3f");
        changed |= ImGui::Checkbox("SSR", &environment.ssr);
        changed |= ImGui::DragFloat("SSR Intensity", &environment.ssrIntensity, 0.01f, 0.0f, 2.0f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SmallButton("Reset Shadows")) {
            environment.directionalShadows = defaults.directionalShadows;
            environment.pointShadows = defaults.pointShadows;
            environment.spotShadows = defaults.spotShadows;
            environment.shadowSoftness = defaults.shadowSoftness;
            changed = true;
        }
        changed |= ImGui::Checkbox("Sun Shadows", &environment.directionalShadows);
        changed |= ImGui::Checkbox("Point Shadows", &environment.pointShadows);
        changed |= ImGui::Checkbox("Spot Shadows", &environment.spotShadows);
        changed |= ImGui::DragFloat("Shadow Softness", &environment.shadowSoftness, 0.05f, 0.1f, 12.0f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Atmosphere", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SmallButton("Reset Atmosphere")) {
            environment.fog = defaults.fog;
            environment.fogColor = defaults.fogColor;
            environment.fogDensity = defaults.fogDensity;
            environment.fogHeight = defaults.fogHeight;
            environment.fogHeightFalloff = defaults.fogHeightFalloff;
            changed = true;
        }
        changed |= ImGui::Checkbox("Atmosphere Fog", &environment.fog);
        changed |= ImGui::ColorEdit3("Fog Color", &environment.fogColor.x);
        changed |= ImGui::DragFloat("Fog Density", &environment.fogDensity, 0.001f, 0.0f, 0.20f, "%.4f");
        changed |= ImGui::DragFloat("Fog Height", &environment.fogHeight, 0.02f, -20.0f, 20.0f);
        changed |= ImGui::DragFloat("Fog Falloff", &environment.fogHeightFalloff, 0.005f, 0.001f, 2.0f, "%.3f");
    }

    if (ImGui::CollapsingHeader("Editor Guides")) {
        if (ImGui::SmallButton("Reset Guides")) {
            environment.showLightGuides = defaults.showLightGuides;
            environment.selectedLightGuideOnly = defaults.selectedLightGuideOnly;
            changed = true;
        }
        changed |= ImGui::Checkbox("Light Guides", &environment.showLightGuides);
        changed |= ImGui::Checkbox("Selected Guide Only", &environment.selectedLightGuideOnly);
    }

    if (changed) {
        scene.SetEnvironment(environment);
    }

    ImGui::End();
}

void DrawHierarchy(EditorDockspace::Context& context, bool* open) {
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::Hierarchy), open)) {
        ImGui::End();
        return;
    }

    if (!context.scene) {
        ImGui::TextUnformatted("Scene unavailable.");
        ImGui::End();
        return;
    }

    const std::vector<EditorScene::Object>& objects = context.scene->Objects();
    ImGui::Text("%d objects", static_cast<int>(objects.size()));
    ImGui::Separator();

    for (int i = 0; i < static_cast<int>(objects.size()); ++i) {
        const EditorScene::Object& object = objects[static_cast<std::size_t>(i)];
        char label[192];
        std::snprintf(label, sizeof(label), "%s%s%s %s",
            object.visible ? "" : "[hidden] ",
            object.locked ? "[locked] " : "",
            object.light ? "[light]" : "",
            object.name.c_str());

        if (ImGui::Selectable(label, i == context.scene->SelectedIndex())) {
            context.scene->SelectIndex(i);
        }
    }

    ImGui::End();
}

void DrawInspector(EditorDockspace::Context& context, bool* open) {
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::Inspector), open)) {
        ImGui::End();
        return;
    }

    if (!context.scene) {
        ImGui::TextUnformatted("Scene unavailable.");
        ImGui::End();
        return;
    }

    const EditorScene::Object* selected = context.scene->SelectedObject();
    if (!selected) {
        ImGui::TextUnformatted("No object selected.");
        ImGui::End();
        return;
    }

    ImGui::Text("Name: %s", selected->name.c_str());
    ImGui::Text("Type: %s", ObjectTypeName(*selected));
    ImGui::Text("Model: %s", selected->modelAssetPath.empty() ? "-" : selected->modelAssetPath.c_str());
    ImGui::Text("Material: %s", selected->materialAssetPath.empty() ? "-" : selected->materialAssetPath.c_str());

    if (selected->light) {
    engine::ecs::Light light = selected->lightData;
        if (const engine::ecs::Light* component = context.scene->TryGetLight(selected->entity)) {
            light = *component;
        }

        int typeIndex = LightTypeIndex(light.type);
        const char* lightTypes[] = {"Directional", "Point", "Spot", "Area"};
        if (ImGui::Combo("Light Type", &typeIndex, lightTypes, 4)) {
            light.type = LightTypeFromIndex(typeIndex);
            context.scene->SetSelectedLight(light);
        }
        if (ImGui::ColorEdit3("Light Color", &light.color.x)) {
            context.scene->SetSelectedLight(light);
        }
        if (ImGui::DragFloat("Intensity", &light.intensity, 0.1f, 0.0f, 10000.0f)) {
            context.scene->SetSelectedLight(light);
        }
        if (ImGui::DragFloat3("Direction", &light.direction.x, 0.02f)) {
            context.scene->SetSelectedLight(light);
        }
        if (ImGui::DragFloat("Inner Angle", &light.innerAngle, 0.25f, 0.0f, 179.0f)) {
            context.scene->SetSelectedLight(light);
        }
        if (ImGui::DragFloat("Outer Angle", &light.outerAngle, 0.25f, 0.0f, 179.0f)) {
            context.scene->SetSelectedLight(light);
        }
        if (ImGui::DragFloat("Range", &light.range, 0.25f, 0.0f, 10000.0f)) {
            context.scene->SetSelectedLight(light);
        }
        if (ImGui::DragFloat("Source Radius", &light.sourceRadius, 0.05f, 0.0f, 1000.0f)) {
            context.scene->SetSelectedLight(light);
        }
    }

    if (context.assets) {
        const std::vector<EditorAssets::Asset> materials = FindMaterialAssets(*context.assets);
        const char* preview = selected->materialAssetPath.empty() ? "None" : selected->materialAssetPath.c_str();
        if (ImGui::BeginCombo("Apply Material", preview)) {
            if (ImGui::Selectable("None", selected->materialAssetPath.empty())) {
                if (context.scene->SetSelectedMaterialAsset(std::string())) {
                    if (context.log) {
                        context.log->Info("Cleared selected object material");
                    }
                } else if (context.log) {
                    context.log->Warning("Material clear failed: selected object is locked");
                }
            }
            for (const EditorAssets::Asset& material : materials) {
                const std::string fullPath = (std::filesystem::path(context.assets->RootPath()) / material.relativePath).string();
                const bool current = selected->materialAssetPath == fullPath;
                if (ImGui::Selectable(material.relativePath.c_str(), current)) {
                    if (context.scene->SetSelectedMaterialAsset(fullPath)) {
                        if (context.log) {
                            context.log->Info("Applied material: " + material.relativePath);
                        }
                    } else if (context.log) {
                        context.log->Warning("Material apply failed: selected object is locked");
                    }
                }
                if (current) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            if (materials.empty()) {
                ImGui::TextUnformatted("No .3dgmat files found.");
            }
            ImGui::EndCombo();
        }
    }

    glm::vec3 linearVelocity = selected->linearVelocity;
    if (ImGui::DragFloat3("Linear velocity", &linearVelocity.x, 0.05f)) {
        context.scene->SetSelectedLinearVelocity(linearVelocity);
    }

    glm::vec3 angularAxis = selected->angularVelocityAxis;
    float angularSpeed = selected->angularVelocityRadians;
    if (ImGui::DragFloat3("Angular axis", &angularAxis.x, 0.05f)) {
        context.scene->SetSelectedAngularVelocity(angularAxis, angularSpeed);
    }
    if (ImGui::DragFloat("Angular speed", &angularSpeed, 0.05f)) {
        context.scene->SetSelectedAngularVelocity(angularAxis, angularSpeed);
    }
    if (ImGui::Button("Spin Y")) {
        context.scene->SetSelectedAngularVelocity(glm::vec3(0.0f, 1.0f, 0.0f), 1.57f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear motion")) {
        context.scene->SetSelectedLinearVelocity(glm::vec3(0.0f));
        context.scene->SetSelectedAngularVelocity(glm::vec3(0.0f, 1.0f, 0.0f), 0.0f);
    }

    bool visible = selected->visible;
    if (ImGui::Checkbox("Visible", &visible)) {
        context.scene->ToggleSelectVisible();
    }

    bool locked = selected->locked;
    if (ImGui::Checkbox("Locked", &locked)) {
        context.scene->ToggleSelectedLocked();
    }

    ImGui::Separator();

    const engine::ecs::Transform* transform = context.scene->TryGetTransform(selected->entity);
    if (transform) {
        ImGui::Text("Position: %.2f, %.2f, %.2f",
            transform->position.x, transform->position.y, transform->position.z);
        ImGui::Text("Scale: %.2f, %.2f, %.2f",
            transform->scale.x, transform->scale.y, transform->scale.z);
        ImGui::Text("Rotation: %.2f, %.2f, %.2f, %.2f",
            transform->rotation.w, transform->rotation.x,
            transform->rotation.y, transform->rotation.z);
    }

    const engine::ecs::MeshRenderer* renderer = context.scene->TryGetMeshRenderer(selected->entity);
    if (renderer) {
        ImGui::Text("Color: %.2f, %.2f, %.2f",
            renderer->color.r, renderer->color.g, renderer->color.b);
    }

    ImGui::End();
}

void DrawAssets(EditorDockspace::Context& context, bool* open) {
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::Assets), open)) {
        ImGui::End();
        return;
    }

    if (!context.assets) {
        ImGui::TextUnformatted("Assets unavailable.");
        ImGui::End();
        return;
    }

    ImGui::Text("Root: %s", context.assets->RootPath().c_str());
    ImGui::Text("Folder: /%s", context.assets->CurrentFolder().c_str());
    ImGui::Text("%d files", static_cast<int>(context.assets->Assets().size()));
    if (ImGui::Button("Up")) {
        std::string error;
        if (!context.assets->GoUp(&error) && context.log) {
            context.log->Error(error);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        std::string error;
        if (!context.assets->Refresh(context.assets->RootPath(), &error) && context.log) {
            context.log->Error(error);
        }
    }
    if (ImGui::Button("Copy")) {
        std::string error;
        if (context.assets->CopySelected(&error)) {
            const EditorAssets::Asset* selectedAsset = context.assets->SelectedAsset();
            if (selectedAsset && selectedAsset->type == EditorAssets::Type::Texture) {
                const std::string texturePath = context.assets->SelectedAssetFullPath();
                ImGui::SetClipboardText(texturePath.c_str());
            }
            if (context.log) {
                context.log->Info("Copied Content entry: " + context.assets->CopiedDisplayName());
                if (selectedAsset && selectedAsset->type == EditorAssets::Type::Texture) {
                    context.log->Info("Texture path ready for Material Maker paste");
                }
            }
        } else if (context.log) {
            context.log->Warning(error);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Paste")) {
        std::string error;
        if (context.assets->PasteCopied(&error)) {
            if (context.log) {
                context.log->Info("Pasted Content entry");
            }
        } else if (context.log) {
            context.log->Warning(error);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        std::string error;
        if (context.assets->DeleteSelectedEntry(&error)) {
            if (context.log) {
                context.log->Info("Deleted Content entry");
            }
        } else if (context.log) {
            context.log->Warning(error);
        }
    }
    if (context.assets->HasCopiedEntry()) {
        ImGui::Text("Copied: %s", context.assets->CopiedDisplayName().c_str());
    }

    static char folderName[128] = "NewFolder";
    ImGui::InputText("Folder name", folderName, sizeof(folderName));
    ImGui::SameLine();
    if (ImGui::Button("Create folder")) {
        std::string error;
        if (context.assets->CreateFolder(folderName, &error)) {
            if (context.log) {
                context.log->Info(std::string("Created Content folder: ") + folderName);
            }
            folderName[0] = '\0';
        } else if (context.log) {
            context.log->Error(error);
        }
    }

    static char importPath[512] = "";
    ImGui::InputText("Import file", importPath, sizeof(importPath));
    ImGui::SameLine();
    if (ImGui::Button("Import")) {
        std::string error;
        if (context.assets->ImportAsset(importPath, &error)) {
            if (context.log) {
                context.log->Info(std::string("Imported asset: ") + importPath);
            }
            importPath[0] = '\0';
        } else if (context.log) {
            context.log->Error(error);
        }
    }
    ImGui::Separator();

    const std::vector<EditorAssets::Folder>& folders = context.assets->Folders();
    for (int i = 0; i < static_cast<int>(folders.size()); ++i) {
        const EditorAssets::Folder& folder = folders[static_cast<std::size_t>(i)];
        char label[256];
        std::snprintf(label, sizeof(label), "[Folder] %s", folder.displayName.c_str());
        const bool selected = context.assets->SelectedType() == EditorAssets::SelectionType::Folder
            && context.assets->SelectedFolderIndex() == i;
        if (ImGui::Selectable(label, selected)) {
            context.assets->SelectFolderIndex(i);
        }
        if (selected && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            std::string error;
            if (!context.assets->EnterFolder(i, &error) && context.log) {
                context.log->Error(error);
            }
        }
    }

    const std::vector<EditorAssets::Asset>& assets = context.assets->Assets();
    for (int i =0; i < static_cast<int>(assets.size()); ++i) {
        const EditorAssets::Asset& asset = assets[static_cast<std::size_t>(i)];
        char label[256];
        std::snprintf(label, sizeof(label), "[%s] %s",
            EditorAssets::TypeName(asset.type), asset.relativePath.c_str());
        
        const bool selected = context.assets->SelectedType() == EditorAssets::SelectionType::Asset
            && i == context.assets->SelectedIndex();
        if (ImGui::Selectable(label, selected)) {
            context.assets->SelectIndex(i);
        }

        if (context.dragDrop && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            context.assets->SelectIndex(i);
            const ImVec2 mouse = ImGui::GetMousePos();
            const std::string fullPath = (std::filesystem::path(context.assets->RootPath()) / asset.relativePath).string();
            if (!context.dragDrop->IsMouseDriven()
                || context.dragDrop->CurrentPayload().path != fullPath) {
                context.dragDrop->BeginAssetDragAt(fullPath, EditorAssets::TypeName(asset.type), mouse.x, mouse.y);
            } else {
                context.dragDrop->UpdateCursor(mouse.x, mouse.y);
            }

            ImGui::SetDragDropPayload("3DGEDITOR_ASSET", fullPath.c_str(), fullPath.size() + 1);
            ImGui::Text("%s", label);
            ImGui::EndDragDropSource();
        }
    }

    ImGui::End();
}

void DrawConsole(EditorDockspace::Context& context, bool* open) {
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::Console), open)) {
        ImGui::End();
        return;
    }

    if (!context.log) {
        ImGui::TextUnformatted("Console unavailable.");
        ImGui::End();
        return;
    }

    ImGui::Text("Latest: %s", context.log->LatestMessage().c_str());
    ImGui::Separator();

    for (const EditorLog::Entry& entry : context.log->Entries()) {
        ImGui::TextColored(LogColor(entry.level), "[%s] %s",
            EditorLog::LevelName(entry.level), entry.message.c_str());
    }

    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::End();
}

void DrawGizmoToolbar(EditorDockspace::Context& context) {
    if (!context.gizmo) {
        return;
    }

    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::Begin("Gizmo", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoSavedSettings);

    const EditorGizmo::Mode mode = context.gizmo->CurrentMode();
    if (ImGui::Selectable("Move", mode == EditorGizmo::Mode::Translate, 0, ImVec2(72.0f, 0.0f))) {
        context.gizmo->SetMode(EditorGizmo::Mode::Translate);
    }
    ImGui::SameLine();
    if (ImGui::Selectable("Rotate", mode == EditorGizmo::Mode::Rotate, 0, ImVec2(72.0f, 0.0f))) {
        context.gizmo->SetMode(EditorGizmo::Mode::Rotate);
    }
    ImGui::SameLine();
    if (ImGui::Selectable("Scale", mode == EditorGizmo::Mode::Scale, 0, ImVec2(72.0f, 0.0f))) {
        context.gizmo->SetMode(EditorGizmo::Mode::Scale);
    }

    ImGui::Separator();
    const EditorGizmo::Axis axis = context.gizmo->CurrentAxis();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.16f, 0.18f, 1.0f));
    if (ImGui::Button(axis == EditorGizmo::Axis::X ? "X -> *" : "X ->", ImVec2(68.0f, 0.0f))) {
        context.gizmo->SetAxis(EditorGizmo::Axis::X);
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.55f, 0.22f, 1.0f));
    if (ImGui::Button(axis == EditorGizmo::Axis::Y ? "Y ^ *" : "Y ^", ImVec2(68.0f, 0.0f))) {
        context.gizmo->SetAxis(EditorGizmo::Axis::Y);
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.34f, 0.76f, 1.0f));
    if (ImGui::Button(axis == EditorGizmo::Axis::Z ? "Z / *" : "Z /", ImVec2(68.0f, 0.0f))) {
        context.gizmo->SetAxis(EditorGizmo::Axis::Z);
    }
    ImGui::PopStyleColor();

    ImGui::End();
}

} // namespace

bool EditorDockspace::Draw(Context &context)
{
    if (!context.panels) {
        return false;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar
        | ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("3DGEditorDockspace", nullptr, flags);
    ImGui::PopStyleVar(2);

    const ImGuiID dockspaceId = ImGui::GetID("3DGEditorDockspaceRoot");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    if (context.dragDrop && ImGui::BeginDragDropTarget()) {
        if (ImGui::AcceptDragDropPayload("3DGEDITOR_ASSET")) {
            context.viewportDropRequested = true;
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("Project")) {
            if (context.project) {
                ImGui::Text("Name: %s", context.project->ProjectName().c_str());
                ImGui::Text("Scene: %s", context.project->ScenePath().c_str());
            }
            ImGui::Text("Mode: %s", context.modeName);
            ImGui::Text("FPS: %.0f", context.fps);
            ImGui::Text("Scene dirty: %s", context.sceneDirty ? "yes" : "no");
            if (context.gizmo) {
                ImGui::Text("Gizmo: %s %s", context.gizmo->ModeName(), context.gizmo->AxisName());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save")) {
                context.saveSceneRequested = true;
            }
            if (context.scenePathBuffer && context.scenePathBufferSize > 0) {
                ImGui::SetNextItemWidth(280.0f);
                ImGui::InputText("Scene Path", context.scenePathBuffer, context.scenePathBufferSize);
            }
            if (ImGui::MenuItem("Save As")) {
                context.saveAsSceneRequested = true;
            }
            if (ImGui::MenuItem("Load")) {
                context.loadSceneRequested = true;
            }
            if (ImGui::MenuItem("Export Runtime")) {
                context.exportRuntimeRequested = true;
            }
            if (ImGui::MenuItem("Validate Runtime")) {
                context.validateRuntimeRequested = true;
            }
            if (context.project && !context.project->RecentScenes().empty()) {
                ImGui::Separator();
                if (ImGui::BeginMenu("Recent Scenes")) {
                    const std::vector<std::string>& recentScenes = context.project->RecentScenes();
                    for (int i = 0; i < static_cast<int>(recentScenes.size()); ++i) {
                        if (ImGui::MenuItem(recentScenes[static_cast<std::size_t>(i)].c_str())) {
                            context.recentSceneRequested = i;
                        }
                    }
                    ImGui::EndMenu();
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Add Directional Light")) {
                context.addDirectionalLightRequested = true;
            }
            if (ImGui::MenuItem("Add Point Light")) {
                context.addPointLightRequested = true;
            }
            if (ImGui::MenuItem("Add Spot Light")) {
                context.addSpotLightRequested = true;
            }
            if (ImGui::MenuItem("Add Area Light")) {
                context.addAreaLightRequested = true;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Panels")) {
            for (int i = 0; i < static_cast<int>(EditorPanels::Panel::Count); ++i) {
                const EditorPanels::Panel panel = static_cast<EditorPanels::Panel>(i);
                const bool open = context.panels->IsOpen(panel);
                if (ImGui::MenuItem(EditorPanels::Name(panel), nullptr, open)) {
                    context.panels->SetOpen(panel, !open);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    for (int i = 0; i < static_cast<int>(EditorPanels::Panel::Count); ++i) {
        const EditorPanels::Panel panel = static_cast<EditorPanels::Panel>(i);
        bool open = context.panels->IsOpen(panel);
        if (!open) {
            continue;
        }

        switch (panel) {
        case EditorPanels::Panel::Hierarchy:
            DrawHierarchy(context, &open);
            break;
        case EditorPanels::Panel::Inspector:
            DrawInspector(context, &open);
            break;
        case EditorPanels::Panel::WorldSettings:
            if (context.scene) {
                DrawWorldSettings(*context.scene, &open);
            }
            break;
        case EditorPanels::Panel::Assets:
            DrawAssets(context, &open);
            break;
        case EditorPanels::Panel::Console:
            DrawConsole(context, &open);
            break;
        case EditorPanels::Panel::MaterialMaker:
            break;
        case EditorPanels::Panel::Count:
            break;
        }

        context.panels->SetOpen(panel, open);
    }

    DrawGizmoToolbar(context);

    ImGui::End();
    return true;
}

bool EditorDockspace::IsCompiledWithImGui() const
{
    return false;
}
