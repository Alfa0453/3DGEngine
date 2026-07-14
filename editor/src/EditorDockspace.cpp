#include "EditorDockspace.h"

#include <engine/ecs/Components.h>
#include <engine/graphics/SkinnedModel.h>
#include <engine/physics/PhysicsComponents.h>

#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::array<char, 128> g_objectNameBuffer{};
std::array<char, 128> g_scriptClassBuffer{};
engine::ecs::Entity g_objectNameEntity = engine::ecs::kNull;
engine::ecs::Entity g_scriptEntity = engine::ecs::kNull;
int g_scriptTemplateIndex = 0;
ImGuiTextFilter g_hierarchyFilter;

const char* PrimitiveName(EditorScene::Primitive primitive) {
    if (primitive == EditorScene::Primitive::Plane) {
        return "Plane";
    }
    if (primitive == EditorScene::Primitive::Sphere) {
        return "Sphere";
    }
    return "Cube";
}

const char* ObjectTypeName(const EditorScene::Object& object) {
    if (object.light) {
        return "Light";
    }
    if (object.skeletalModel) {
        return "Skeletal Model";
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

const char* TriggerActionModeName(EditorScene::TriggerActionMode mode) {
    switch (mode) {
    case EditorScene::TriggerActionMode::None: return "None";
    case EditorScene::TriggerActionMode::Enable: return "Enable";
    case EditorScene::TriggerActionMode::Disable: return "Disable";
    case EditorScene::TriggerActionMode::Toggle: return "Toggle";
    }
    return "None";
}

const char* ScriptFieldTypeName(EditorScene::ScriptField::Type type) {
    switch (type) {
    case EditorScene::ScriptField::Type::Float: return "Float";
    case EditorScene::ScriptField::Type::Int: return "Int";
    case EditorScene::ScriptField::Type::Bool: return "Bool";
    case EditorScene::ScriptField::Type::String: return "String";
    }
    return "Float";
}

EditorScene::ScriptField::Type ScriptFieldTypeFromIndex(int index) {
    switch (index) {
    case 1: return EditorScene::ScriptField::Type::Int;
    case 2: return EditorScene::ScriptField::Type::Bool;
    case 3: return EditorScene::ScriptField::Type::String;
    default: return EditorScene::ScriptField::Type::Float;
    }
}

int ScriptFieldTypeIndex(EditorScene::ScriptField::Type type) {
    switch (type) {
    case EditorScene::ScriptField::Type::Float: return 0;
    case EditorScene::ScriptField::Type::Int: return 1;
    case EditorScene::ScriptField::Type::Bool: return 2;
    case EditorScene::ScriptField::Type::String: return 3;
    }
    return 0;
}

enum class ScriptTemplate {
    Empty = 0,
    PlayerMovement = 1,
    DoorOpener = 2,
    Pickup = 3,
    DamageZone = 4
};

ScriptTemplate ScriptTemplateFromIndex(int index) {
    switch (index) {
    case 1: return ScriptTemplate::PlayerMovement;
    case 2: return ScriptTemplate::DoorOpener;
    case 3: return ScriptTemplate::Pickup;
    case 4: return ScriptTemplate::DamageZone;
    default: return ScriptTemplate::Empty;
    }
}

std::vector<EditorScene::ScriptField> DefaultFieldsForTemplate(ScriptTemplate scriptTemplate) {
    using Field = EditorScene::ScriptField;
    std::vector<Field> fields;
    auto add = [&fields](const char* name, Field::Type type, const char* value) {
        Field field;
        field.name = name;
        field.type = type;
        field.value = value;
        fields.push_back(field);
    };

    switch (scriptTemplate) {
    case ScriptTemplate::PlayerMovement:
        add("speed", Field::Type::Float, "4.0");
        break;
    case ScriptTemplate::DoorOpener:
        add("target", Field::Type::String, "Door");
        add("speed", Field::Type::Float, "2.0");
        add("height", Field::Type::Float, "3.0");
        break;
    case ScriptTemplate::Pickup:
        add("interactKey", Field::Type::String, "E");
        break;
    case ScriptTemplate::DamageZone:
        add("target", Field::Type::String, "PlayerStart");
        add("damagePerSecond", Field::Type::Float, "10.0");
        break;
    case ScriptTemplate::Empty:
        break;
    }
    return fields;
}

std::string ScriptTemplateDescription(ScriptTemplate scriptTemplate) {
    switch (scriptTemplate) {
    case ScriptTemplate::PlayerMovement: return "Moves this object with WASD using the `speed` field.";
    case ScriptTemplate::DoorOpener: return "Moves a named target upward while E is held.";
    case ScriptTemplate::Pickup: return "Destroys this object when E is pressed.";
    case ScriptTemplate::DamageZone: return "Damages a named Health target every update.";
    case ScriptTemplate::Empty: return "Blank script with helper comments.";
    }
    return "Blank script with helper comments.";
}

void WriteTemplateUpdateBody(std::ostringstream& source, ScriptTemplate scriptTemplate) {
    switch (scriptTemplate) {
    case ScriptTemplate::PlayerMovement:
        source << "    const float speed = GetFieldFloat(\"speed\", 4.0f);\n"
               << "    auto* transform = Transform();\n"
               << "    if (!transform) {\n"
               << "        return;\n"
               << "    }\n"
               << "    glm::vec3 move(0.0f);\n"
               << "    if (IsKeyDown('W')) move.z -= 1.0f;\n"
               << "    if (IsKeyDown('S')) move.z += 1.0f;\n"
               << "    if (IsKeyDown('A')) move.x -= 1.0f;\n"
               << "    if (IsKeyDown('D')) move.x += 1.0f;\n"
               << "    if (glm::length(move) > 0.0f) {\n"
               << "        transform->position += glm::normalize(move) * speed * dt;\n"
               << "    }\n";
        break;
    case ScriptTemplate::DoorOpener:
        source << "    const std::string targetName = GetFieldString(\"target\", \"Door\");\n"
               << "    const float speed = GetFieldFloat(\"speed\", 2.0f);\n"
               << "    const float height = GetFieldFloat(\"height\", 3.0f);\n"
               << "    auto* target = FindTransform(targetName);\n"
               << "    if (!target || !IsKeyDown('E')) {\n"
               << "        return;\n"
               << "    }\n"
               << "    if (target->position.y < height) {\n"
               << "        target->position.y = std::min(height, target->position.y + speed * dt);\n"
               << "    }\n";
        break;
    case ScriptTemplate::Pickup:
        source << "    if (WasKeyPressed('E')) {\n"
               << "        DestroySelf();\n"
               << "    }\n";
        break;
    case ScriptTemplate::DamageZone:
        source << "    const std::string targetName = GetFieldString(\"target\", \"PlayerStart\");\n"
               << "    const float damage = GetFieldFloat(\"damagePerSecond\", 10.0f) * dt;\n"
               << "    const engine::ecs::Entity target = FindObject(targetName);\n"
               << "    if (target == engine::ecs::kNull) {\n"
               << "        return;\n"
               << "    }\n"
               << "    if (!IsTriggerTouching(target)) {\n"
               << "        return;\n"
               << "    }\n"
               << "    if (auto* health = TryGet<engine::Health>(target)) {\n"
               << "        health->Damage(damage);\n"
               << "    }\n";
        break;
    case ScriptTemplate::Empty:
        source << "    (void)dt;\n"
               << "    // Called every Play-mode update.\n"
               << "    // Helpers: GetFieldFloat(\"speed\", 1.0f), IsKeyDown(key), WasKeyPressed(key), Transform(), FindObject(\"Door\"), DestroySelf().\n";
        break;
    }
}

bool DrawTriggerActionModeCombo(const char* label, EditorScene::TriggerActionMode* mode) {
    bool changed = false;
    if (ImGui::BeginCombo(label, TriggerActionModeName(*mode))) {
        const EditorScene::TriggerActionMode modes[] = {
            EditorScene::TriggerActionMode::None,
            EditorScene::TriggerActionMode::Enable,
            EditorScene::TriggerActionMode::Disable,
            EditorScene::TriggerActionMode::Toggle
        };
        for (EditorScene::TriggerActionMode candidate : modes) {
            const bool selected = candidate == *mode;
            if (ImGui::Selectable(TriggerActionModeName(candidate), selected)) {
                *mode = candidate;
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

std::string SanitizeScriptClassName(const std::string& raw) {
    std::string result;
    result.reserve(raw.size());
    for (char ch : raw) {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (std::isalnum(u) || ch == '_') {
            result.push_back(ch);
        }
    }
    if (result.empty()) {
        return {};
    }
    if (std::isdigit(static_cast<unsigned char>(result.front()))) {
        result.insert(result.begin(), '_');
    }
    return result;
}

std::filesystem::path ScriptRootFor(const EditorDockspace::Context& context) {
    if (context.assets && !context.assets->RootPath().empty()) {
        const std::filesystem::path assetRoot(context.assets->RootPath());
        const std::filesystem::path parent = assetRoot.parent_path();
        return (parent.empty() ? std::filesystem::path("Game") : parent / "Game") / "Scripts";
    }
    return std::filesystem::path("Game") / "Scripts";
}

bool WriteTextFile(const std::filesystem::path& path, const std::string& text, std::string* error) {
    std::ofstream out(path);
    if (!out) {
        if (error) {
            *error = "Could not write " + path.string();
        }
        return false;
    }
    out << text;
    return true;
}

bool CreateScriptFiles(const EditorDockspace::Context& context,
                       const std::string& className,
                       ScriptTemplate scriptTemplate,
                       std::string* scriptPath,
                       std::string* error) {
    const std::filesystem::path scriptRoot = ScriptRootFor(context);
    std::error_code ec;
    std::filesystem::create_directories(scriptRoot, ec);
    if (ec) {
        if (error) {
            *error = "Could not create script folder: " + ec.message();
        }
        return false;
    }

    const std::filesystem::path headerPath = scriptRoot / (className + ".h");
    const std::filesystem::path sourcePath = scriptRoot / (className + ".cpp");
    const std::filesystem::path docsRoot = scriptRoot / "docs";
    std::filesystem::create_directories(docsRoot, ec);
    if (ec) {
        if (error) {
            *error = "Could not create script docs folder: " + ec.message();
        }
        return false;
    }
    const std::filesystem::path docPath = docsRoot / (className + ".md");

    if (!std::filesystem::exists(headerPath)) {
        std::ostringstream header;
        header << "#pragma once\n\n"
               << "#include <engine/gameplay/Script.h>\n\n"
               << "class " << className << " : public engine::Script {\n"
               << "public:\n"
               << "    void OnCreate() override;\n"
               << "    void OnUpdate(float dt) override;\n"
               << "};\n\n"
               << "void Register" << className << "Script();\n";
        if (!WriteTextFile(headerPath, header.str(), error)) {
            return false;
        }
    }

    if (!std::filesystem::exists(sourcePath)) {
        std::ostringstream source;
        source << "#include \"" << className << ".h\"\n\n"
               << "#include <engine/ecs/Entity.h>\n"
               << "#include <engine/gameplay/GameplayComponents.h>\n"
               << "#include <engine/gameplay/Script.h>\n\n"
               << "#include <algorithm>\n"
               << "#include <glm/geometric.hpp>\n"
               << "#include <memory>\n\n"
               << "void " << className << "::OnCreate() {\n"
               << "    // Called when the object enters Play mode.\n"
               << "    // Example: if (auto* transform = Transform()) { transform->position.y += 1.0f; }\n"
               << "}\n\n"
               << "void " << className << "::OnUpdate(float dt) {\n";
        WriteTemplateUpdateBody(source, scriptTemplate);
        source << "}\n\n"
               << "void Register" << className << "Script() {\n"
               << "    engine::ScriptRegistry::Instance().Register(\"" << className << "\", [] {\n"
               << "        return std::make_unique<" << className << ">();\n"
               << "    });\n"
               << "}\n";
        if (!WriteTextFile(sourcePath, source.str(), error)) {
            return false;
        }
    }

    if (!std::filesystem::exists(docPath)) {
        std::ostringstream doc;
        doc << "# " << className << "\n\n"
            << "## What It Does\n\n"
            << "Gameplay script template generated by the editor. " << ScriptTemplateDescription(scriptTemplate) << " It derives from `engine::Script` so it can be attached to a scene object and run in Play mode after registration.\n\n"
            << "## How To Use It\n\n"
            << "Use `OnCreate()` for one-time setup when the object enters Play mode. Use `OnUpdate(float dt)` for fixed-step behavior. Call `Register" << className << "Script()` from your game startup before entering Play mode so `ScriptRegistry` can construct the script from its class name. Common helpers include `GetFieldFloat()`, `GetFieldInt()`, `GetFieldBool()`, `GetFieldString()`, `Self()`, `Transform()`, `FindObject()`, `FindTransform()`, `IsKeyDown()`, `WasKeyPressed()`, `MouseDeltaX()`, and `DestroySelf()`.\n";
        if (!WriteTextFile(docPath, doc.str(), error)) {
            return false;
        }
    }

    if (scriptPath) {
        *scriptPath = (std::filesystem::path("Game") / "Scripts" / (className + ".cpp")).string();
    }
    return true;
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
    std::size_t joints = 0;
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
    status.joints = scene.PhysicsJoints().size();

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

const char* PhysicsJointTypeName(EditorScene::PhysicsJoint::Type type, bool rope) {
    if (type == EditorScene::PhysicsJoint::Type::Spring) {
        return "Spring";
    }
    return rope ? "Rope" : "Distance";
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
    // A low restitution so editor bodies settle on landing instead of bouncing.
    constexpr float kDefaultRestitution = 0.1f;

    if (object.primitive == EditorScene::Primitive::Plane && object.modelAssetPath.empty()) {
        const float planeOffset = transform ? transform->position.y : 0.0f;
        engine::ecs::Collider collider = engine::ecs::Collider::MakePlane(glm::vec3(0.0f, 1.0f, 0.0f), planeOffset);
        collider.restitution = kDefaultRestitution;
        return collider;
    }

    engine::ecs::Collider collider = transform
        ? engine::ecs::Collider::MakeBox(glm::vec3(
              std::max(transform->scale.x * 0.5f, 0.001f),
              std::max(transform->scale.y * 0.5f, 0.001f),
              std::max(transform->scale.z * 0.5f, 0.001f)))
        : engine::ecs::Collider::MakeBox(glm::vec3(0.5f));
    collider.restitution = kDefaultRestitution;
    return collider;
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
    case EditorLog::Level::Warning: return ImVec4(1.0f, 0.78f, 0.30f, 1.0f);
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
            environment.physicsSleepAngularVelocity = defaults.physicsSleepAngularVelocity;
            environment.physicsTimeToSleep = defaults.physicsTimeToSleep;
            changed = true;
        }
        changed |= ImGui::DragFloat3("Gravity", &environment.physicsGravity.x, 0.05f);
        changed |= ImGui::DragInt("Solver Iterations", &environment.physicsSolverIterations, 1.0f, 1, 32);
        changed |= ImGui::Checkbox("Broad Phase", &environment.physicsBroadPhase);
        changed |= ImGui::DragFloat("Cell Size", &environment.physicsCellSize, 0.05f, 0.1f, 100.0f, "%.2f");
        changed |= ImGui::DragFloat("Bounce Threshold", &environment.physicsRestitutionThreshold, 0.02f, 0.0f, 10.0f, "%.2f");
        changed |= ImGui::Checkbox("Allow Sleeping", &environment.physicsAllowSleeping);
        changed |= ImGui::DragFloat("Sleep Linear Velocity", &environment.physicsSleepLinearVelocity, 0.005f, 0.0f, 10.0f, "%.3f");
        changed |= ImGui::DragFloat("Sleep Angular Velocity", &environment.physicsSleepAngularVelocity, 0.005f, 0.0f, 10.0f, "%.3f");
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
        context.physicsEventExitCount,
        context.physicsActionCount);

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

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Sleep Linear");
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", environment.physicsSleepLinearVelocity);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Sleep Angular");
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", environment.physicsSleepAngularVelocity);
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
            DrawStatusRow("Joints", status.joints);
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
                ImGui::Text("Freeze Rotation: %s", selected->rigidBody.freezeRotation ? "On" : "Off");
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

    if (ImGui::CollapsingHeader("Scene Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (context.showPhysicsEventGuides) {
            ImGui::Checkbox("Event Guides", context.showPhysicsEventGuides);
        }
        if (context.physicsEventGuidesSelectedOnly) {
            ImGui::Checkbox("Selected Events Only", context.physicsEventGuidesSelectedOnly);
        }
        if (context.physicsEventGuidesTriggersOnly) {
            ImGui::Checkbox("Trigger Events Only", context.physicsEventGuidesTriggersOnly);
        }
        if (context.physicsEventGuidesEnterExitOnly) {
            ImGui::Checkbox("Enter/Exit Events Only", context.physicsEventGuidesEnterExitOnly);
        }
        if (ImGui::Button("Clear Recent Events")) {
            context.clearPhysicsEventGuidesRequested = true;
        }
        ImGui::TextUnformatted("Light and collider guide visibility is in World Settings.");
    }

    if (ImGui::CollapsingHeader("Recent Events", ImGuiTreeNodeFlags_DefaultOpen)) {
        static bool selectedOnly = false;
        static bool triggersOnly = false;
        static bool enterExitOnly = false;
        static bool actionsOnly = false;

        ImGui::Checkbox("Selected Only##PhysicsEvents", &selectedOnly);
        ImGui::SameLine();
        ImGui::Checkbox("Triggers Only##PhysicsEvents", &triggersOnly);
        ImGui::SameLine();
        ImGui::Checkbox("Enter/Exit Only##PhysicsEvents", &enterExitOnly);
        ImGui::SameLine();
        ImGui::Checkbox("Actions Only##PhysicsEvents", &actionsOnly);

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
                if (actionsOnly && !row.action) {
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

void DrawGameplayDebug(EditorDockspace::Context& context, bool* open) {
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::GameplayDebug), open)) {
        ImGui::End();
        return;
    }

    const EditorDockspace::GameplayDebugState& debug = context.gameplayDebug;
    ImGui::Text("Mode: %s", context.playMode ? "Play" : "Edit");
    if (!debug.hasSelection) {
        ImGui::TextUnformatted("No object selected.");
        ImGui::End();
        return;
    }

    ImGui::Text("Selected: %s", debug.selectedName.c_str());
    if (!context.playMode) {
        ImGui::TextUnformatted("Enter Play mode to inspect runtime script and Health state.");
    } else if (!debug.playEntityFound) {
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.30f, 1.0f),
            "Selected object was not found in the Play registry.");
    }

    if (ImGui::CollapsingHeader("Health", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!debug.hasHealth) {
            ImGui::TextUnformatted("No runtime Health component.");
        } else {
            ImGui::Text("HP: %.2f / %.2f", debug.health, debug.maxHealth);
            ImGui::Text("Alive: %s", debug.healthAlive ? "Yes" : "No");
            ImGui::Text("Just Died: %s", debug.healthJustDied ? "Yes" : "No");
            const float fraction = debug.maxHealth > 0.0f ? debug.health / debug.maxHealth : 0.0f;
            ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f));
        }
    }

    if (ImGui::CollapsingHeader("Script", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!debug.hasScript) {
            ImGui::TextUnformatted("No runtime NativeScriptComponent.");
        } else {
            ImGui::Text("Class: %s", debug.scriptClassName.empty() ? "-" : debug.scriptClassName.c_str());
            ImGui::Text("Path: %s", debug.scriptPath.empty() ? "-" : debug.scriptPath.c_str());
            ImGui::Text("Enabled: %s", debug.scriptEnabled ? "Yes" : "No");
            ImGui::Text("Created: %s", debug.scriptCreated ? "Yes" : "No");
            ImGui::Text("Missing Factory: %s", debug.scriptMissingFactory ? "Yes" : "No");
            ImGui::Text("Fields: %d authored, %d runtime",
                debug.authoredFieldCount,
                debug.runtimeFieldCount);
            if (debug.scriptMissingFactory) {
                ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.34f, 1.0f),
                    "Register this script class before entering Play mode.");
            }
        }
    }

    if (ImGui::CollapsingHeader("Trigger Contacts", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!context.playMode) {
            ImGui::TextUnformatted("Trigger contact counts are available in Play mode.");
        } else {
            ImGui::Text("Touching/Stay: %d", debug.selectedTriggerTouchCount);
            ImGui::Text("Entered: %d", debug.selectedTriggerEnterCount);
            ImGui::Text("Exited: %d", debug.selectedTriggerExitCount);
            ImGui::TextUnformatted("Counts come from the most recent physics event rows.");
        }
    }

    ImGui::End();
}

void DrawAnimationPreview(EditorDockspace::Context& context, bool* open) {
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::AnimationPreview), open)) {
        ImGui::End();
        return;
    }

    const EditorDockspace::AnimationPreviewState& state = context.animationPreview;
    if (!state.hasSelection) {
        ImGui::TextUnformatted("No object selected.");
        ImGui::End();
        return;
    }

    ImGui::Text("Object: %s", state.selectedName.c_str());
    ImGui::Text("Mode: %s", state.playMode ? "Play" : "Edit");
    if (!state.skeletalModel) {
        ImGui::TextUnformatted("Selected object is not marked as a skeletal model.");
        ImGui::End();
        return;
    }

    ImGui::TextWrapped("Model: %s", state.modelPath.empty() ? "-" : state.modelPath.c_str());
    if (!state.loadError.empty()) {
        ImGui::TextWrapped("Load error: %s", state.loadError.c_str());
        ImGui::End();
        return;
    }
    if (!state.modelLoaded) {
        ImGui::TextUnformatted("Skeletal model is not loaded yet.");
        ImGui::End();
        return;
    }

    if (!state.playMode && context.scene) {
        if (ImGui::CollapsingHeader("Preview Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Button(state.autoplay ? "Pause" : "Play")) {
                context.scene->SetSelectedAnimationSettings(true,
                    state.defaultClipIndex,
                    state.defaultClipName,
                    !state.autoplay,
                    state.loop,
                    state.playbackSpeed);
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset") && context.animationPreviewTime) {
                *context.animationPreviewTime = 0.0f;
            }

            if (!state.clips.empty()) {
                int selectedClip = std::clamp(state.defaultClipIndex, 0, static_cast<int>(state.clips.size() - 1));
                const std::string currentClipName = state.defaultClipName.empty()
                    ? state.clips[static_cast<std::size_t>(selectedClip)].name
                    : state.defaultClipName;
                if (ImGui::BeginCombo("Preview Clip", currentClipName.empty() ? "(unnamed)" : currentClipName.c_str())) {
                    for (std::size_t i = 0; i < state.clips.size(); ++i) {
                        const std::string label = state.clips[i].name.empty()
                            ? ("Clip " + std::to_string(i))
                            : state.clips[i].name;
                        const bool selected = static_cast<int>(i) == selectedClip;
                        if (ImGui::Selectable(label.c_str(), selected)) {
                            context.scene->SetSelectedAnimationSettings(true,
                                static_cast<int>(i),
                                state.clips[i].name,
                                state.autoplay,
                                state.loop,
                                state.playbackSpeed);
                            if (context.animationPreviewTime) {
                                *context.animationPreviewTime = 0.0f;
                            }
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                float scrubTime = state.previewTime;
                const float maxTime = std::max(state.previewDuration, 0.001f);
                if (ImGui::SliderFloat("Time", &scrubTime, 0.0f, maxTime, "%.2f s")
                    && context.animationPreviewTime) {
                    *context.animationPreviewTime = scrubTime;
                }
                ImGui::Text("Duration: %.2f s", state.previewDuration);
            }
        }
    }

    if (ImGui::CollapsingHeader("Authored", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Default Clip: %d %s", state.defaultClipIndex,
            state.defaultClipName.empty() ? "" : state.defaultClipName.c_str());
        ImGui::Text("Autoplay: %s", state.autoplay ? "Yes" : "No");
        ImGui::SameLine();
        ImGui::Text("Loop: %s", state.loop ? "Yes" : "No");
        ImGui::Text("Speed: %.2f", state.playbackSpeed);
        ImGui::Separator();
        ImGui::Text("Locomotion: %s", state.locomotionEnabled ? "Enabled" : "Disabled");
        if (state.locomotionEnabled) {
            ImGui::Text("Idle: %d %s", state.idleClipIndex, state.idleClipName.c_str());
            ImGui::Text("Walk: %d %s", state.walkClipIndex, state.walkClipName.c_str());
            ImGui::Text("Run: %d %s", state.runClipIndex, state.runClipName.c_str());
            ImGui::Text("Walk At: %.2f", state.walkAt);
            ImGui::SameLine();
            ImGui::Text("Run At: %.2f", state.runAt);
        }
    }

    if (ImGui::CollapsingHeader("Action Test", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (state.clips.empty()) {
            ImGui::TextUnformatted("No animation clips found.");
        } else if (!context.animationActionClip
            || !context.animationActionFadeIn
            || !context.animationActionFadeOut
            || !context.animationActionSpeed) {
            ImGui::TextUnformatted("Action test controls are unavailable.");
        } else {
            *context.animationActionClip = std::clamp(
                *context.animationActionClip,
                0,
                static_cast<int>(state.clips.size() - 1));

            const int actionClip = *context.animationActionClip;
            const std::string currentActionClipName = state.clips[static_cast<std::size_t>(actionClip)].name.empty()
                ? ("Clip " + std::to_string(actionClip))
                : state.clips[static_cast<std::size_t>(actionClip)].name;
            if (ImGui::BeginCombo("Action Clip", currentActionClipName.c_str())) {
                for (std::size_t i = 0; i < state.clips.size(); ++i) {
                    const std::string label = state.clips[i].name.empty()
                        ? ("Clip " + std::to_string(i))
                        : state.clips[i].name;
                    const bool selected = static_cast<int>(i) == actionClip;
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        *context.animationActionClip = static_cast<int>(i);
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::DragFloat("Fade In", context.animationActionFadeIn, 0.01f, 0.0f, 5.0f, "%.2f s");
            ImGui::DragFloat("Fade Out", context.animationActionFadeOut, 0.01f, 0.0f, 5.0f, "%.2f s");
            ImGui::DragFloat("Speed", context.animationActionSpeed, 0.01f, 0.0f, 5.0f, "%.2f");
            if (context.animationActionMaskRoot && context.animationActionMaskRootSize > 0) {
                ImGui::InputText("Mask Root Bone",
                    context.animationActionMaskRoot,
                    context.animationActionMaskRootSize);
                if (!state.bones.empty()) {
                    const char* currentMask = context.animationActionMaskRoot[0] == '\0'
                        ? "Full Body"
                        : context.animationActionMaskRoot;
                    if (ImGui::BeginCombo("Pick Mask Bone", currentMask)) {
                        if (ImGui::Selectable("Full Body", context.animationActionMaskRoot[0] == '\0')) {
                            context.animationActionMaskRoot[0] = '\0';
                        }
                        for (const EditorDockspace::AnimationPreviewState::BoneInfo& bone : state.bones) {
                            std::string label(static_cast<std::size_t>(std::max(bone.depth, 0)) * 2, ' ');
                            label += bone.name.empty() ? "(unnamed)" : bone.name;
                            const bool selected = bone.name == context.animationActionMaskRoot;
                            if (ImGui::Selectable(label.c_str(), selected)) {
                                std::snprintf(context.animationActionMaskRoot,
                                    context.animationActionMaskRootSize,
                                    "%s",
                                    bone.name.c_str());
                            }
                            if (selected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
            }
            *context.animationActionFadeIn = std::max(*context.animationActionFadeIn, 0.0f);
            *context.animationActionFadeOut = std::max(*context.animationActionFadeOut, 0.0f);
            *context.animationActionSpeed = std::max(*context.animationActionSpeed, 0.0f);

            if (ImGui::Button("Play Action")) {
                context.animationActionRequested = true;
            }
            ImGui::SameLine();
            ImGui::Text("Status: %s", state.actionPlaying ? "Playing" : "Idle");
        }
    }

    if (ImGui::CollapsingHeader("State Graph", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (state.playMode || !context.scene) {
            ImGui::TextUnformatted("State graphs are edited in Edit mode.");
        } else if (state.clips.empty()) {
            ImGui::TextUnformatted("No animation clips found.");
        } else {
            std::vector<EditorScene::AnimationStateNode> states = state.states;
            std::vector<EditorScene::AnimationStateTransition> transitions = state.transitions;
            bool changed = false;
            int removeState = -1;
            int removeTransition = -1;

            ImGui::Text("States: %zu", states.size());
            for (std::size_t i = 0; i < states.size(); ++i) {
                EditorScene::AnimationStateNode& node = states[i];
                ImGui::PushID(static_cast<int>(i));
                std::array<char, 96> nameBuffer{};
                std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", node.name.c_str());
                if (ImGui::InputText("State", nameBuffer.data(), nameBuffer.size())) {
                    node.name = nameBuffer.data();
                    changed = true;
                }

                node.clipIndex = std::clamp(node.clipIndex, 0, static_cast<int>(state.clips.size() - 1));
                const std::string currentClip = node.clipName.empty()
                    ? (state.clips[static_cast<std::size_t>(node.clipIndex)].name.empty()
                        ? ("Clip " + std::to_string(node.clipIndex))
                        : state.clips[static_cast<std::size_t>(node.clipIndex)].name)
                    : node.clipName;
                if (ImGui::BeginCombo("Clip", currentClip.empty() ? "(unnamed)" : currentClip.c_str())) {
                    for (std::size_t clip = 0; clip < state.clips.size(); ++clip) {
                        const std::string label = state.clips[clip].name.empty()
                            ? ("Clip " + std::to_string(clip))
                            : state.clips[clip].name;
                        const bool selectedClip = static_cast<int>(clip) == node.clipIndex;
                        if (ImGui::Selectable(label.c_str(), selectedClip)) {
                            node.clipIndex = static_cast<int>(clip);
                            node.clipName = state.clips[clip].name;
                            changed = true;
                        }
                        if (selectedClip) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::Checkbox("Loop", &node.loop)) {
                    changed = true;
                }
                if (ImGui::DragFloat("Speed", &node.speed, 0.01f, 0.0f, 5.0f, "%.2f")) {
                    node.speed = std::max(node.speed, 0.0f);
                    changed = true;
                }
                if (ImGui::Button("Remove State")) {
                    removeState = static_cast<int>(i);
                }
                ImGui::Separator();
                ImGui::PopID();
            }

            if (removeState >= 0) {
                const std::string removedName = states[static_cast<std::size_t>(removeState)].name;
                states.erase(states.begin() + removeState);
                transitions.erase(std::remove_if(transitions.begin(), transitions.end(),
                    [&](const EditorScene::AnimationStateTransition& transition) {
                        return transition.fromState == removedName || transition.toState == removedName;
                    }), transitions.end());
                changed = true;
            }

            if (ImGui::Button("Add State")) {
                const int clip = std::clamp(state.defaultClipIndex, 0, static_cast<int>(state.clips.size() - 1));
                states.push_back(EditorScene::AnimationStateNode{
                    "State",
                    clip,
                    state.clips[static_cast<std::size_t>(clip)].name,
                    true,
                    1.0f
                });
                changed = true;
            }

            ImGui::Separator();
            ImGui::Text("Transitions: %zu", transitions.size());
            const char* compareLabels[] = {">=", "<", "==", "!="};
            for (std::size_t i = 0; i < transitions.size(); ++i) {
                EditorScene::AnimationStateTransition& transition = transitions[i];
                ImGui::PushID(10000 + static_cast<int>(i));

                auto drawStateCombo = [&](const char* label, std::string& value) {
                    const char* current = value.empty() ? "-" : value.c_str();
                    if (ImGui::BeginCombo(label, current)) {
                        for (const EditorScene::AnimationStateNode& node : states) {
                            const bool selectedNode = node.name == value;
                            if (ImGui::Selectable(node.name.empty() ? "(unnamed)" : node.name.c_str(), selectedNode)) {
                                value = node.name;
                                changed = true;
                            }
                            if (selectedNode) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                };

                drawStateCombo("From", transition.fromState);
                drawStateCombo("To", transition.toState);

                std::array<char, 64> parameterBuffer{};
                std::snprintf(parameterBuffer.data(), parameterBuffer.size(), "%s", transition.parameter.c_str());
                if (ImGui::InputText("Parameter", parameterBuffer.data(), parameterBuffer.size())) {
                    transition.parameter = parameterBuffer.data();
                    changed = true;
                }

                int compare = static_cast<int>(transition.compare);
                compare = std::clamp(compare, 0, 3);
                if (ImGui::BeginCombo("Compare", compareLabels[compare])) {
                    for (int c = 0; c < 4; ++c) {
                        const bool selectedCompare = c == compare;
                        if (ImGui::Selectable(compareLabels[c], selectedCompare)) {
                            transition.compare = static_cast<EditorScene::AnimationStateTransition::Compare>(c);
                            changed = true;
                        }
                        if (selectedCompare) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::DragFloat("Threshold", &transition.threshold, 0.01f, -1000.0f, 1000.0f, "%.2f")) {
                    changed = true;
                }
                if (ImGui::DragFloat("Fade", &transition.fade, 0.01f, 0.0f, 5.0f, "%.2f s")) {
                    transition.fade = std::max(transition.fade, 0.0f);
                    changed = true;
                }
                if (ImGui::SliderFloat("Exit Time", &transition.exitTime, 0.0f, 1.0f, "%.2f")) {
                    transition.exitTime = std::clamp(transition.exitTime, 0.0f, 1.0f);
                    changed = true;
                }
                if (ImGui::DragInt("Priority", &transition.priority, 1.0f, -100, 100)) {
                    changed = true;
                }
                if (ImGui::Checkbox("Interrupt Blend", &transition.canInterrupt)) {
                    changed = true;
                }
                if (ImGui::Button("Remove Transition")) {
                    removeTransition = static_cast<int>(i);
                }
                ImGui::Separator();
                ImGui::PopID();
            }

            if (removeTransition >= 0) {
                transitions.erase(transitions.begin() + removeTransition);
                changed = true;
            }
            if (ImGui::Button("Add Transition")) {
                const std::string from = states.empty() ? std::string() : states.front().name;
                const std::string to = states.size() > 1 ? states[1].name : from;
                transitions.push_back(EditorScene::AnimationStateTransition{
                    from,
                    to,
                    "Speed",
                    EditorScene::AnimationStateTransition::Compare::GreaterOrEqual,
                    0.0f,
                    0.2f
                });
                changed = true;
            }

            if (changed) {
                context.scene->SetSelectedAnimationStateGraph(states, transitions);
            }
        }
    }

    if (ImGui::CollapsingHeader("Graph Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (state.parameters.empty()) {
            ImGui::TextUnformatted("No graph parameters are referenced by transitions.");
        } else if (state.playMode || !context.animationPreviewParameters) {
            for (const EditorDockspace::AnimationPreviewState::ParameterInfo& parameter : state.parameters) {
                ImGui::Text("%s: %.3f", parameter.name.c_str(), parameter.value);
            }
        } else {
            for (const EditorDockspace::AnimationPreviewState::ParameterInfo& parameter : state.parameters) {
                float value = parameter.value;
                const auto found = context.animationPreviewParameters->find(parameter.name);
                if (found != context.animationPreviewParameters->end()) {
                    value = found->second;
                }
                if (ImGui::DragFloat(parameter.name.c_str(), &value, 0.01f, -1000.0f, 1000.0f, "%.3f")) {
                    (*context.animationPreviewParameters)[parameter.name] = value;
                }
                ImGui::SameLine();
                std::string buttonLabel = "Toggle##" + parameter.name;
                if (ImGui::Button(buttonLabel.c_str())) {
                    (*context.animationPreviewParameters)[parameter.name] = std::abs(value) > 0.0001f ? 0.0f : 1.0f;
                }
                ImGui::SameLine();
                std::string triggerLabel = "Trigger##" + parameter.name;
                if (ImGui::Button(triggerLabel.c_str())) {
                    (*context.animationPreviewParameters)[parameter.name] = 1.0f;
                }
            }
            if (ImGui::Button("Reset Parameters")) {
                for (const EditorDockspace::AnimationPreviewState::ParameterInfo& parameter : state.parameters) {
                    (*context.animationPreviewParameters)[parameter.name] = 0.0f;
                }
            }
        }
    }

    if (ImGui::CollapsingHeader("Action Profiles", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (state.playMode || !context.scene) {
            ImGui::TextUnformatted("Action profiles are edited in Edit mode.");
        } else if (state.clips.empty()) {
            ImGui::TextUnformatted("No animation clips found.");
        } else {
            std::vector<EditorScene::AnimationActionProfile> edited = state.actionProfiles;
            bool changed = false;
            int removeIndex = -1;

            for (std::size_t i = 0; i < edited.size(); ++i) {
                EditorScene::AnimationActionProfile& profile = edited[i];
                ImGui::PushID(static_cast<int>(i));

                std::array<char, 96> nameBuffer{};
                std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", profile.name.c_str());
                if (ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size())) {
                    profile.name = nameBuffer.data();
                    changed = true;
                }

                profile.clipIndex = std::clamp(profile.clipIndex, 0, static_cast<int>(state.clips.size() - 1));
                const std::string currentClipName = profile.clipName.empty()
                    ? (state.clips[static_cast<std::size_t>(profile.clipIndex)].name.empty()
                        ? ("Clip " + std::to_string(profile.clipIndex))
                        : state.clips[static_cast<std::size_t>(profile.clipIndex)].name)
                    : profile.clipName;
                if (ImGui::BeginCombo("Clip", currentClipName.empty() ? "(unnamed)" : currentClipName.c_str())) {
                    for (std::size_t clip = 0; clip < state.clips.size(); ++clip) {
                        const std::string label = state.clips[clip].name.empty()
                            ? ("Clip " + std::to_string(clip))
                            : state.clips[clip].name;
                        const bool selected = static_cast<int>(clip) == profile.clipIndex;
                        if (ImGui::Selectable(label.c_str(), selected)) {
                            profile.clipIndex = static_cast<int>(clip);
                            profile.clipName = state.clips[clip].name;
                            changed = true;
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                std::array<char, 128> maskBuffer{};
                std::snprintf(maskBuffer.data(), maskBuffer.size(), "%s", profile.maskRootBone.c_str());
                if (ImGui::InputText("Mask Root", maskBuffer.data(), maskBuffer.size())) {
                    profile.maskRootBone = maskBuffer.data();
                    changed = true;
                }
                if (!state.bones.empty()) {
                    const char* currentMask = profile.maskRootBone.empty() ? "Full Body" : profile.maskRootBone.c_str();
                    if (ImGui::BeginCombo("Pick Bone", currentMask)) {
                        if (ImGui::Selectable("Full Body", profile.maskRootBone.empty())) {
                            profile.maskRootBone.clear();
                            changed = true;
                        }
                        for (const EditorDockspace::AnimationPreviewState::BoneInfo& bone : state.bones) {
                            std::string label(static_cast<std::size_t>(std::max(bone.depth, 0)) * 2, ' ');
                            label += bone.name.empty() ? "(unnamed)" : bone.name;
                            const bool selected = bone.name == profile.maskRootBone;
                            if (ImGui::Selectable(label.c_str(), selected)) {
                                profile.maskRootBone = bone.name;
                                changed = true;
                            }
                            if (selected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                }

                if (ImGui::DragFloat("Fade In", &profile.fadeIn, 0.01f, 0.0f, 5.0f, "%.2f s")) {
                    profile.fadeIn = std::max(profile.fadeIn, 0.0f);
                    changed = true;
                }
                if (ImGui::DragFloat("Fade Out", &profile.fadeOut, 0.01f, 0.0f, 5.0f, "%.2f s")) {
                    profile.fadeOut = std::max(profile.fadeOut, 0.0f);
                    changed = true;
                }
                if (ImGui::DragFloat("Speed", &profile.speed, 0.01f, 0.0f, 5.0f, "%.2f")) {
                    profile.speed = std::max(profile.speed, 0.0f);
                    changed = true;
                }

                if (ImGui::Button("Remove Profile")) {
                    removeIndex = static_cast<int>(i);
                }
                ImGui::Separator();
                ImGui::PopID();
            }

            if (removeIndex >= 0) {
                edited.erase(edited.begin() + removeIndex);
                changed = true;
            }

            if (ImGui::Button("Add Profile")) {
                const int clip = context.animationActionClip
                    ? std::clamp(*context.animationActionClip, 0, static_cast<int>(state.clips.size() - 1))
                    : std::clamp(state.defaultClipIndex, 0, static_cast<int>(state.clips.size() - 1));
                edited.push_back(EditorScene::AnimationActionProfile{
                    "ActionProfile",
                    clip,
                    state.clips[static_cast<std::size_t>(clip)].name,
                    context.animationActionMaskRoot ? std::string(context.animationActionMaskRoot) : std::string(),
                    context.animationActionFadeIn ? *context.animationActionFadeIn : 0.08f,
                    context.animationActionFadeOut ? *context.animationActionFadeOut : 0.15f,
                    context.animationActionSpeed ? *context.animationActionSpeed : 1.0f
                });
                changed = true;
            }

            if (changed) {
                context.scene->SetSelectedAnimationActionProfiles(edited);
            }
            if (edited.empty()) {
                ImGui::TextUnformatted("No action profiles authored for this object.");
            }
        }
    }

    if (ImGui::CollapsingHeader("Events", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (state.playMode || !context.scene) {
            ImGui::TextUnformatted("Animation events are edited in Edit mode.");
        } else {
            std::vector<EditorScene::AnimationEvent> edited = state.events;
            bool changed = false;
            int removeIndex = -1;

            for (std::size_t i = 0; i < edited.size(); ++i) {
                EditorScene::AnimationEvent& event = edited[i];
                ImGui::PushID(static_cast<int>(i));

                int clipIndex = event.clipIndex;
                if (ImGui::InputInt("Clip", &clipIndex)) {
                    event.clipIndex = std::max(clipIndex, 0);
                    changed = true;
                }

                float eventTime = event.time;
                if (ImGui::DragFloat("Time", &eventTime, 0.01f, 0.0f, 10000.0f, "%.2f s")) {
                    event.time = std::max(eventTime, 0.0f);
                    changed = true;
                }

                std::array<char, 96> nameBuffer{};
                std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", event.name.c_str());
                if (ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size())) {
                    event.name = nameBuffer.data();
                    changed = true;
                }

                if (ImGui::Button("Remove")) {
                    removeIndex = static_cast<int>(i);
                }
                ImGui::Separator();
                ImGui::PopID();
            }

            if (removeIndex >= 0) {
                edited.erase(edited.begin() + removeIndex);
                changed = true;
            }

            if (ImGui::Button("Add Event")) {
                edited.push_back(EditorScene::AnimationEvent{
                    std::max(state.defaultClipIndex, 0),
                    std::max(state.previewTime, 0.0f),
                    "Event"
                });
                changed = true;
            }

            if (changed) {
                context.scene->SetSelectedAnimationEvents(edited);
            }

            if (edited.empty()) {
                ImGui::TextUnformatted("No events authored for this object.");
            }
        }
    }

    if (ImGui::CollapsingHeader("Runtime", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!state.playMode) {
            ImGui::TextUnformatted("Enter Play mode to inspect live animation state.");
        } else if (!state.runtimeAnimated) {
            ImGui::TextUnformatted("Selected runtime entity has no AnimatedModel component.");
        } else {
            ImGui::Text("State: %s", state.currentState.c_str());
            ImGui::Text("States: %zu", state.stateCount);
            ImGui::Text("Parameter: %.2f", state.parameter);
            ImGui::Text("Current Clip: %d", state.currentClip);
            ImGui::Text("Current Time: %.2f s", state.currentTime);
            if (state.previousClip >= 0) {
                ImGui::Text("Previous Clip: %d", state.previousClip);
                ImGui::Text("Previous Time: %.2f s", state.previousTime);
            }
            ImGui::Text("Blend: %.2f", state.blend);
            ImGui::Text("Pose Bones: %zu", state.poseBones);
        }
    }

    if (ImGui::CollapsingHeader("Clips", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (state.clips.empty()) {
            ImGui::TextUnformatted("No animation clips found.");
        } else {
            for (std::size_t i = 0; i < state.clips.size(); ++i) {
                const auto& clip = state.clips[i];
                ImGui::Text("%zu: %s", i, clip.name.empty() ? "(unnamed)" : clip.name.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("%.2f s", clip.durationSeconds);
            }
        }
    }

    if (ImGui::CollapsingHeader("Skeleton")) {
        if (state.bones.empty()) {
            ImGui::TextUnformatted("No skeleton bones found.");
        } else {
            ImGui::Text("Bones: %zu", state.bones.size());
            ImGui::Separator();
            ImGui::BeginChild("AnimationSkeletonBones", ImVec2(0.0f, 220.0f), true);
            for (std::size_t i = 0; i < state.bones.size(); ++i) {
                const auto& bone = state.bones[i];
                std::string label(static_cast<std::size_t>(std::max(bone.depth, 0)) * 2, ' ');
                label += std::to_string(i);
                label += ": ";
                label += bone.name.empty() ? "(unnamed)" : bone.name;
                if (context.animationActionMaskRoot
                    && context.animationActionMaskRootSize > 0
                    && ImGui::Selectable(label.c_str(), bone.name == context.animationActionMaskRoot)) {
                    std::snprintf(context.animationActionMaskRoot,
                        context.animationActionMaskRootSize,
                        "%s",
                        bone.name.c_str());
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Parent: %d", bone.parent);
                }
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
            object.light ? "[light]" : "",
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
            if (ImGui::MenuItem(object.locked ? "Unlock" : "Lock")) {
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
        if (ImGui::Checkbox("Visible", &visible)) {
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

    if (!selected->modelAssetPath.empty()
        && ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool skeletalModel = selected->skeletalModel;
        int clipIndex = selected->animationClipIndex;
        bool autoplay = selected->animationAutoplay;
        bool loop = selected->animationLoop;
        float speed = selected->animationSpeed;
        bool locomotionEnabled = selected->animationLocomotionEnabled;
        int idleClipIndex = selected->animationIdleClipIndex;
        int walkClipIndex = selected->animationWalkClipIndex;
        int runClipIndex = selected->animationRunClipIndex;
        float walkAt = selected->animationWalkAt;
        float runAt = selected->animationRunAt;
        std::array<char, 96> clipName{};
        std::array<char, 96> idleClipName{};
        std::array<char, 96> walkClipName{};
        std::array<char, 96> runClipName{};
        std::snprintf(clipName.data(), clipName.size(), "%s", selected->animationClipName.c_str());
        std::snprintf(idleClipName.data(), idleClipName.size(), "%s", selected->animationIdleClipName.c_str());
        std::snprintf(walkClipName.data(), walkClipName.size(), "%s", selected->animationWalkClipName.c_str());
        std::snprintf(runClipName.data(), runClipName.size(), "%s", selected->animationRunClipName.c_str());
        const engine::SkinnedModel* inspectedModel = nullptr;

        bool changed = false;
        changed |= ImGui::Checkbox("Skeletal Model", &skeletalModel);
        if (skeletalModel && context.runtimeAssets) {
            std::string error;
            inspectedModel = context.runtimeAssets->LoadSkinnedModel(selected->modelAssetPath, &error);
            if (inspectedModel) {
                const auto& animations = inspectedModel->Animations();
                if (!animations.empty()) {
                    clipIndex = std::clamp(clipIndex, 0, static_cast<int>(animations.size() - 1));
                    const std::string currentName = clipName[0] != '\0'
                        ? std::string(clipName.data())
                        : animations[static_cast<std::size_t>(clipIndex)].name;
                    if (ImGui::BeginCombo("Default Clip", currentName.c_str())) {
                        for (std::size_t i = 0; i < animations.size(); ++i) {
                            const std::string label = animations[i].name.empty()
                                ? ("Clip " + std::to_string(i))
                                : animations[i].name;
                            const bool selectedClip = static_cast<int>(i) == clipIndex;
                            if (ImGui::Selectable(label.c_str(), selectedClip)) {
                                clipIndex = static_cast<int>(i);
                                std::snprintf(clipName.data(), clipName.size(), "%s", animations[i].name.c_str());
                                changed = true;
                            }
                            if (selectedClip) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::Text("Clips: %zu | Bones: %zu", animations.size(), inspectedModel->BoneCount());
                } else {
                    ImGui::TextUnformatted("This skeletal model has no animation clips.");
                    changed |= ImGui::InputInt("Default Clip Index", &clipIndex);
                    changed |= ImGui::InputText("Default Clip Name", clipName.data(), clipName.size());
                }
            } else {
                ImGui::TextWrapped("Could not inspect clips: %s", error.c_str());
                changed |= ImGui::InputInt("Default Clip Index", &clipIndex);
                changed |= ImGui::InputText("Default Clip Name", clipName.data(), clipName.size());
            }
        } else {
            changed |= ImGui::InputInt("Default Clip Index", &clipIndex);
            changed |= ImGui::InputText("Default Clip Name", clipName.data(), clipName.size());
        }
        changed |= ImGui::Checkbox("Autoplay", &autoplay);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Loop", &loop);
        changed |= ImGui::DragFloat("Playback Speed", &speed, 0.05f, 0.0f, 10.0f);

        if (skeletalModel && inspectedModel && !inspectedModel->Animations().empty()
            && ImGui::TreeNodeEx("Locomotion States", ImGuiTreeNodeFlags_DefaultOpen)) {
            const auto& animations = inspectedModel->Animations();
            auto drawClipCombo = [&](const char* label, int& index, std::array<char, 96>& nameBuffer) {
                index = std::clamp(index, 0, static_cast<int>(animations.size() - 1));
                const std::string currentName = nameBuffer[0] != '\0'
                    ? std::string(nameBuffer.data())
                    : animations[static_cast<std::size_t>(index)].name;
                bool comboChanged = false;
                if (ImGui::BeginCombo(label, currentName.c_str())) {
                    for (std::size_t i = 0; i < animations.size(); ++i) {
                        const std::string item = animations[i].name.empty()
                            ? ("Clip " + std::to_string(i))
                            : animations[i].name;
                        const bool selectedClip = static_cast<int>(i) == index;
                        if (ImGui::Selectable(item.c_str(), selectedClip)) {
                            index = static_cast<int>(i);
                            std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", animations[i].name.c_str());
                            comboChanged = true;
                        }
                        if (selectedClip) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                return comboChanged;
            };

            bool locomotionChanged = false;
            locomotionChanged |= ImGui::Checkbox("Use Locomotion", &locomotionEnabled);
            locomotionChanged |= drawClipCombo("Idle", idleClipIndex, idleClipName);
            locomotionChanged |= drawClipCombo("Walk", walkClipIndex, walkClipName);
            locomotionChanged |= drawClipCombo("Run", runClipIndex, runClipName);
            locomotionChanged |= ImGui::DragFloat("Walk At", &walkAt, 0.05f, 0.0f, 100.0f, "%.2f");
            locomotionChanged |= ImGui::DragFloat("Run At", &runAt, 0.05f, 0.0f, 100.0f, "%.2f");
            if (locomotionChanged) {
                context.scene->SetSelectedAnimationLocomotion(locomotionEnabled,
                    idleClipIndex,
                    idleClipName.data(),
                    walkClipIndex,
                    walkClipName.data(),
                    runClipIndex,
                    runClipName.data(),
                    walkAt,
                    runAt);
            }
            ImGui::TreePop();
        }

        if (changed) {
            context.scene->SetSelectedAnimationSettings(skeletalModel,
                clipIndex,
                clipName.data(),
                autoplay,
                loop,
                speed);
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

        // Colliders are world-sized (physics ignores Transform.scale), so a resized
        // object leaves its collider stale. Re-fit the collider's dimensions to the
        // current scale while preserving its shape, material and trigger flag.
        if (selected->colliderEnabled && selectedTransform) {
            if (ImGui::Button("Match Collider to Scale")) {
                engine::ecs::Collider collider = selected->collider;
                const glm::vec3 s = selectedTransform->scale;
                switch (collider.shape) {
                    case engine::ecs::ColliderShape::Box:
                        collider.halfExtents = glm::max(s * 0.5f, glm::vec3(0.001f));
                        break;
                    case engine::ecs::ColliderShape::Sphere:
                        collider.radius = std::max(std::max(s.x, std::max(s.y, s.z)) * 0.5f, 0.001f);
                        break;
                    case engine::ecs::ColliderShape::Capsule:
                        collider.radius = std::max(std::max(s.x, s.z) * 0.5f, 0.001f);
                        collider.halfHeight = std::max(s.y * 0.5f - collider.radius, 0.0f);
                        break;
                    case engine::ecs::ColliderShape::Plane:
                    default:
                        break;   // plane offset tracks position, not scale
                }
                context.scene->SetSelectedCollider(collider);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Re-fit the collider to the object's current scale "
                                  "(physics uses collider size, not Transform scale).");
            }
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
            if (ImGui::Checkbox("Freeze Rotation", &rigidBody.freezeRotation)) {
                if (rigidBody.freezeRotation) {
                    rigidBody.angularVelocity = glm::vec3(0.0f);
                    rigidBody.accumTorque = glm::vec3(0.0f);
                }
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
            const char* shapes[] = {"Sphere", "Box", "Plane", "Capsule"};
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
            } else if (collider.shape == engine::ecs::ColliderShape::Capsule) {
                bool changed = false;
                changed |= ImGui::DragFloat("Radius", &collider.radius, 0.02f, 0.001f, 1000.0f);
                changed |= ImGui::DragFloat("Half Height", &collider.halfHeight, 0.02f, 0.0f, 1000.0f);
                if (changed) {
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

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Physics Joints", ImGuiTreeNodeFlags_DefaultOpen)) {
            static int jointTargetIndex = 0;
            const std::vector<EditorScene::Object>& objects = context.scene->Objects();
            if (jointTargetIndex >= static_cast<int>(objects.size())) {
                jointTargetIndex = 0;
            }

            const char* preview = "None";
            if (jointTargetIndex >= 0 && jointTargetIndex < static_cast<int>(objects.size())) {
                preview = objects[static_cast<std::size_t>(jointTargetIndex)].name.c_str();
            }

            if (ImGui::BeginCombo("Target", preview)) {
                for (std::size_t i = 0; i < objects.size(); ++i) {
                    if (objects[i].name == selected->name) {
                        continue;
                    }
                    const bool isSelected = jointTargetIndex == static_cast<int>(i);
                    if (ImGui::Selectable(objects[i].name.c_str(), isSelected)) {
                        jointTargetIndex = static_cast<int>(i);
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            auto addJoint = [&](EditorScene::PhysicsJoint::Type type, bool rope) {
                if (jointTargetIndex < 0 || jointTargetIndex >= static_cast<int>(objects.size())) {
                    return;
                }
                const EditorScene::Object& target = objects[static_cast<std::size_t>(jointTargetIndex)];
                if (target.name == selected->name) {
                    return;
                }

                EditorScene::PhysicsJoint joint;
                joint.type = type;
                joint.objectA = selected->name;
                joint.objectB = target.name;
                joint.rope = rope;

                const engine::ecs::Transform* selectedJointTransform = context.scene->TryGetTransform(selected->entity);
                const engine::ecs::Transform* targetTransform = context.scene->TryGetTransform(target.entity);
                if (selectedJointTransform && targetTransform) {
                    joint.restLength = glm::length(targetTransform->position - selectedJointTransform->position);
                }
                if (joint.restLength <= 0.001f) {
                    joint.restLength = 1.0f;
                }
                context.scene->AddPhysicsJoint(joint);
            };

            if (ImGui::Button("Add Distance")) {
                addJoint(EditorScene::PhysicsJoint::Type::Distance, false);
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Rope")) {
                addJoint(EditorScene::PhysicsJoint::Type::Distance, true);
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Spring")) {
                addJoint(EditorScene::PhysicsJoint::Type::Spring, false);
            }

            const std::vector<EditorScene::PhysicsJoint>& joints = context.scene->PhysicsJoints();
            int shown = 0;
            for (std::size_t i = 0; i < joints.size(); ++i) {
                const EditorScene::PhysicsJoint& joint = joints[i];
                if (joint.objectA != selected->name && joint.objectB != selected->name) {
                    continue;
                }

                ImGui::PushID(static_cast<int>(i));
                EditorScene::PhysicsJoint edited = joint;
                if (ImGui::Checkbox("Enabled", &edited.enabled)) {
                    context.scene->SetPhysicsJoint(i, edited);
                }
                ImGui::SameLine();
                ImGui::Text("%s: %s <-> %s",
                    PhysicsJointTypeName(joint.type, joint.rope),
                    joint.objectA.c_str(),
                    joint.worldAnchor ? "World" : joint.objectB.c_str());
                if (ImGui::DragFloat("Rest Length", &edited.restLength, 0.05f, 0.001f, 10000.0f)) {
                    context.scene->SetPhysicsJoint(i, edited);
                }
                if (edited.type == EditorScene::PhysicsJoint::Type::Spring) {
                    bool changed = false;
                    changed |= ImGui::DragFloat("Stiffness", &edited.stiffness, 1.0f, 0.0f, 100000.0f);
                    changed |= ImGui::DragFloat("Damping", &edited.damping, 0.05f, 0.0f, 10000.0f);
                    if (changed) {
                        context.scene->SetPhysicsJoint(i, edited);
                    }
                }
                if (ImGui::Button("Delete Joint")) {
                    context.scene->RemovePhysicsJoint(i);
                    ImGui::PopID();
                    break;
                }
                ImGui::Separator();
                ImGui::PopID();
                ++shown;
            }
            if (shown == 0) {
                ImGui::TextUnformatted("No joints connected to this object.");
            }
        }
    }

    if (ImGui::CollapsingHeader("Gameplay Components", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool rotatorEnabled = selected->rotatorEnabled;
        if (ImGui::Checkbox("Rotator", &rotatorEnabled)) {
            context.scene->SetSelectedRotatorEnabled(rotatorEnabled);
        }
        if (rotatorEnabled) {
            engine::ecs::Rotator rotator = selected->rotator;
            bool changed = false;
            changed |= ImGui::DragFloat3("Rotator Axis", &rotator.axis.x, 0.05f);
            changed |= ImGui::DragFloat("Rotator Speed", &rotator.radiansPerSecond, 0.05f, -100.0f, 100.0f);
            if (changed) {
                context.scene->SetSelectedRotator(rotator);
            }
            if (ImGui::Button("Spin Y##Rotator")) {
                rotator.axis = glm::vec3(0.0f, 1.0f, 0.0f);
                rotator.radiansPerSecond = 1.57f;
                context.scene->SetSelectedRotator(rotator);
            }
        }

        bool moverEnabled = selected->moverEnabled;
        if (ImGui::Checkbox("Mover", &moverEnabled)) {
            context.scene->SetSelectedMoverEnabled(moverEnabled);
        }
        if (moverEnabled) {
            engine::ecs::Mover mover = selected->mover;
            bool changed = false;
            changed |= ImGui::DragFloat3("Mover Axis", &mover.axis.x, 0.05f);
            changed |= ImGui::DragFloat("Mover Distance", &mover.distance, 0.05f, 0.0f, 1000.0f);
            changed |= ImGui::DragFloat("Mover Speed", &mover.speed, 0.05f, -100.0f, 100.0f);
            changed |= ImGui::DragFloat("Mover Phase", &mover.phase, 0.05f, -1000.0f, 1000.0f);
            if (changed) {
                mover.initialized = false;
                context.scene->SetSelectedMover(mover);
            }
            if (ImGui::Button("Move X##Mover")) {
                mover.axis = glm::vec3(1.0f, 0.0f, 0.0f);
                mover.distance = 2.0f;
                mover.speed = 1.0f;
                mover.phase = 0.0f;
                mover.initialized = false;
                context.scene->SetSelectedMover(mover);
            }
        }

        bool playerEnabled = selected->playerControllerEnabled;
        if (ImGui::Checkbox("Player Controller", &playerEnabled)) {
            context.scene->SetSelectedPlayerControllerEnabled(playerEnabled);
        }
        if (playerEnabled) {
            EditorScene::PlayerControllerSettings player = selected->playerController;
            bool changed = false;
            changed |= ImGui::Checkbox("First Person", &player.firstPerson);
            changed |= ImGui::DragFloat("Walk Speed", &player.walkSpeed, 0.05f, 0.0f, 100.0f);
            changed |= ImGui::DragFloat("Run Speed", &player.runSpeed, 0.05f, 0.0f, 100.0f);
            changed |= ImGui::DragFloat("Jump Speed", &player.jumpSpeed, 0.05f, 0.0f, 100.0f);
            changed |= ImGui::DragFloat("Look Sensitivity", &player.lookSensitivity, 0.005f, 0.001f, 10.0f);
            changed |= ImGui::DragFloat("Capsule Radius", &player.capsuleRadius, 0.01f, 0.01f, 100.0f);
            changed |= ImGui::DragFloat("Capsule Height", &player.capsuleHeight, 0.01f, 0.02f, 100.0f);
            changed |= ImGui::DragFloat("Eye Height", &player.eyeHeight, 0.01f, 0.0f, 100.0f);
            changed |= ImGui::DragFloat("Camera Distance", &player.cameraDistance, 0.05f, 0.0f, 100.0f);
            changed |= ImGui::DragFloat("Camera Target Height", &player.cameraTargetHeight, 0.01f, 0.0f, 100.0f);
            changed |= ImGui::DragFloat("Max Slope", &player.maxSlopeDegrees, 0.5f, 0.0f, 89.0f);
            changed |= ImGui::DragFloat("Step Height", &player.stepHeight, 0.01f, 0.0f, 10.0f);
            player.capsuleHeight = std::max(player.capsuleHeight, player.capsuleRadius * 2.0f);
            if (changed) {
                context.scene->SetSelectedPlayerController(player);
            }
        }

        bool healthEnabled = selected->healthEnabled;
        if (ImGui::Checkbox("Health", &healthEnabled)) {
            context.scene->SetSelectedHealthEnabled(healthEnabled);
        }
        if (healthEnabled) {
            engine::Health health = selected->health;
            bool changed = false;
            changed |= ImGui::DragFloat("HP", &health.hp, 1.0f, 0.0f, 100000.0f);
            changed |= ImGui::DragFloat("Max HP", &health.maxHp, 1.0f, 1.0f, 100000.0f);
            changed |= ImGui::Checkbox("Alive", &health.alive);
            if (ImGui::Button("Full Health")) {
                health.Reset(std::max(health.maxHp, 1.0f));
                changed = true;
            }
            if (changed) {
                context.scene->SetSelectedHealth(health);
            }
        }
        
        ImGui::Separator();
        if (g_scriptEntity != selected->entity) {
            std::snprintf(g_scriptClassBuffer.data(),
                g_scriptClassBuffer.size(),
                "%s",
                selected->scriptClassName.empty() ? "NewObjectScript" : selected->scriptClassName.c_str());
            g_scriptEntity = selected->entity;
        }

        bool scriptEnabled = selected->scriptEnabled;
        if (ImGui::Checkbox("Script Enabled", &scriptEnabled)) {
            if (!context.scene->SetSelectedScriptEnabled(scriptEnabled) && context.log) {
                context.log->Warning("Script toggle failed: add a script class first or unlock the object");
            }
        }

        ImGui::InputText("Script Class", g_scriptClassBuffer.data(), g_scriptClassBuffer.size());
        ImGui::Text("Script Path: %s", selected->scriptPath.empty() ? "-" : selected->scriptPath.c_str());
        const char* scriptTemplates[] = {"Empty", "Player Movement", "Door Opener", "Pickup", "Damage Zone"};
        ImGui::Combo("Template", &g_scriptTemplateIndex, scriptTemplates, 5);

        if (ImGui::Button("Create Script")) {
            const std::string className = SanitizeScriptClassName(g_scriptClassBuffer.data());
            if (className.empty()) {
                if (context.log) {
                    context.log->Warning("Script create failed: enter a class name");
                }
            } else {
                const ScriptTemplate scriptTemplate = ScriptTemplateFromIndex(g_scriptTemplateIndex);
                std::string scriptPath;
                std::string error;
                if (CreateScriptFiles(context, className, scriptTemplate, &scriptPath, &error)) {
                    context.scene->SetSelectedScript(className, scriptPath, true);
                    context.scene->SetSelectedScriptFields(DefaultFieldsForTemplate(scriptTemplate));
                    std::snprintf(g_scriptClassBuffer.data(), g_scriptClassBuffer.size(), "%s", className.c_str());
                    if (context.assets) {
                        std::string refreshError;
                        context.assets->Refresh(context.assets->RootPath(), &refreshError);
                    }
                    if (context.log) {
                        context.log->Info("Created script: " + scriptPath);
                    }
                } else if (context.log) {
                    context.log->Error("Script create failed: " + error);
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Attach")) {
            const std::string className = SanitizeScriptClassName(g_scriptClassBuffer.data());
            if (className.empty()) {
                if (context.log) {
                    context.log->Warning("Script attach failed: enter a class name");
                }
            } else {
                const std::filesystem::path storedPath = std::filesystem::path("Game") / "Scripts" / (className + ".cpp");
                if (context.scene->SetSelectedScript(className, storedPath.string(), true)) {
                    if (context.log) {
                        context.log->Info("Attached script: " + className);
                    }
                } else if (context.log) {
                    context.log->Warning("Script attach failed: selected object is locked");
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove")) {
            if (context.scene->SetSelectedScript(std::string(), std::string(), false)) {
                std::snprintf(g_scriptClassBuffer.data(), g_scriptClassBuffer.size(), "%s", "NewObjectScript");
                if (context.log) {
                    context.log->Info("Removed script from selected object");
                }
            } else if (context.log) {
                context.log->Warning("Script remove failed: selected object is locked");
            }
        }

        if (ImGui::Button("Add Field")) {
            if (!context.scene->AddSelectedScriptField() && context.log) {
                context.log->Warning("Script field add failed: selected object is locked");
            }
        }

        for (std::size_t i = 0; i < selected->scriptFields.size(); ++i) {
            const EditorScene::ScriptField& sourceField = selected->scriptFields[i];
            EditorScene::ScriptField field = sourceField;
            ImGui::PushID(static_cast<int>(i));
            ImGui::Separator();

            std::array<char, 64> nameBuffer{};
            std::array<char, 128> valueBuffer{};
            std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", field.name.c_str());
            std::snprintf(valueBuffer.data(), valueBuffer.size(), "%s", field.value.c_str());

            bool changed = false;
            if (ImGui::InputText("Field", nameBuffer.data(), nameBuffer.size())) {
                field.name = SanitizeScriptClassName(nameBuffer.data());
                changed = true;
            }

            int typeIndex = ScriptFieldTypeIndex(field.type);
            const char* fieldTypes[] = {"Float", "Int", "Bool", "String"};
            if (ImGui::Combo("Type", &typeIndex, fieldTypes, 4)) {
                field.type = ScriptFieldTypeFromIndex(typeIndex);
                if (field.type == EditorScene::ScriptField::Type::Bool
                    && field.value != "0"
                    && field.value != "1") {
                    field.value = "0";
                }
                changed = true;
            }

            if (field.type == EditorScene::ScriptField::Type::Bool) {
                bool value = field.value == "1" || field.value == "true" || field.value == "True";
                if (ImGui::Checkbox("Value", &value)) {
                    field.value = value ? "1" : "0";
                    changed = true;
                }
            } else if (ImGui::InputText("Value", valueBuffer.data(), valueBuffer.size())) {
                field.value = valueBuffer.data();
                if (field.value.empty()) {
                    field.value = field.type == EditorScene::ScriptField::Type::String ? "-" : "0";
                }
                changed = true;
            }

            if (changed) {
                context.scene->SetSelectedScriptField(i, field);
            }

            if (ImGui::Button("Remove Field")) {
                context.scene->RemoveSelectedScriptField(i);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }

        if (selected->colliderEnabled && selected->collider.isTrigger) {
            ImGui::Separator();
            std::string targetName = selected->triggerTargetName;
            EditorScene::TriggerActionMode enterMoverAction = selected->triggerEnterMoverAction;
            EditorScene::TriggerActionMode enterRotatorAction = selected->triggerEnterRotatorAction;
            EditorScene::TriggerActionMode exitMoverAction = selected->triggerExitMoverAction;
            EditorScene::TriggerActionMode exitRotatorAction = selected->triggerExitRotatorAction;
            const char* preview = targetName.empty() ? "None" : targetName.c_str();
            if (ImGui::BeginCombo("Trigger Target", preview)) {
                if (ImGui::Selectable("None", targetName.empty())) {
                    targetName.clear();
                    context.scene->SetSelectedTriggerAction(targetName,
                        enterMoverAction,
                        enterRotatorAction,
                        exitMoverAction,
                        exitRotatorAction);
                }
                for (const EditorScene::Object& object : context.scene->Objects()) {
                    if (object.name == selected->name) {
                        continue;
                    }
                    const bool isSelectedTarget = object.name == targetName;
                    if (ImGui::Selectable(object.name.c_str(), isSelectedTarget)) {
                        targetName = object.name;
                        context.scene->SetSelectedTriggerAction(targetName,
                            enterMoverAction,
                            enterRotatorAction,
                            exitMoverAction,
                            exitRotatorAction);
                    }
                    if (isSelectedTarget) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            bool changed = false;
            changed |= DrawTriggerActionModeCombo("Enter Mover", &enterMoverAction);
            changed |= DrawTriggerActionModeCombo("Enter Rotator", &enterRotatorAction);
            changed |= DrawTriggerActionModeCombo("Exit Mover", &exitMoverAction);
            changed |= DrawTriggerActionModeCombo("Exit Rotator", &exitRotatorAction);
            if (changed) {
                context.scene->SetSelectedTriggerAction(targetName,
                    enterMoverAction,
                    enterRotatorAction,
                    exitMoverAction,
                    exitRotatorAction);
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
    ImGui::Text("%d files", static_cast<int>(context.assets->TotalFileCount()));
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
    for (int i = 0; i < static_cast<int>(assets.size()); ++i) {
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

bool EditorDockspace::Draw(Context& context) {
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
            if (ImGui::BeginMenu("Gameplay")) {
                if (ImGui::MenuItem("Player Start")) {
                    context.addPlayerStartRequested = true;
                }
                if (ImGui::MenuItem("Door")) {
                    context.addDoorRequested = true;
                }
                if (ImGui::MenuItem("Pickup")) {
                    context.addPickupRequested = true;
                }
                if (ImGui::MenuItem("Damage Zone")) {
                    context.addDamageZoneRequested = true;
                }
                if (ImGui::MenuItem("Moving Platform")) {
                    context.addMovingPlatformRequested = true;
                }
                if (ImGui::MenuItem("Trigger Mover Test")) {
                    context.addTriggerMoverTestRequested = true;
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
            break;
        case EditorPanels::Panel::GameplayDebug:
            DrawGameplayDebug(context, &open);
            break;
        case EditorPanels::Panel::AnimationPreview:
            DrawAnimationPreview(context, &open);
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

bool EditorDockspace::IsCompiledWithImGui() const {
    return true;
}
