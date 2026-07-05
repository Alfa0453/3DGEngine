#include "EditorDockspace.h"

#include <engine/ecs/Components.h>
#include <engine/physics/PhysicsComponents.h>

#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

std::array<char, 128> g_objectNameBuffer{};
engine::ecs::Entity g_objectNameEntity = engine::ecs::kNull;
ImGuiTextFilter g_hierarchyFilter;

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

int ColliderShapeIndex(engine::ecs::ColliderShape shape) {
    switch (shape) {
    case engine::ecs::ColliderShape::Sphere: return 0;
    case engine::ecs::ColliderShape::Box: return 1;
    case engine::ecs::ColliderShape::Plane: return 2;
    }
    return 0;
}

engine::ecs::ColliderShape ColliderShapeFromIndex(int index) {
    switch (index) {
    case 1: return engine::ecs::ColliderShape::Box;
    case 2: return engine::ecs::ColliderShape::Plane;
    default: return engine::ecs::ColliderShape::Sphere;
    }
}

const char* ColliderShapeName(engine::ecs::ColliderShape shape) {
    switch (shape) {
    case engine::ecs::ColliderShape::Sphere: return "Sphere";
    case engine::ecs::ColliderShape::Box: return "Box";
    case engine::ecs::ColliderShape::Plane: return "Plane";
    }
    return "Unknown";
}

struct ScenePhysicsStatus {
    std::size_t objects = 0;
    std::size_t rigidBodies = 0;
    std::size_t dynamicBodies = 0;
    std::size_t staticBodies = 0;
    std::size_t colliders = 0;
    std::size_t staticColliders = 0;
    std::size_t triggers = 0;
    std::size_t spheres = 0;
    std::size_t boxes = 0;
    std::size_t planes = 0;
    std::size_t dynamicBodiesWithoutCollider = 0;
    std::size_t invalidColliders = 0;
};

bool ColliderIsInvalid(const engine::ecs::Collider& collider) {
    switch (collider.shape) {
    case engine::ecs::ColliderShape::Sphere:
        return collider.radius <= 0.0f;
    case engine::ecs::ColliderShape::Box:
        return collider.halfExtents.x <= 0.0f
            || collider.halfExtents.y <= 0.0f
            || collider.halfExtents.z <= 0.0f;
    case engine::ecs::ColliderShape::Plane:
        return collider.planeNormal.x == 0.0f
            && collider.planeNormal.y == 0.0f
            && collider.planeNormal.z == 0.0f;
    }

    return true;
}

ScenePhysicsStatus CollectScenePhysicsStatus(const EditorScene& scene) {
    ScenePhysicsStatus status;
    status.objects = scene.Objects().size();

    for (const EditorScene::Object& object : scene.Objects()) {
        if (object.rigidBodyEnabled) {
            ++status.rigidBodies;
            if (object.rigidBody.invMass > 0.0f) {
                ++status.dynamicBodies;
                if (!object.colliderEnabled) {
                    ++status.dynamicBodiesWithoutCollider;
                }
            } else {
                ++status.staticBodies;
            }
        }

        if (!object.colliderEnabled) {
            continue;
        }

        ++status.colliders;
        if (object.collider.isTrigger) {
            ++status.triggers;
        }
        if (!object.rigidBodyEnabled || object.rigidBody.invMass <= 0.0f) {
            ++status.staticColliders;
        }
        if (ColliderIsInvalid(object.collider)) {
            ++status.invalidColliders;
        }

        switch (object.collider.shape) {
        case engine::ecs::ColliderShape::Sphere:
            ++status.spheres;
            break;
        case engine::ecs::ColliderShape::Box:
            ++status.boxes;
            break;
        case engine::ecs::ColliderShape::Plane:
            ++status.planes;
            break;
        }
    }

    return status;
}

void DrawStatusRow(const char* label, std::size_t value) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
    ImGui::Text("%d", static_cast<int>(value));
}

engine::ecs::Collider DefaultColliderForObject(const EditorScene::Object& object,
                                               const engine::ecs::Transform* transform) {
    if (object.primitive == EditorScene::Primitive::Plane && object.modelAssetPath.empty()) {
        const float planeOffset = transform ? transform->position.y : 0.0f;
        return engine::ecs::Collider::MakePlane(glm::vec3(0.0f, 1.0f, 0.0f), planeOffset);
    }

    if (transform) {
        return engine::ecs::Collider::MakeBox(glm::vec3(
            std::max(transform->scale.x * 0.5f, 0.001f),
            std::max(transform->scale.y * 0.5f, 0.001f),
            std::max(transform->scale.z * 0.5f, 0.001f)));
    }

    return engine::ecs::Collider::MakeBox(glm::vec3(0.5f));
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

    if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SmallButton("Reset Physics")) {
            environment.physicsGravity = defaults.physicsGravity;
            environment.physicsSolverIterations = defaults.physicsSolverIterations;
            environment.physicsBroadPhase = defaults.physicsBroadPhase;
            environment.physicsCellSize = defaults.physicsCellSize;
            environment.physicsRestitutionThreshold = defaults.physicsRestitutionThreshold;
            environment.physicsAllowSleeping = defaults.physicsAllowSleeping;
            environment.physicsSleepLinearVelocity = defaults.physicsSleepLinearVelocity;
            environment.physicsTimeToSleep = defaults.physicsTimeToSleep;
            changed = true;
        }
        changed |= ImGui::DragFloat3("Gravity", &environment.physicsGravity.x, 0.05f);
        changed |= ImGui::DragInt("Solver Iterations", &environment.physicsSolverIterations, 1.0f, 1, 32);
        changed |= ImGui::Checkbox("Broad Phase", &environment.physicsBroadPhase);
        changed |= ImGui::DragFloat("Cell Size", &environment.physicsCellSize, 0.05f, 0.1f, 100.0f, "%.2f");
        changed |= ImGui::DragFloat("Bounce Threshold", &environment.physicsRestitutionThreshold, 0.02f, 0.0f, 10.0f, "%.2f");
        changed |= ImGui::Checkbox("Allow Sleeping", &environment.physicsAllowSleeping);
        changed |= ImGui::DragFloat("Sleep Velocity", &environment.physicsSleepLinearVelocity, 0.005f, 0.0f, 10.0f, "%.3f");
        changed |= ImGui::DragFloat("Sleep Time", &environment.physicsTimeToSleep, 0.02f, 0.0f, 10.0f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Editor Guides")) {
        if (ImGui::SmallButton("Reset Guides")) {
            environment.showLightGuides = defaults.showLightGuides;
            environment.selectedLightGuideOnly = defaults.selectedLightGuideOnly;
            environment.showPhysicsGuides = defaults.showPhysicsGuides;
            environment.selectedPhysicsGuideOnly = defaults.selectedPhysicsGuideOnly;
            changed = true;
        }
        changed |= ImGui::Checkbox("Light Guides", &environment.showLightGuides);
        changed |= ImGui::Checkbox("Selected Guide Only", &environment.selectedLightGuideOnly);
        changed |= ImGui::Checkbox("Physics Guides", &environment.showPhysicsGuides);
        changed |= ImGui::Checkbox("Selected Physics Only", &environment.selectedPhysicsGuideOnly);
    }

    if (changed) {
        scene.SetEnvironment(environment);
    }

    ImGui::End();
}

void DrawPhysicsStatus(EditorDockspace::Context& context, bool* open) {
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::PhysicsStatus), open)) {
        ImGui::End();
        return;
    }

    if (!context.scene) {
        ImGui::TextUnformatted("Scene unavailable.");
        ImGui::End();
        return;
    }

    const EditorScene::Environment& environment = context.scene->GetEnvironment();
    const ScenePhysicsStatus status = CollectScenePhysicsStatus(*context.scene);

    if (ImGui::Button("Validate Runtime")) {
        context.validateRuntimeRequested = true;
    }
    ImGui::SameLine();
    if (context.playMode) {
        ImGui::TextUnformatted("Mode: Play");
    } else {
        ImGui::TextUnformatted("Mode: Edit");
    }

    if (context.playMode) {
        if (ImGui::Button(context.physicsPaused ? "Resume Physics" : "Pause Physics")) {
            context.physicsPauseToggleRequested = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Step Physics")) {
            context.physicsStepRequested = true;
        }
    } else {
        ImGui::TextUnformatted("Physics playback controls are available in Play mode.");
    }

    ImGui::Text("Fixed Step: %.4f s (%.1f Hz)",
        context.physicsFixedTimestep,
        context.physicsFixedTimestep > 0.0f ? 1.0f / context.physicsFixedTimestep : 0.0f);
    ImGui::Text("Accumulator: %.4f s", context.physicsAccumulator);
    ImGui::Text("Steps Last Frame: %d", context.physicsStepsLastFrame);
    ImGui::Text("Events: %d enter, %d stay, %d exit",
        context.physicsEventEnterCount,
        context.physicsEventStayCount,
        context.physicsEventExitCount);

    if (ImGui::CollapsingHeader("World Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("PhysicsWorldStatus", 2, ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Gravity");
            ImGui::TableNextColumn();
            ImGui::Text("%.2f, %.2f, %.2f",
                environment.physicsGravity.x,
                environment.physicsGravity.y,
                environment.physicsGravity.z);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Solver Iterations");
            ImGui::TableNextColumn();
            ImGui::Text("%d", environment.physicsSolverIterations);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Broad Phase");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(environment.physicsBroadPhase ? "On" : "Off");

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Cell Size");
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", environment.physicsCellSize);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Sleeping");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(environment.physicsAllowSleeping ? "On" : "Off");
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Scene Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("PhysicsSceneStatus", 2, ImGuiTableFlags_BordersInnerV)) {
            DrawStatusRow("Objects", status.objects);
            DrawStatusRow("Rigid Bodies", status.rigidBodies);
            DrawStatusRow("Dynamic Bodies", status.dynamicBodies);
            DrawStatusRow("Static Bodies", status.staticBodies);
            DrawStatusRow("Colliders", status.colliders);
            DrawStatusRow("Static Colliders", status.staticColliders);
            DrawStatusRow("Triggers", status.triggers);
            DrawStatusRow("Sphere Colliders", status.spheres);
            DrawStatusRow("Box Colliders", status.boxes);
            DrawStatusRow("Plane Colliders", status.planes);
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Selected Object", ImGuiTreeNodeFlags_DefaultOpen)) {
        const EditorScene::Object* selected = context.scene->SelectedObject();
        if (!selected) {
            ImGui::TextUnformatted("No object selected.");
        } else {
            ImGui::Text("Name: %s", selected->name.c_str());
            ImGui::Text("Type: %s", ObjectTypeName(*selected));
            ImGui::Text("Rigid Body: %s", selected->rigidBodyEnabled ? "Enabled" : "Disabled");
            if (selected->rigidBodyEnabled) {
                const bool dynamic = selected->rigidBody.invMass > 0.0f;
                ImGui::Text("Mode: %s", dynamic ? "Dynamic" : "Static");
                if (dynamic) {
                    ImGui::Text("Mass: %.3f", 1.0f / selected->rigidBody.invMass);
                }
                ImGui::Text("Velocity: %.3f, %.3f, %.3f",
                    selected->rigidBody.velocity.x,
                    selected->rigidBody.velocity.y,
                    selected->rigidBody.velocity.z);
                ImGui::Text("Gravity: %s", selected->rigidBody.useGravity ? "On" : "Off");
                ImGui::Text("CCD: %s", selected->rigidBody.ccd ? "On" : "Off");
                ImGui::Text("Sleep: %s", selected->rigidBody.allowSleep ? "Allowed" : "Blocked");
            }

            ImGui::Separator();
            ImGui::Text("Collider: %s", selected->colliderEnabled ? "Enabled" : "Disabled");
            if (selected->colliderEnabled) {
                ImGui::Text("Shape: %s", ColliderShapeName(selected->collider.shape));
                if (selected->collider.shape == engine::ecs::ColliderShape::Sphere) {
                    ImGui::Text("Radius: %.3f", selected->collider.radius);
                } else if (selected->collider.shape == engine::ecs::ColliderShape::Box) {
                    ImGui::Text("Half Extents: %.3f, %.3f, %.3f",
                        selected->collider.halfExtents.x,
                        selected->collider.halfExtents.y,
                        selected->collider.halfExtents.z);
                } else {
                    ImGui::Text("Plane: %.3f, %.3f, %.3f / %.3f",
                        selected->collider.planeNormal.x,
                        selected->collider.planeNormal.y,
                        selected->collider.planeNormal.z,
                        selected->collider.planeOffset);
                }
                ImGui::Text("Trigger: %s", selected->collider.isTrigger ? "Yes" : "No");
                ImGui::Text("Restitution: %.3f", selected->collider.restitution);
                ImGui::Text("Friction: %.3f", selected->collider.friction);
            }
        }
    }

    if (ImGui::CollapsingHeader("Warnings", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool hasWarning = false;
        if (status.dynamicBodiesWithoutCollider > 0) {
            hasWarning = true;
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.30f, 1.0f),
                "%d dynamic body/bodies have no collider",
                static_cast<int>(status.dynamicBodiesWithoutCollider));
        }
        if (status.invalidColliders > 0) {
            hasWarning = true;
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.30f, 1.0f),
                "%d collider(s) have invalid dimensions",
                static_cast<int>(status.invalidColliders));
        }
        if (status.dynamicBodies > 0 && status.staticColliders == 0) {
            hasWarning = true;
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.30f, 1.0f),
                "Dynamic bodies exist but no static colliders are present");
        }
        if (!hasWarning) {
            ImGui::TextUnformatted("No physics warnings.");
        }
        
    }

    if (ImGui::CollapsingHeader("Recent Events", ImGuiTreeNodeFlags_DefaultOpen)) {
        static bool selectedOnly = false;
        static bool triggersOnly = false;
        static bool enterExitOnly = false;

        ImGui::Checkbox("Selected Only##PhysicsEvents", &selectedOnly);
        ImGui::SameLine();
        ImGui::Checkbox("Triggers Only##PhysicsEvents", &triggersOnly);
        ImGui::SameLine();
        ImGui::Checkbox("Enter/Exit Only##PhysicsEvents", &enterExitOnly);

        std::string selectedName;
        if (context.scene) {
            if (const EditorScene::Object* selected = context.scene->SelectedObject()) {
                selectedName = selected->name;
            }
        }

        if (!context.playMode) {
            ImGui::TextUnformatted("Enter Play mode to capture physics events.");
        } else if (!context.physicsEventRows || context.physicsEventRows->empty()) {
            ImGui::TextUnformatted("No collision or trigger events captured yet.");
        } else {
            int visibleRows = 0;
            ImGui::BeginChild("PhysicsEventList", ImVec2(0.0f, 150.0f), true);
            for (const EditorDockspace::PhysicsEventRow& row : *context.physicsEventRows) {
                if (selectedOnly && (selectedName.empty()
                    || (row.objectA != selectedName && row.objectB != selectedName))) {
                    continue;
                }
                if (triggersOnly && !row.trigger) {
                    continue;
                }
                if (enterExitOnly && row.phase == 1) {
                    continue;
                }

                ImGui::TextUnformatted(row.text.c_str());
                ++visibleRows;
            }
            if (visibleRows == 0) {
                ImGui::TextUnformatted("No events match the active filters.");
            }
            ImGui::EndChild();
        }
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
    g_hierarchyFilter.Draw("Filter");

    int visibleObjects = 0;
    for (const EditorScene::Object& object : objects) {
        char label[192];
        std::snprintf(label, sizeof(label), "%s%s%s %s",
            object.visible ? "" : "[hidden] ",
            object.locked ? "[locked] " : "",
            object.light ? "[light] " : "",
            object.name.c_str());

        if (g_hierarchyFilter.PassFilter(label)) {
            ++visibleObjects;
        }
    }

    if (g_hierarchyFilter.IsActive()) {
        ImGui::Text("%d of %d objects", visibleObjects, static_cast<int>(objects.size()));
    } else {
        ImGui::Text("%d objects", static_cast<int>(objects.size()));
    }
    ImGui::Separator();

    for (int i = 0; i < static_cast<int>(objects.size()); ++i) {
        const EditorScene::Object& object = objects[static_cast<std::size_t>(i)];
        char label[192];
        std::snprintf(label, sizeof(label), "%s%s%s %s",
            object.visible ? "" : "[hidden] ",
            object.locked ? "[locked] " : "",
            object.light ? "[light]" : "",
            object.name.c_str());

        if (!g_hierarchyFilter.PassFilter(label)) {
            continue;
        }

        if (ImGui::Selectable(label, i == context.scene->SelectedIndex())) {
            context.scene->SelectIndex(i);
        }
        if (ImGui::BeginPopupContextItem()) {
            context.scene->SelectIndex(i);
            if (ImGui::MenuItem("Duplicate", nullptr, false, !object.locked)) {
                context.duplicateSelectedRequested = true;
            }
            if (ImGui::MenuItem(object.visible ? "Hide" : "Show")) {
                context.scene->ToggleSelectVisible();
            }
            if (ImGui::MenuItem(object.locked ? "Unlock" : "lock")) {
                context.scene->ToggleSelectedLocked();
            }
            if (ImGui::MenuItem("Delete", nullptr, false, !object.locked)) {
                context.deleteSelectedRequested = true;
            }
            if (ImGui::MenuItem("Frame Selected")) {
                context.frameSelectedRequested = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Deselect")) {
                context.scene->Deselect();
            }
            ImGui::EndPopup();
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

    if (ImGui::CollapsingHeader("Object", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (g_objectNameEntity != selected->entity) {
            std::snprintf(g_objectNameBuffer.data(), g_objectNameBuffer.size(), "%s", selected->name.c_str());
            g_objectNameEntity = selected->entity;
        }

        const bool submitted = ImGui::InputText("Name",
            g_objectNameBuffer.data(),
            g_objectNameBuffer.size(),
            ImGuiInputTextFlags_EnterReturnsTrue);
        const bool nameEditFinished = ImGui::IsItemDeactivatedAfterEdit();
        if (submitted || nameEditFinished) {
            if (context.scene->SetSelectedName(g_objectNameBuffer.data())) {
                const EditorScene::Object* renamed = context.scene->SelectedObject();
                std::snprintf(g_objectNameBuffer.data(), g_objectNameBuffer.size(), "%s",
                    renamed ? renamed->name.c_str() : "");
            } else {
                std::snprintf(g_objectNameBuffer.data(), g_objectNameBuffer.size(), "%s", selected->name.c_str());
            }
        }

        ImGui::Text("Type: %s", ObjectTypeName(*selected));
        if (ImGui::Button("Frame Selected")) {
            context.frameSelectedRequested = true;
        }

        bool visible = selected->visible;
        if(ImGui::Checkbox("Visible", &visible)) {
            context.scene->ToggleSelectVisible();
        }

        bool locked = selected->locked;
        if (ImGui::Checkbox("Locked", &locked)) {
            context.scene->ToggleSelectedLocked();
        }
    }

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        const engine::ecs::Transform* transform = context.scene->TryGetTransform(selected->entity);
        if (transform) {
            engine::ecs::Transform edited = *transform;
            glm::vec3 rotationDegrees = glm::degrees(glm::eulerAngles(transform->rotation));
            bool transformChanged = false;
            bool transformEditEnded = false;

            transformChanged |= ImGui::DragFloat3("Position", &edited.position.x, 0.05f);
            if (ImGui::IsItemActivated()) {
                context.scene->BeginTransformEdit();
            }
            transformEditEnded |= ImGui::IsItemDeactivatedAfterEdit();

            transformChanged |= ImGui::DragFloat3("Scale", &edited.scale.x, 0.02f, 0.001f, 1000.0f);
            if (ImGui::IsItemActivated()) {
                context.scene->BeginTransformEdit();
            }
            transformEditEnded |= ImGui::IsItemDeactivatedAfterEdit();

            if (ImGui::DragFloat3("Rotation", &rotationDegrees.x, 0.5f)) {
                edited.rotation = glm::quat(glm::radians(rotationDegrees));
                transformChanged = true;
            }
            if (ImGui::IsItemActivated()) {
                context.scene->BeginTransformEdit();
            }
            transformEditEnded |= ImGui::IsItemDeactivatedAfterEdit();

            if (transformChanged) {
                context.scene->SetSelectedTransform(edited);
            }
            if (transformEditEnded) {
                context.scene->EndTransformEdit();
            }

            if (ImGui::Button("Reset Transform")) {
                context.scene->ResetSelectedTransform();
            }
        } else {
            ImGui::TextUnformatted("No transform component.");
        }
    }

    if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Model: %s", selected->modelAssetPath.empty() ? "-" : selected->modelAssetPath.c_str());
        ImGui::Text("Material: %s", selected->materialAssetPath.empty() ? "-" : selected->materialAssetPath.c_str());

        const engine::ecs::MeshRenderer* renderer = context.scene->TryGetMeshRenderer(selected->entity);
        if (renderer) {
            glm::vec3 color = renderer->color;
            if (ImGui::ColorEdit3("Color", &color.x)) {
                context.scene->SetSelectedColor(color);
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
    }

    if (selected->light) {
        if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
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
    }

        if (ImGui::CollapsingHeader("Runtime Components", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool linearVelocityEnabled = selected->linearVelocityEnabled;
        if (ImGui::Checkbox("LinearVelocity", &linearVelocityEnabled)) {
            context.scene->SetSelectedLinearVelocityEnabled(linearVelocityEnabled);
        }
        if (linearVelocityEnabled) {
            glm::vec3 linearVelocity = selected->linearVelocity;
            if (ImGui::DragFloat3("Velocity", &linearVelocity.x, 0.05f)) {
                context.scene->SetSelectedLinearVelocity(linearVelocity);
            }
        }

        bool angularVelocityEnabled = selected->angularVelocityEnabled;
        if (ImGui::Checkbox("AngularVelocity", &angularVelocityEnabled)) {
            context.scene->SetSelectedAngularVelocityEnabled(angularVelocityEnabled);
        }
        if (angularVelocityEnabled) {
            glm::vec3 angularAxis = selected->angularVelocityAxis;
            float angularSpeed = selected->angularVelocityRadians;
            if (ImGui::DragFloat3("Axis", &angularAxis.x, 0.05f)) {
                context.scene->SetSelectedAngularVelocity(angularAxis, angularSpeed);
            }
            if (ImGui::DragFloat("Speed", &angularSpeed, 0.05f)) {
                context.scene->SetSelectedAngularVelocity(angularAxis, angularSpeed);
            }
            if (ImGui::Button("Spin Y")) {
                context.scene->SetSelectedAngularVelocity(glm::vec3(0.0f, 1.0f, 0.0f), 1.57f);
            }
        }

        if (ImGui::Button("Clear Runtime Motion")) {
            context.scene->SetSelectedLinearVelocityEnabled(false);
            context.scene->SetSelectedAngularVelocityEnabled(false);
        }

        ImGui::Separator();
        const engine::ecs::Transform* selectedTransform = context.scene->TryGetTransform(selected->entity);
        if (ImGui::Button("Dynamic Body")) {
            engine::ecs::RigidBody rigidBody = engine::ecs::RigidBody::Dynamic(1.0f);
            rigidBody.velocity = selected->rigidBody.velocity;
            context.scene->SetSelectedRigidBody(rigidBody);

            engine::ecs::Collider collider = selected->colliderEnabled
                ? selected->collider
                : DefaultColliderForObject(*selected, selectedTransform);
            collider.isTrigger = false;
            context.scene->SetSelectedCollider(collider);
        }
        ImGui::SameLine();
        if (ImGui::Button("Static Collider")) {
            context.scene->SetSelectedRigidBodyEnabled(false);
            engine::ecs::Collider collider = selected->colliderEnabled
                ? selected->collider
                : DefaultColliderForObject(*selected, selectedTransform);
            collider.isTrigger = false;
            context.scene->SetSelectedCollider(collider);
        }
        if (ImGui::Button("Trigger##PhysicsPreset")) {
            context.scene->SetSelectedRigidBodyEnabled(false);
            engine::ecs::Collider collider = selected->colliderEnabled
                ? selected->collider
                : DefaultColliderForObject(*selected, selectedTransform);
            collider.isTrigger = true;
            context.scene->SetSelectedCollider(collider);
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Physics")) {
            context.scene->SetSelectedRigidBodyEnabled(false);
            context.scene->SetSelectedColliderEnabled(false);
        }

        bool rigidBodyEnabled = selected->rigidBodyEnabled;
        if (ImGui::Checkbox("RigidBody", &rigidBodyEnabled)) {
            context.scene->SetSelectedRigidBodyEnabled(rigidBodyEnabled);
        }
        if (rigidBodyEnabled) {
            engine::ecs::RigidBody rigidBody = selected->rigidBody;
            bool dynamicBody = rigidBody.invMass > 0.0f;
            if (ImGui::Checkbox("Dynamic", &dynamicBody)) {
                if (dynamicBody && rigidBody.invMass <= 0.0f) {
                    rigidBody.invMass = 1.0f;
                    rigidBody.useGravity = true;
                } else if (!dynamicBody) {
                    rigidBody.invMass = 0.0f;
                    rigidBody.useGravity = false;
                }
                context.scene->SetSelectedRigidBody(rigidBody);
            }
            float mass = rigidBody.invMass > 0.0f ? 1.0f / rigidBody.invMass : 0.0f;
            if (ImGui::DragFloat("Mass", &mass, 0.05f, 0.0f, 10000.0f)) {
                rigidBody.invMass = mass > 0.0f ? 1.0f / mass : 0.0f;
                if (rigidBody.invMass == 0.0f) {
                    rigidBody.useGravity = false;
                }
                context.scene->SetSelectedRigidBody(rigidBody);
            }
            if (ImGui::DragFloat3("Physics Velocity", &rigidBody.velocity.x, 0.05f)) {
                context.scene->SetSelectedRigidBody(rigidBody);
            }
            if (ImGui::Checkbox("Gravity", &rigidBody.useGravity)) {
                context.scene->SetSelectedRigidBody(rigidBody);
            }
            if (ImGui::Checkbox("Allow Sleep", &rigidBody.allowSleep)) {
                context.scene->SetSelectedRigidBody(rigidBody);
            }
            if (ImGui::Checkbox("CCD", &rigidBody.ccd)) {
                context.scene->SetSelectedRigidBody(rigidBody);
            }
        }

        bool colliderEnabled = selected->colliderEnabled;
        if (ImGui::Checkbox("Collider", &colliderEnabled)) {
            context.scene->SetSelectedColliderEnabled(colliderEnabled);
        }
        if (colliderEnabled) {
            engine::ecs::Collider collider = selected->collider;
            int shapeIndex = ColliderShapeIndex(collider.shape);
            const char* shapes[] = {"Sphere", "Box", "Plane"};
            if (ImGui::Combo("Collider Shape", &shapeIndex, shapes, 3)) {
                collider.shape = ColliderShapeFromIndex(shapeIndex);
                context.scene->SetSelectedCollider(collider);
            }
            if (collider.shape == engine::ecs::ColliderShape::Sphere) {
                if (ImGui::DragFloat("Radius", &collider.radius, 0.02f, 0.001f, 1000.0f)) {
                    context.scene->SetSelectedCollider(collider);
                }
            } else if (collider.shape == engine::ecs::ColliderShape::Box) {
                if (ImGui::DragFloat3("Half Extents", &collider.halfExtents.x, 0.02f, 0.001f, 1000.0f)) {
                    context.scene->SetSelectedCollider(collider);
                }
            } else {
                if (ImGui::DragFloat3("Plane Normal", &collider.planeNormal.x, 0.02f)) {
                    context.scene->SetSelectedCollider(collider);
                }
                if (ImGui::DragFloat("Plane Offset", &collider.planeOffset, 0.02f)) {
                    context.scene->SetSelectedCollider(collider);
                }
            }
            if (ImGui::DragFloat("Restitution", &collider.restitution, 0.02f, 0.0f, 1.0f)) {
                context.scene->SetSelectedCollider(collider);
            }
            if (ImGui::DragFloat("Friction", &collider.friction, 0.02f, 0.0f, 10.0f)) {
                context.scene->SetSelectedCollider(collider);
            }
            if (ImGui::Checkbox("Trigger##ColliderMode", &collider.isTrigger)) {
                context.scene->SetSelectedCollider(collider);
            }
        }
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
            if (ImGui::MenuItem("New Scene")) {
                context.newSceneRequested = true;
            }
            if (ImGui::MenuItem("Play", "P", false, !context.playMode)) {
                context.enterPlayModeRequested = true;
            }
            if (ImGui::MenuItem("Stop", "P", false, context.playMode)) {
                context.exitPlayModeRequested = true;
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
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) {
                context.undoRequested = true;
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) {
                context.redoRequested = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, context.scene && context.scene->SelectedObject())) {
                context.duplicateSelectedRequested = true;
            }
            if (ImGui::MenuItem("Delete", "Del", false, context.scene && context.scene->SelectedObject())) {
                context.deleteSelectedRequested = true;
            }
            if (ImGui::MenuItem("Frame Selected", "F", false, context.scene && context.scene->SelectedObject())) {
                context.frameSelectedRequested = true;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Add")) {
            if (ImGui::BeginMenu("Primitives")) {
                if (ImGui::MenuItem("Cube")) {
                    context.addCubeRequested = true;
                }
                if (ImGui::MenuItem("Plane")) {
                    context.addPlaneRequested = true;
                }
                if (ImGui::MenuItem("Sphere")) {
                    context.addSphereRequested = true;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Physics")) {
                if (ImGui::MenuItem("Dynamic Cube")) {
                    context.addDynamicCubeRequested = true;
                }
                if (ImGui::MenuItem("Static Floor")) {
                    context.addStaticFloorRequested = true;
                }
                if (ImGui::MenuItem("Trigger Volume")) {
                    context.addTriggerVolumeRequested = true;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Lights")) {
                if (ImGui::MenuItem("Directional Light")) {
                    context.addDirectionalLightRequested = true;
                }
                if (ImGui::MenuItem("Point Light")) {
                    context.addPointLightRequested = true;
                }
                if (ImGui::MenuItem("Spot Light")) {
                    context.addSpotLightRequested = true;
                }
                if (ImGui::MenuItem("Area Light")) {
                    context.addAreaLightRequested = true;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (context.gizmo && ImGui::BeginMenu("Gizmo")) {
            if (ImGui::BeginMenu("Mode")) {
                const EditorGizmo::Mode mode = context.gizmo->CurrentMode();
                if (ImGui::MenuItem("Move", "G", mode == EditorGizmo::Mode::Translate)) {
                    context.gizmo->SetMode(EditorGizmo::Mode::Translate);
                }
                if (ImGui::MenuItem("Rotate", nullptr, mode == EditorGizmo::Mode::Rotate)) {
                    context.gizmo->SetMode(EditorGizmo::Mode::Rotate);
                }
                if (ImGui::MenuItem("Scale", nullptr, mode == EditorGizmo::Mode::Scale)) {
                    context.gizmo->SetMode(EditorGizmo::Mode::Scale);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Axis")) {
                const EditorGizmo::Axis axis = context.gizmo->CurrentAxis();
                if (ImGui::MenuItem("X", "X", axis == EditorGizmo::Axis::X)) {
                    context.gizmo->SetAxis(EditorGizmo::Axis::X);
                }
                if (ImGui::MenuItem("Y", "Y", axis == EditorGizmo::Axis::Y)) {
                    context.gizmo->SetAxis(EditorGizmo::Axis::Y);
                }
                if (ImGui::MenuItem("Z", "Z", axis == EditorGizmo::Axis::Z)) {
                    context.gizmo->SetAxis(EditorGizmo::Axis::Z);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Panels")) {
            if (ImGui::MenuItem("Show All")) {
                context.panels->ShowAll();
            }
            if (ImGui::MenuItem("Hide All")) {
                context.panels->HideAll();
            }
            if (ImGui::MenuItem("Reset Defaults")) {
                context.panels->ResetDefaults();
            }
            ImGui::Separator();
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
        case EditorPanels::Panel::PhysicsStatus:
            DrawPhysicsStatus(context, &open);
        case EditorPanels::Panel::Count:
            break;
        }

        context.panels->SetOpen(panel, open);
    }

    //DrawGizmoToolbar(context);

    ImGui::End();
    return true;
}

bool EditorDockspace::IsCompiledWithImGui() const
{
    return false;
}
