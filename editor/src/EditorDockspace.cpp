#include "EditorDockspace.h"
#include "ParticlePresets.h"
#include "ParticleAsset.h"

#include <engine/ecs/Components.h>
#include <engine/audio/AudioEditing.h>
#include <engine/graphics/Camera.h>
#include <engine/graphics/SkinnedModel.h>
#include <engine/assets/ShaderAsset.h>
#include <engine/physics/PhysicsComponents.h>

#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
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
std::array<char, 96> g_componentSearch{};
bool g_componentPopupOpenRequested = false;
int g_cameraPresetSelection = -1;
std::array<char, 128> g_cameraPresetName{};
int g_cameraPresetNameSelection = -1;
int g_cameraSequenceSelection = -1;
int g_cameraSequenceShotSelection = -1;
int g_cameraSequenceCueSelection = -1;
int g_cameraSequenceNameSelection = -1;
std::array<char, 128> g_cameraSequenceName{};

struct AudioEditorState {
    engine::AudioBuffer buffer;
    engine::AudioBuffer original;
    engine::AudioBuffer undo;
    engine::AudioBuffer clipboard;
    std::vector<engine::AudioBuffer> undoStack;
    std::vector<engine::AudioBuffer> redoStack;
    std::string sourcePath;
    std::array<char, 320> outputPath{};
    float selectionStart = 0.0f;
    float selectionEnd = 0.0f;
    float gainDb = 0.0f;
    float fadeInSeconds = 0.05f;
    float fadeOutSeconds = 0.10f;
    int generator = 0;
    float generatorFrequency = 440.0f;
    float generatorDuration = 1.0f;
    float generatorAmplitude = 0.5f;
    bool dirty = false;
    float waveformZoom = 1.0f;
    float waveformOffset = 0.0f;
    engine::AudioCueAsset cue;
    std::array<char, 320> cuePath{};
    int selectedCueClip = -1;
    engine::AdaptiveMusicAsset music;
    std::array<char, 320> musicPath{};
    int selectedMusicState = -1;
};

AudioEditorState g_audioEditor;

enum class AddableComponent {
    LinearVelocity,
    AngularVelocity,
    RigidBody,
    Collider,
    Rotator,
    Mover,
    PlayerController,
    CameraZone,
    Health,
    NavAgent,
    Script,
    AudioSource,
    ParticleSystem
};

struct ComponentCatalogEntry {
    const char* name;
    const char* category;
    const char* description;
    AddableComponent component;
};

constexpr ComponentCatalogEntry kComponentCatalog[] = {
    {"Linear Velocity", "Motion", "Constant directional movement", AddableComponent::LinearVelocity},
    {"Angular Velocity", "Motion", "Constant rotation around an axis", AddableComponent::AngularVelocity},
    {"Rigid Body", "Physics", "Mass, gravity, velocity, and simulation", AddableComponent::RigidBody},
    {"Collider", "Physics", "Collision shape fitted to this object", AddableComponent::Collider},
    {"Rotator", "Gameplay", "Continuous configurable rotation", AddableComponent::Rotator},
    {"Mover", "Gameplay", "Movement along an axis", AddableComponent::Mover},
    {"Player Controller", "Gameplay", "Player movement and camera controls", AddableComponent::PlayerController},
    {"Camera Zone", "Gameplay", "Blend to a saved camera inside a trigger volume", AddableComponent::CameraZone},
    {"Health", "Gameplay", "Hit points and alive state", AddableComponent::Health},
    {"Navigation Agent", "AI", "Patrol, perception, and chase behavior", AddableComponent::NavAgent},
    {"Script", "Scripting", "Custom object behavior and exposed fields", AddableComponent::Script},
    {"Audio Source", "Audio", "2D or spatial sound playback", AddableComponent::AudioSource},
    {"Particle System", "Effects", "Animated billboard particle emitter", AddableComponent::ParticleSystem},
};

struct PrimitiveCreatorState {
    EditorScene::Primitive type = EditorScene::Primitive::Cube;
    std::array<char, 128> name{};
    glm::vec3 position{0.0f, 0.5f, 0.0f};
    glm::vec3 rotationDegrees{0.0f};
    glm::vec3 dimensions{1.0f};
    float radius = 0.5f;
    float height = 1.0f;
    int staircaseSteps = 6;
    bool addCollider = true;
    bool frameAfterCreate = true;
    bool openRequested = false;
};

PrimitiveCreatorState g_primitiveCreator;
bool g_creationPaletteOpenRequested = false;
std::array<char, 96> g_creationSearch{};

const char* PrimitiveName(EditorScene::Primitive primitive) {
    if (primitive == EditorScene::Primitive::Plane) {
        return "Plane";
    }
    if (primitive == EditorScene::Primitive::Sphere) {
        return "Sphere";
    }
    if (primitive == EditorScene::Primitive::Capsule) {
        return "Capsule";
    }
    if (primitive == EditorScene::Primitive::Cylinder) {
        return "Cylinder";
    }
    if (primitive == EditorScene::Primitive::Cone) {
        return "Cone";
    }
    if (primitive == EditorScene::Primitive::Pyramid) return "Pyramid";
    if (primitive == EditorScene::Primitive::Torus) return "Torus";
    if (primitive == EditorScene::Primitive::Staircase) return "Staircase";
    return "Cube";
}

void ResetPrimitiveCreator(EditorScene::Primitive type) {
    g_primitiveCreator.type = type;
    g_primitiveCreator.name.fill('\0');
    g_primitiveCreator.rotationDegrees = glm::vec3(0.0f);
    g_primitiveCreator.addCollider = true;
    g_primitiveCreator.dimensions = glm::vec3(1.0f);
    g_primitiveCreator.radius = type == EditorScene::Primitive::Capsule ? 0.4f : 0.5f;
    g_primitiveCreator.height = type == EditorScene::Primitive::Capsule ? 1.8f : 1.0f;
    g_primitiveCreator.staircaseSteps = 6;
    if (type == EditorScene::Primitive::Torus) {
        g_primitiveCreator.radius = 0.5f;
        g_primitiveCreator.height = 0.3f;
    }
    if (type == EditorScene::Primitive::Staircase) {
        g_primitiveCreator.dimensions = glm::vec3(2.0f, 1.0f, 3.0f);
    }
    if (type == EditorScene::Primitive::Plane) {
        g_primitiveCreator.position = glm::vec3(0.0f);
        g_primitiveCreator.dimensions = glm::vec3(3.0f, 1.0f, 3.0f);
        g_primitiveCreator.addCollider = false;
    } else {
        g_primitiveCreator.position = glm::vec3(0.0f, g_primitiveCreator.height * 0.5f, 0.0f);
    }
    g_primitiveCreator.openRequested = true;
}

bool MatchesCreationSearch(const char* label, const char* category) {
    std::string query(g_creationSearch.data());
    if (query.empty()) return true;
    std::string haystack = std::string(category) + " " + label;
    std::transform(query.begin(), query.end(), query.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(haystack.begin(), haystack.end(), haystack.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return haystack.find(query) != std::string::npos;
}

float PrimitiveGroundHeight() {
    switch (g_primitiveCreator.type) {
    case EditorScene::Primitive::Plane: return 0.0f;
    case EditorScene::Primitive::Sphere: return g_primitiveCreator.radius;
    case EditorScene::Primitive::Capsule:
    case EditorScene::Primitive::Cylinder:
    case EditorScene::Primitive::Cone: return g_primitiveCreator.height * 0.5f;
    case EditorScene::Primitive::Torus: return g_primitiveCreator.height * 0.5f;
    case EditorScene::Primitive::Cube:
    case EditorScene::Primitive::Pyramid:
    case EditorScene::Primitive::Staircase: return g_primitiveCreator.dimensions.y * 0.5f;
    }
    return 0.0f;
}

const char* ObjectTypeName(const EditorScene::Object& object) {
    if (object.navMeshBoundsVolume) {
        return "Nav Mesh Bounds Volume";
    }
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
    case engine::ecs::ColliderShape::Capsule: return 3;
    case engine::ecs::ColliderShape::Cylinder: return 4;
    case engine::ecs::ColliderShape::Cone: return 5;
    case engine::ecs::ColliderShape::Pyramid: return 6;
    case engine::ecs::ColliderShape::Torus: return 7;
    case engine::ecs::ColliderShape::Staircase: return 8;
    }
    return 0;
}

engine::ecs::ColliderShape ColliderShapeFromIndex(int index) {
    switch (index) {
    case 1: return engine::ecs::ColliderShape::Box;
    case 2: return engine::ecs::ColliderShape::Plane;
    case 3: return engine::ecs::ColliderShape::Capsule;
    case 4: return engine::ecs::ColliderShape::Cylinder;
    case 5: return engine::ecs::ColliderShape::Cone;
    case 6: return engine::ecs::ColliderShape::Pyramid;
    case 7: return engine::ecs::ColliderShape::Torus;
    case 8: return engine::ecs::ColliderShape::Staircase;
    default: return engine::ecs::ColliderShape::Sphere;
    }
}

const char* ColliderShapeName(engine::ecs::ColliderShape shape) {
    switch (shape) {
    case engine::ecs::ColliderShape::Sphere: return "Sphere";
    case engine::ecs::ColliderShape::Box: return "Box";
    case engine::ecs::ColliderShape::Plane: return "Plane";
    case engine::ecs::ColliderShape::Capsule: return "Capsule";
    case engine::ecs::ColliderShape::Cylinder: return "Cylinder";
    case engine::ecs::ColliderShape::Cone: return "Cone";
    case engine::ecs::ColliderShape::Pyramid: return "Pyramid";
    case engine::ecs::ColliderShape::Torus: return "Torus";
    case engine::ecs::ColliderShape::Staircase: return "Staircase";
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

const char* CameraSequenceTriggerActionName(
    EditorScene::CameraSequenceTriggerAction action) {
    switch (action) {
    case EditorScene::CameraSequenceTriggerAction::Play: return "Play";
    case EditorScene::CameraSequenceTriggerAction::Stop: return "Stop";
    case EditorScene::CameraSequenceTriggerAction::Skip: return "Skip";
    case EditorScene::CameraSequenceTriggerAction::None:
    default: return "None";
    }
}

bool DrawCameraSequenceTriggerActionCombo(
    const char* label, EditorScene::CameraSequenceTriggerAction* action) {
    bool changed = false;
    if (ImGui::BeginCombo(label, CameraSequenceTriggerActionName(*action))) {
        const EditorScene::CameraSequenceTriggerAction actions[] = {
            EditorScene::CameraSequenceTriggerAction::None,
            EditorScene::CameraSequenceTriggerAction::Play,
            EditorScene::CameraSequenceTriggerAction::Stop,
            EditorScene::CameraSequenceTriggerAction::Skip,
        };
        for (const auto candidate : actions) {
            const bool selected = candidate == *action;
            if (ImGui::Selectable(CameraSequenceTriggerActionName(candidate), selected)) {
                *action = candidate;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

const char* AudioActionName(engine::ecs::AudioAction action) {
    switch (action) {
    case engine::ecs::AudioAction::None: return "None";
    case engine::ecs::AudioAction::Play: return "Play";
    case engine::ecs::AudioAction::Restart: return "Restart";
    case engine::ecs::AudioAction::Pause: return "Pause";
    case engine::ecs::AudioAction::Resume: return "Resume";
    case engine::ecs::AudioAction::Stop: return "Stop";
    }
    return "None";
}

const char* ParticleActionName(engine::ParticleAction action) {
    switch (action) {
    case engine::ParticleAction::None: return "None";
    case engine::ParticleAction::Play: return "Play";
    case engine::ParticleAction::Restart: return "Restart";
    case engine::ParticleAction::Stop: return "Stop";
    case engine::ParticleAction::Burst: return "Burst";
    case engine::ParticleAction::Clear: return "Clear";
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

bool DrawAudioActionCombo(const char* label, engine::ecs::AudioAction* action) {
    bool changed = false;
    if (ImGui::BeginCombo(label, AudioActionName(*action))) {
        const engine::ecs::AudioAction actions[] = {
            engine::ecs::AudioAction::None,
            engine::ecs::AudioAction::Play,
            engine::ecs::AudioAction::Restart,
            engine::ecs::AudioAction::Pause,
            engine::ecs::AudioAction::Resume,
            engine::ecs::AudioAction::Stop
        };
        for (const engine::ecs::AudioAction candidate : actions) {
            const bool selected = candidate == *action;
            if (ImGui::Selectable(AudioActionName(candidate), selected)) {
                *action = candidate;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool DrawParticleActionCombo(const char* label, engine::ParticleAction* action) {
    bool changed = false;
    if (ImGui::BeginCombo(label, ParticleActionName(*action))) {
        const engine::ParticleAction actions[] = {
            engine::ParticleAction::None, engine::ParticleAction::Play,
            engine::ParticleAction::Restart, engine::ParticleAction::Stop,
            engine::ParticleAction::Burst, engine::ParticleAction::Clear
        };
        for (const engine::ParticleAction candidate : actions) {
            const bool selected = candidate == *action;
            if (ImGui::Selectable(ParticleActionName(candidate), selected)) {
                *action = candidate;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
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
            << "Use `OnCreate()` for one-time setup when the object enters Play mode. Use `OnUpdate(float dt)` for fixed-step behavior. Call `Register" << className << "Script()` from your game startup before entering Play mode so `ScriptRegistry` can construct the script from its class name. Common helpers include `GetFieldFloat()`, `GetFieldInt()`, `GetFieldBool()`, `GetFieldString()`, `Self()`, `Transform()`, `FindObject()`, `FindTransform()`, `IsKeyDown()`, `WasKeyPressed()`, `PlayAudio()`, `StopAudio()`, `PlayParticles()`, `StopParticles()`, `RestartParticles()`, `BurstParticles()`, `SetParticleRate()`, `ShakeCamera()`, `PlayCameraSequence()`, `WasCameraSequenceEvent()`, `WasCameraSequenceFinished()`, and `DestroySelf()`. Pass an entity returned by `FindObject()` to control another object's Audio Source or Particle System.\n";
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
    std::size_t capsules = 0;
    std::size_t cylinders = 0;
    std::size_t cones = 0;
    std::size_t pyramids = 0;
    std::size_t toruses = 0;
    std::size_t staircases = 0;
    std::size_t dynamicBodiesWithoutCollider = 0;
    std::size_t invalidColliders = 0;
    std::size_t joints = 0;
};

bool ColliderIsInvalid(const engine::ecs::Collider& collider) {
    switch (collider.shape) {
    case engine::ecs::ColliderShape::Sphere:
        return collider.radius <= 0.0f;
    case engine::ecs::ColliderShape::Box:
    case engine::ecs::ColliderShape::Pyramid:
    case engine::ecs::ColliderShape::Staircase:
        return collider.halfExtents.x <= 0.0f
            || collider.halfExtents.y <= 0.0f
            || collider.halfExtents.z <= 0.0f;
    case engine::ecs::ColliderShape::Plane:
        return collider.planeNormal.x == 0.0f
            && collider.planeNormal.y == 0.0f
            && collider.planeNormal.z == 0.0f;
    case engine::ecs::ColliderShape::Capsule:
        return collider.radius <= 0.0f || collider.halfHeight < 0.0f;
    case engine::ecs::ColliderShape::Cylinder:
    case engine::ecs::ColliderShape::Cone:
        return collider.radius <= 0.0f || collider.halfHeight <= 0.0f;
    case engine::ecs::ColliderShape::Torus:
        return collider.majorRadius <= 0.0f || collider.minorRadius <= 0.0f;
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
        case engine::ecs::ColliderShape::Capsule: ++status.capsules; break;
        case engine::ecs::ColliderShape::Cylinder: ++status.cylinders; break;
        case engine::ecs::ColliderShape::Cone: ++status.cones; break;
        case engine::ecs::ColliderShape::Pyramid: ++status.pyramids; break;
        case engine::ecs::ColliderShape::Torus: ++status.toruses; break;
        case engine::ecs::ColliderShape::Staircase: ++status.staircases; break;
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

    if (object.primitive == EditorScene::Primitive::Sphere && object.modelAssetPath.empty()) {
        engine::ecs::Collider collider = engine::ecs::Collider::MakeSphere(0.5f);
        collider.restitution = kDefaultRestitution;
        return collider;
    }

    if (object.primitive == EditorScene::Primitive::Capsule && object.modelAssetPath.empty()) {
        engine::ecs::Collider collider = engine::ecs::Collider::MakeCapsuleFromHeight(0.4f, 1.8f);
        collider.restitution = kDefaultRestitution;
        return collider;
    }

    if (object.primitive == EditorScene::Primitive::Cylinder && object.modelAssetPath.empty()) {
        const float radius = transform ? 0.5f * std::max(transform->scale.x, transform->scale.z) : 0.5f;
        const float height = transform ? transform->scale.y : 1.0f;
        engine::ecs::Collider collider = engine::ecs::Collider::MakeCylinder(radius, height);
        collider.restitution = kDefaultRestitution;
        return collider;
    }

    if (object.modelAssetPath.empty() && transform) {
        if (object.primitive == EditorScene::Primitive::Cone)
            return engine::ecs::Collider::MakeCone(
                0.5f * std::max(transform->scale.x, transform->scale.z), transform->scale.y);
        if (object.primitive == EditorScene::Primitive::Pyramid)
            return engine::ecs::Collider::MakePyramid(transform->scale * 0.5f);
        if (object.primitive == EditorScene::Primitive::Torus) {
            const float radialScale = std::max(transform->scale.x, transform->scale.z);
            return engine::ecs::Collider::MakeTorus(0.35f * radialScale,
                0.15f * std::max(radialScale, transform->scale.y));
        }
        if (object.primitive == EditorScene::Primitive::Staircase)
            return engine::ecs::Collider::MakeStaircase(transform->scale * 0.5f, 6);
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

bool HasComponent(const EditorScene::Object& object, AddableComponent component) {
    switch (component) {
    case AddableComponent::LinearVelocity: return object.linearVelocityEnabled;
    case AddableComponent::AngularVelocity: return object.angularVelocityEnabled;
    case AddableComponent::RigidBody: return object.rigidBodyEnabled;
    case AddableComponent::Collider: return object.colliderEnabled;
    case AddableComponent::Rotator: return object.rotatorEnabled;
    case AddableComponent::Mover: return object.moverEnabled;
    case AddableComponent::PlayerController: return object.playerControllerEnabled;
    case AddableComponent::CameraZone: return object.cameraZoneEnabled;
    case AddableComponent::Health: return object.healthEnabled;
    case AddableComponent::NavAgent: return object.navAgentEnabled;
    case AddableComponent::Script:
        return object.scriptEnabled || !object.scriptClassName.empty() || !object.scriptPath.empty();
    case AddableComponent::AudioSource: return object.audioSourceEnabled;
    case AddableComponent::ParticleSystem: return object.particleSystemEnabled;
    }
    return false;
}

bool ComponentMatchesSearch(const ComponentCatalogEntry& entry) {
    std::string query(g_componentSearch.data());
    if (query.empty()) return true;
    std::string haystack = std::string(entry.category) + " " + entry.name + " " + entry.description;
    std::transform(query.begin(), query.end(), query.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(haystack.begin(), haystack.end(), haystack.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return haystack.find(query) != std::string::npos;
}

bool AddComponent(EditorDockspace::Context& context, AddableComponent component) {
    if (!context.scene) return false;
    const EditorScene::Object* selected = context.scene->SelectedObject();
    if (!selected || selected->locked || HasComponent(*selected, component)) return false;

    bool added = false;
    switch (component) {
    case AddableComponent::LinearVelocity:
        added = context.scene->SetSelectedLinearVelocityEnabled(true);
        break;
    case AddableComponent::AngularVelocity:
        added = context.scene->SetSelectedAngularVelocityEnabled(true);
        break;
    case AddableComponent::RigidBody:
        added = context.scene->SetSelectedRigidBody(engine::ecs::RigidBody::Dynamic(1.0f));
        break;
    case AddableComponent::Collider: {
        const engine::ecs::Transform* transform = context.scene->TryGetTransform(selected->entity);
        added = context.scene->SetSelectedCollider(DefaultColliderForObject(*selected, transform));
        break;
    }
    case AddableComponent::Rotator:
        added = context.scene->SetSelectedRotatorEnabled(true);
        break;
    case AddableComponent::Mover:
        added = context.scene->SetSelectedMoverEnabled(true);
        break;
    case AddableComponent::PlayerController:
        added = context.scene->SetSelectedPlayerControllerEnabled(true);
        break;
    case AddableComponent::CameraZone: {
        const std::string preset = context.scene->CameraPresets().empty()
            ? std::string()
            : context.scene->CameraPresets().front().name;
        added = context.scene->SetSelectedCameraZone(true, preset, true, 0, 0.35f);
        break;
    }
    case AddableComponent::Health:
        added = context.scene->SetSelectedHealthEnabled(true);
        break;
    case AddableComponent::NavAgent:
        added = context.scene->SetSelectedNavAgent(true, 3.0f, 20.0f, 0.6f, 0.3f,
            std::string(), 12.0f, 45.0f);
        break;
    case AddableComponent::Script:
        added = context.scene->SetSelectedScript("NewObjectScript", std::string(), false);
        break;
    case AddableComponent::AudioSource:
        added = context.scene->SetSelectedAudioSource(true, std::string(), 1.0f, 1.0f,
            true, false, false, 1.0f, 40.0f, 1.0f);
        break;
    case AddableComponent::ParticleSystem: {
        engine::ParticleSystemComponent settings;
        added = context.scene->SetSelectedParticleSystem(true, settings);
        break;
    }
    }
    if (added && context.log) {
        for (const ComponentCatalogEntry& entry : kComponentCatalog) {
            if (entry.component == component) {
                context.log->Info(std::string("Added ") + entry.name + " component to " + selected->name);
                break;
            }
        }
    }
    return added;
}

void DrawComponentMenuItems(EditorDockspace::Context& context) {
    const EditorScene::Object* selected = context.scene ? context.scene->SelectedObject() : nullptr;
    const bool locked = !selected || selected->locked;
    const char* currentCategory = nullptr;
    for (const ComponentCatalogEntry& entry : kComponentCatalog) {
        if (!currentCategory || std::string(currentCategory) != entry.category) {
            if (currentCategory) ImGui::Separator();
            currentCategory = entry.category;
            ImGui::TextDisabled("%s", currentCategory);
        }
        const bool attached = selected && HasComponent(*selected, entry.component);
        const std::string label = attached ? std::string(entry.name) + " (Added)" : entry.name;
        if (ImGui::MenuItem(label.c_str(), nullptr, false, !locked && !attached)) {
            AddComponent(context, entry.component);
        }
    }
}

void DrawAddComponentPopup(EditorDockspace::Context& context) {
    if (g_componentPopupOpenRequested) {
        g_componentSearch.fill('\0');
        ImGui::OpenPopup("Add Component");
        g_componentPopupOpenRequested = false;
    }
    ImGui::SetNextWindowSize(ImVec2(455.0f, 390.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopup("Add Component")) return;

    const EditorScene::Object* selected = context.scene ? context.scene->SelectedObject() : nullptr;
    ImGui::Text("Add Component to %s", selected ? selected->name.c_str() : "selection");
    if (selected && selected->locked) ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "Unlock this object to add components.");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##ComponentSearch", "Search components...",
        g_componentSearch.data(), g_componentSearch.size());
    ImGui::Separator();

    bool close = false;
    ImGui::BeginChild("##ComponentResults", ImVec2(0.0f, 0.0f), false);
    const char* currentCategory = nullptr;
    for (const ComponentCatalogEntry& entry : kComponentCatalog) {
        if (!ComponentMatchesSearch(entry)) continue;
        if (!currentCategory || std::string(currentCategory) != entry.category) {
            currentCategory = entry.category;
            ImGui::Spacing();
            ImGui::TextDisabled("%s", currentCategory);
        }
        const bool attached = selected && HasComponent(*selected, entry.component);
        const bool disabled = !selected || selected->locked || attached;
        ImGui::PushID(static_cast<int>(entry.component));
        if (disabled) ImGui::BeginDisabled();
        const std::string label = attached ? std::string(entry.name) + "   Added" : entry.name;
        if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(0.0f, 38.0f))) {
            close = AddComponent(context, entry.component);
        }
        if (disabled) ImGui::EndDisabled();
        ImGui::SameLine(190.0f);
        ImGui::TextDisabled("%s", entry.description);
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (close) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
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

std::vector<EditorAssets::Asset> FindAudioAssets(const EditorAssets& assets) {
    std::vector<EditorAssets::Asset> audio;
    std::error_code ec;
    const std::filesystem::path root = assets.RootPath();
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) return audio;
    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        std::string extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension != ".wav" && extension != ".flac" && extension != ".mp3" && extension != ".ogg") continue;
        EditorAssets::Asset asset;
        asset.relativePath = std::filesystem::relative(entry.path(), root, ec).string();
        if (ec) continue;
        std::replace(asset.relativePath.begin(), asset.relativePath.end(), '\\', '/');
        asset.displayName = entry.path().filename().string();
        asset.type = EditorAssets::Type::Audio;
        audio.push_back(std::move(asset));
    }
    std::sort(audio.begin(), audio.end(), [](const auto& a, const auto& b) {
        return a.relativePath < b.relativePath;
    });
    return audio;
}

bool IsEditableAudioPath(const std::string& path) {
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".wav" || extension == ".flac" || extension == ".mp3" || extension == ".ogg";
}

void SetAudioEditorOutputPath(const std::string& source) {
    const std::filesystem::path input(source);
    const std::filesystem::path output = input.parent_path() / (input.stem().string() + "_edited.wav");
    std::snprintf(g_audioEditor.outputPath.data(), g_audioEditor.outputPath.size(), "%s", output.string().c_str());
}

bool LoadAudioIntoEditor(const std::string& path, EditorLog* log) {
    engine::AudioBuffer decoded;
    std::string error;
    if (!engine::DecodeAudioFile(path, &decoded, &error)) {
        if (log) log->Error("Audio Editor: " + error + " " + path);
        return false;
    }
    g_audioEditor.buffer = std::move(decoded);
    g_audioEditor.original = g_audioEditor.buffer;
    g_audioEditor.undo = {};
    g_audioEditor.undoStack.clear();
    g_audioEditor.redoStack.clear();
    g_audioEditor.sourcePath = path;
    g_audioEditor.selectionStart = 0.0f;
    g_audioEditor.selectionEnd = g_audioEditor.buffer.DurationSeconds();
    g_audioEditor.dirty = false;
    SetAudioEditorOutputPath(path);
    if (log) log->Info("Audio Editor loaded: " + path);
    return true;
}

void PushAudioEditUndo() {
    g_audioEditor.undo = g_audioEditor.buffer;
    g_audioEditor.undoStack.push_back(g_audioEditor.buffer);
    if (g_audioEditor.undoStack.size() > 32) g_audioEditor.undoStack.erase(g_audioEditor.undoStack.begin());
    g_audioEditor.redoStack.clear();
    g_audioEditor.dirty = true;
}

std::pair<std::size_t, std::size_t> AudioSelectionFrames() {
    const engine::AudioBuffer& buffer = g_audioEditor.buffer;
    const std::size_t frames = buffer.FrameCount();
    const std::size_t begin = std::min(frames, static_cast<std::size_t>(
        std::max(g_audioEditor.selectionStart, 0.0f) * buffer.sampleRate));
    const std::size_t end = std::clamp(static_cast<std::size_t>(
        std::max(g_audioEditor.selectionEnd, 0.0f) * buffer.sampleRate), begin, frames);
    return {begin, end};
}

void DrawAudioWaveform(const engine::AudioBuffer& buffer) {
    const ImVec2 size(ImGui::GetContentRegionAvail().x, 190.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(15, 18, 24, 255));
    draw->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(65, 78, 96, 255));
    ImGui::InvisibleButton("##AudioWaveform", size);
    if (buffer.Empty() || size.x < 2.0f) return;

    const float duration = buffer.DurationSeconds();
    const float visibleDuration = duration / std::max(g_audioEditor.waveformZoom, 1.0f);
    const float visibleStart = std::clamp(g_audioEditor.waveformOffset, 0.0f,
        std::max(duration - visibleDuration, 0.0f));
    const float visibleEnd = visibleStart + visibleDuration;
    const auto timeToX = [&](float time) {
        return origin.x + size.x * ((time - visibleStart) / std::max(visibleDuration, 0.0001f));
    };
    const float selectedX0 = std::clamp(timeToX(g_audioEditor.selectionStart), origin.x, origin.x + size.x);
    const float selectedX1 = std::clamp(timeToX(g_audioEditor.selectionEnd), origin.x, origin.x + size.x);
    draw->AddRectFilled(ImVec2(selectedX0, origin.y), ImVec2(selectedX1, origin.y + size.y), IM_COL32(35, 92, 130, 65));
    const float centerY = origin.y + size.y * 0.5f;
    draw->AddLine(ImVec2(origin.x, centerY), ImVec2(origin.x + size.x, centerY), IM_COL32(70, 80, 92, 255));
    const std::size_t frames = buffer.FrameCount();
    const std::size_t visibleFrameStart = std::min(frames,
        static_cast<std::size_t>(visibleStart * buffer.sampleRate));
    const std::size_t visibleFrameEnd = std::min(frames,
        static_cast<std::size_t>(visibleEnd * buffer.sampleRate));
    const std::size_t visibleFrames = std::max<std::size_t>(visibleFrameEnd - visibleFrameStart, 1);
    const int columns = std::max(1, static_cast<int>(size.x));
    for (int x = 0; x < columns; ++x) {
        const std::size_t begin = visibleFrameStart
            + visibleFrames * static_cast<std::size_t>(x) / columns;
        const std::size_t end = std::max(begin + 1, visibleFrameStart
            + visibleFrames * static_cast<std::size_t>(x + 1) / columns);
        float minimum = 1.0f;
        float maximum = -1.0f;
        for (std::size_t frame = begin; frame < std::min(end, frames); ++frame) {
            for (std::uint32_t channel = 0; channel < buffer.channels; ++channel) {
                const float sample = buffer.samples[frame * buffer.channels + channel];
                minimum = std::min(minimum, sample);
                maximum = std::max(maximum, sample);
            }
        }
        draw->AddLine(ImVec2(origin.x + x, centerY - maximum * (size.y * 0.46f)),
            ImVec2(origin.x + x, centerY - minimum * (size.y * 0.46f)), IM_COL32(64, 205, 226, 255));
    }
    draw->AddLine(ImVec2(selectedX0, origin.y), ImVec2(selectedX0, origin.y + size.y), IM_COL32(255, 205, 80, 255), 2.0f);
    draw->AddLine(ImVec2(selectedX1, origin.y), ImVec2(selectedX1, origin.y + size.y), IM_COL32(255, 205, 80, 255), 2.0f);
    static float dragAnchor = 0.0f;
    if (ImGui::IsItemActivated()) {
        const float normalized = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / size.x, 0.0f, 1.0f);
        dragAnchor = visibleStart + normalized * visibleDuration;
        g_audioEditor.selectionStart = dragAnchor;
        g_audioEditor.selectionEnd = dragAnchor;
    }
    if (ImGui::IsItemActive()) {
        const float normalized = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / size.x, 0.0f, 1.0f);
        const float cursor = visibleStart + normalized * visibleDuration;
        g_audioEditor.selectionStart = std::min(dragAnchor, cursor);
        g_audioEditor.selectionEnd = std::max(dragAnchor, cursor);
    }
}

void GenerateAudioEditorClip() {
    constexpr std::uint32_t sampleRate = 44100;
    const float duration = std::clamp(g_audioEditor.generatorDuration, 0.05f, 30.0f);
    const std::size_t frames = static_cast<std::size_t>(duration * sampleRate);
    engine::AudioBuffer generated;
    generated.sampleRate = sampleRate;
    generated.channels = 1;
    generated.samples.resize(frames);
    std::uint32_t noise = 0x12345678u;
    for (std::size_t i = 0; i < frames; ++i) {
        const float phase = 6.28318530718f * g_audioEditor.generatorFrequency * static_cast<float>(i) / sampleRate;
        float sample = 0.0f;
        if (g_audioEditor.generator == 0) sample = std::sin(phase);
        else if (g_audioEditor.generator == 1) sample = std::sin(phase) >= 0.0f ? 1.0f : -1.0f;
        else {
            noise = noise * 1664525u + 1013904223u;
            sample = static_cast<float>((noise >> 8) & 0x00FFFFFFu) / 8388607.5f - 1.0f;
        }
        generated.samples[i] = sample * std::clamp(g_audioEditor.generatorAmplitude, 0.0f, 1.0f);
    }
    g_audioEditor.buffer = generated;
    g_audioEditor.original = generated;
    g_audioEditor.undo = {};
    g_audioEditor.undoStack.clear();
    g_audioEditor.redoStack.clear();
    g_audioEditor.sourcePath.clear();
    g_audioEditor.selectionStart = 0.0f;
    g_audioEditor.selectionEnd = duration;
    g_audioEditor.dirty = true;
}

void EnsureAudioAssetOutputPaths(EditorDockspace::Context& context) {
    if (g_audioEditor.cuePath[0] == '\0') {
        const std::filesystem::path root = context.assets
            ? std::filesystem::path(context.assets->RootPath()) : std::filesystem::path("Content");
        const std::string path = (root / "Audio" / "new_cue.3dgaudio").string();
        std::snprintf(g_audioEditor.cuePath.data(), g_audioEditor.cuePath.size(), "%s", path.c_str());
    }
    if (g_audioEditor.musicPath[0] == '\0') {
        const std::filesystem::path root = context.assets
            ? std::filesystem::path(context.assets->RootPath()) : std::filesystem::path("Content");
        const std::string path = (root / "Audio" / "adaptive_music.3dgmusic").string();
        std::snprintf(g_audioEditor.musicPath.data(), g_audioEditor.musicPath.size(), "%s", path.c_str());
    }
}

std::string SelectedAudioAssetPath(EditorDockspace::Context& context) {
    if (!context.assets) return {};
    const EditorAssets::Asset* selected = context.assets->SelectedAsset();
    const std::string path = context.assets->SelectedAssetFullPath();
    return selected && IsEditableAudioPath(path) ? path : std::string{};
}

void RefreshAudioAssets(EditorDockspace::Context& context) {
    if (!context.assets) return;
    std::string ignored;
    context.assets->Refresh(context.assets->RootPath(), &ignored);
}

void DrawAudioCueAuthoring(EditorDockspace::Context& context) {
    EnsureAudioAssetOutputPaths(context);
    engine::AudioCueAsset& cue = g_audioEditor.cue;
    if (!ImGui::CollapsingHeader("Gameplay Audio Cue", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::TextDisabled("Reusable randomized, sequential, or layered gameplay sound.");
    const char* modes[] = {"Random", "Sequence", "Layered"};
    int mode = static_cast<int>(cue.mode);
    if (ImGui::Combo("Playback Mode", &mode, modes, 3))
        cue.mode = static_cast<engine::AudioCueMode>(mode);
    int bus = static_cast<int>(cue.bus);
    if (ImGui::BeginCombo("Cue Bus", engine::AudioBusName(cue.bus))) {
        for (int i = static_cast<int>(engine::AudioBus::Music);
             i <= static_cast<int>(engine::AudioBus::Ambient); ++i) {
            if (ImGui::Selectable(engine::AudioBusName(static_cast<engine::AudioBus>(i)), i == bus)) {
                bus = i;
                cue.bus = static_cast<engine::AudioBus>(i);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::DragFloatRange2("Volume Variation", &cue.volumeMin, &cue.volumeMax,
        0.01f, 0.0f, 2.0f, "%.2f", "%.2f");
    ImGui::DragFloatRange2("Pitch Variation", &cue.pitchMin, &cue.pitchMax,
        0.01f, 0.1f, 4.0f, "%.2f", "%.2f");
    ImGui::DragFloat("Cooldown", &cue.cooldownSeconds, 0.01f, 0.0f, 60.0f, "%.2f s");
    ImGui::SliderInt("Max Instances", &cue.maxInstances, 1, 64);
    ImGui::SliderInt("Priority", &cue.priority, 0, 100);
    ImGui::Checkbox("Spatial Cue", &cue.spatial);
    ImGui::SameLine();
    ImGui::Checkbox("Avoid Immediate Repeat", &cue.noImmediateRepeat);

    if (ImGui::Button("Add Selected Audio Clip")) {
        const std::string path = SelectedAudioAssetPath(context);
        if (!path.empty()) {
            cue.clips.push_back({path});
            g_audioEditor.selectedCueClip = static_cast<int>(cue.clips.size()) - 1;
        } else if (context.log) {
            context.log->Warning("Audio Cue: select a WAV, FLAC, MP3, or OGG asset first");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove Clip") && g_audioEditor.selectedCueClip >= 0
        && g_audioEditor.selectedCueClip < static_cast<int>(cue.clips.size())) {
        cue.clips.erase(cue.clips.begin() + g_audioEditor.selectedCueClip);
        g_audioEditor.selectedCueClip = std::min(g_audioEditor.selectedCueClip,
            static_cast<int>(cue.clips.size()) - 1);
    }
    ImGui::BeginChild("##CueClips", ImVec2(0.0f, 110.0f), true);
    for (std::size_t i = 0; i < cue.clips.size(); ++i) {
        const std::string label = std::to_string(i + 1) + ". "
            + std::filesystem::path(cue.clips[i].path).filename().string();
        if (ImGui::Selectable(label.c_str(), g_audioEditor.selectedCueClip == static_cast<int>(i)))
            g_audioEditor.selectedCueClip = static_cast<int>(i);
    }
    ImGui::EndChild();
    if (g_audioEditor.selectedCueClip >= 0
        && g_audioEditor.selectedCueClip < static_cast<int>(cue.clips.size())) {
        engine::AudioCueClip& clip = cue.clips[static_cast<std::size_t>(g_audioEditor.selectedCueClip)];
        ImGui::TextWrapped("%s", clip.path.c_str());
        ImGui::DragFloat("Weight", &clip.weight, 0.05f, 0.0f, 100.0f);
        ImGui::DragFloat("Clip Volume", &clip.volume, 0.01f, 0.0f, 4.0f);
        ImGui::DragFloat("Clip Pitch", &clip.pitch, 0.01f, 0.1f, 4.0f);
        ImGui::DragFloat("Layer Delay", &clip.delaySeconds, 0.01f, 0.0f, 30.0f, "%.2f s");
    }
    ImGui::InputText("Cue Asset", g_audioEditor.cuePath.data(), g_audioEditor.cuePath.size());
    if (ImGui::Button("Save Cue")) {
        std::string error;
        if (engine::SaveAudioCue(g_audioEditor.cuePath.data(), cue, &error)) {
            RefreshAudioAssets(context);
            if (context.log) context.log->Info("Saved audio cue: " + std::string(g_audioEditor.cuePath.data()));
        } else if (context.log) context.log->Error("Audio Cue: " + error);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Cue")) {
        std::string error;
        if (!engine::LoadAudioCue(g_audioEditor.cuePath.data(), &cue, &error)) {
            if (context.log) context.log->Error("Audio Cue: " + error);
        } else {
            g_audioEditor.selectedCueClip = cue.clips.empty() ? -1 : 0;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Preview Cue")) {
        context.previewAudioRequested = true;
        context.previewAudioPath = g_audioEditor.cuePath.data();
        context.previewAudioSpatial = cue.spatial;
    }
}

void DrawAdaptiveMusicAuthoring(EditorDockspace::Context& context) {
    if (!ImGui::CollapsingHeader("Adaptive Music", ImGuiTreeNodeFlags_DefaultOpen)) return;
    engine::AdaptiveMusicAsset& music = g_audioEditor.music;
    if (ImGui::Button("Add Music State")) {
        engine::AdaptiveMusicState state;
        state.name = "State " + std::to_string(music.states.size() + 1);
        const std::string selected = SelectedAudioAssetPath(context);
        if (!selected.empty()) state.stems.push_back(selected);
        music.states.push_back(std::move(state));
        g_audioEditor.selectedMusicState = static_cast<int>(music.states.size()) - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove State") && g_audioEditor.selectedMusicState >= 0
        && g_audioEditor.selectedMusicState < static_cast<int>(music.states.size())) {
        music.states.erase(music.states.begin() + g_audioEditor.selectedMusicState);
        g_audioEditor.selectedMusicState = std::min(g_audioEditor.selectedMusicState,
            static_cast<int>(music.states.size()) - 1);
    }
    if (ImGui::BeginCombo("Music State",
        g_audioEditor.selectedMusicState >= 0
            && g_audioEditor.selectedMusicState < static_cast<int>(music.states.size())
        ? music.states[static_cast<std::size_t>(g_audioEditor.selectedMusicState)].name.c_str()
        : "Select state...")) {
        for (std::size_t i = 0; i < music.states.size(); ++i)
            if (ImGui::Selectable(music.states[i].name.c_str(),
                    g_audioEditor.selectedMusicState == static_cast<int>(i)))
                g_audioEditor.selectedMusicState = static_cast<int>(i);
        ImGui::EndCombo();
    }
    if (g_audioEditor.selectedMusicState >= 0
        && g_audioEditor.selectedMusicState < static_cast<int>(music.states.size())) {
        auto& state = music.states[static_cast<std::size_t>(g_audioEditor.selectedMusicState)];
        ImGui::DragFloat("BPM", &state.bpm, 0.5f, 1.0f, 400.0f);
        ImGui::SliderFloat("Music Volume", &state.volume, 0.0f, 2.0f);
        ImGui::DragFloat("Crossfade", &state.crossfadeSeconds, 0.05f, 0.0f, 30.0f, "%.2f s");
        if (ImGui::Button("Add Selected Stem")) {
            const std::string selected = SelectedAudioAssetPath(context);
            if (!selected.empty()) state.stems.push_back(selected);
        }
        for (std::size_t i = 0; i < state.stems.size(); ++i)
            ImGui::BulletText("%s", std::filesystem::path(state.stems[i]).filename().string().c_str());
    }
    ImGui::InputText("Music Asset", g_audioEditor.musicPath.data(), g_audioEditor.musicPath.size());
    if (ImGui::Button("Save Adaptive Music")) {
        std::string error;
        if (engine::SaveAdaptiveMusic(g_audioEditor.musicPath.data(), music, &error))
            RefreshAudioAssets(context);
        else if (context.log) context.log->Error("Adaptive Music: " + error);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Adaptive Music")) {
        std::string error;
        if (!engine::LoadAdaptiveMusic(g_audioEditor.musicPath.data(), &music, &error)) {
            if (context.log) context.log->Error("Adaptive Music: " + error);
        } else {
            g_audioEditor.selectedMusicState = music.states.empty() ? -1 : 0;
        }
    }
}

ImVec4 LogColor(EditorLog::Level level) {
    switch (level) {
    case EditorLog::Level::Info: return ImVec4(0.78f, 0.84f, 0.92f, 1.0f);
    case EditorLog::Level::Warning: return ImVec4(1.0f, 0.78f, 0.30f, 1.0f);
    case EditorLog::Level::Error: return ImVec4(1.0f, 0.34f, 0.32f, 1.0f);
    }
    return ImVec4(0.78f, 0.84f, 0.92f, 1.0f);
}

void DrawWorldSettings(EditorScene& scene, EditorDockspace::Context& context, bool* open) {
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
            environment.msaa = defaults.msaa;
            environment.fxaa = defaults.fxaa;
            environment.renderScale = defaults.renderScale;
            changed = true;
        }
        changed |= ImGui::Checkbox("IBL", &environment.ibl);
        changed |= ImGui::Checkbox("SSAO", &environment.ssao);
        changed |= ImGui::DragFloat("SSAO Radius", &environment.ssaoRadius, 0.01f, 0.05f, 5.0f, "%.2f");
        changed |= ImGui::DragFloat("SSAO Bias", &environment.ssaoBias, 0.001f, 0.0f, 0.2f, "%.3f");
        changed |= ImGui::Checkbox("SSR", &environment.ssr);
        changed |= ImGui::DragFloat("SSR Intensity", &environment.ssrIntensity, 0.01f, 0.0f, 2.0f, "%.2f");
        ImGui::Separator();
        ImGui::TextUnformatted("Anti-aliasing");
        changed |= ImGui::Checkbox("MSAA (4x, direct view)", &environment.msaa);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Multisample AA on the main view. Active when SSR is off.");
        }
        changed |= ImGui::Checkbox("FXAA (post, SSR view)", &environment.fxaa);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Post-process edge AA. Active when SSR is on.");
        }
        changed |= ImGui::SliderFloat("Render Scale", &environment.renderScale, 0.25f, 1.0f, "%.2fx");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Render the 3D scene at this fraction of window resolution, then "
                              "upscale. Cuts fill-rate GPU cost. Ignored while SSR or SSAO is on.");
        }
        ImGui::Separator();
        ImGui::TextUnformatted("Display");
        if (ImGui::Checkbox("VSync", &context.vsync)) {
            context.vsyncChangeRequested = true;   // applied by the app (window setting)
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Cap the frame rate to the monitor refresh. Off = uncapped (see Profiler).");
        }
    }

    if (ImGui::CollapsingHeader(
            "Post Process Shader Stack", ImGuiTreeNodeFlags_DefaultOpen)) {
        const EditorAssets::Asset* selected =
            context.assets ? context.assets->SelectedAsset() : nullptr;
        const bool selectedShader =
            selected && selected->type == EditorAssets::Type::Shader;
        if (!selectedShader) ImGui::BeginDisabled();
        if (ImGui::Button("Add Selected Shader") && context.assets) {
            const std::string path = context.assets->SelectedAssetFullPath();
            engine::ShaderAsset shader;
            std::string loadError;
            if (!engine::LoadShaderAsset(path, &shader, &loadError)) {
                if (context.log)
                    context.log->Error("Post Process: " + loadError);
            } else if (shader.domain != engine::ShaderDomain::PostProcess) {
                if (context.log)
                    context.log->Warning(
                        "Only Post Process shader assets can be added to this stack.");
            } else {
                EditorScene::Environment::PostProcessEffect effect;
                effect.shaderPath = path;
                for (const engine::ShaderParameter& reflected :
                     shader.parameters) {
                    effect.parameters.push_back({
                        reflected.name,
                        static_cast<int>(reflected.type),
                        reflected.defaultValue
                    });
                }
                environment.postProcessEffects.push_back(std::move(effect));
                changed = true;
            }
        }
        if (!selectedShader) ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("Effects run top to bottom before bloom and tone mapping.");

        int moveFrom = -1;
        int moveTo = -1;
        int remove = -1;
        for (std::size_t i = 0; i < environment.postProcessEffects.size(); ++i) {
            auto& effect = environment.postProcessEffects[i];
            ImGui::PushID(static_cast<int>(i));
            const std::string label =
                std::to_string(i + 1) + ". "
                + std::filesystem::path(effect.shaderPath).filename().string();
            const bool expanded = ImGui::TreeNodeEx(
                "Effect", ImGuiTreeNodeFlags_DefaultOpen, "%s", label.c_str());
            ImGui::SameLine();
            changed |= ImGui::Checkbox("Enabled", &effect.enabled);
            ImGui::SameLine();
            if (ImGui::SmallButton("Up") && i > 0) {
                moveFrom = static_cast<int>(i);
                moveTo = static_cast<int>(i - 1);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Down")
                && i + 1 < environment.postProcessEffects.size()) {
                moveFrom = static_cast<int>(i);
                moveTo = static_cast<int>(i + 1);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove"))
                remove = static_cast<int>(i);
            if (expanded) {
                ImGui::TextDisabled("%s", effect.shaderPath.c_str());
                for (auto& parameter : effect.parameters) {
                    ImGui::PushID(parameter.name.c_str());
                    if (parameter.type
                        == static_cast<int>(engine::ShaderValueType::Bool)) {
                        bool value = parameter.value == "true"
                            || parameter.value == "1";
                        if (ImGui::Checkbox(parameter.name.c_str(), &value)) {
                            parameter.value = value ? "true" : "false";
                            changed = true;
                        }
                    } else if (parameter.type
                               == static_cast<int>(
                                   engine::ShaderValueType::Float)) {
                        float value = std::strtof(
                            parameter.value.c_str(), nullptr);
                        if (ImGui::DragFloat(
                                parameter.name.c_str(), &value, 0.01f)) {
                            parameter.value = std::to_string(value);
                            changed = true;
                        }
                    } else {
                        std::array<char, 384> value{};
                        std::snprintf(
                            value.data(), value.size(), "%s",
                            parameter.value.c_str());
                        if (ImGui::InputText(
                                parameter.name.c_str(), value.data(),
                                value.size())) {
                            parameter.value = value.data();
                            changed = true;
                        }
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (moveFrom >= 0 && moveTo >= 0) {
            std::swap(environment.postProcessEffects[
                          static_cast<std::size_t>(moveFrom)],
                      environment.postProcessEffects[
                          static_cast<std::size_t>(moveTo)]);
            changed = true;
        }
        if (remove >= 0) {
            environment.postProcessEffects.erase(
                environment.postProcessEffects.begin() + remove);
            changed = true;
        }
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
            DrawStatusRow("Capsule Colliders", status.capsules);
            DrawStatusRow("Cylinder Colliders", status.cylinders);
            DrawStatusRow("Cone Colliders", status.cones);
            DrawStatusRow("Pyramid Colliders", status.pyramids);
            DrawStatusRow("Torus Colliders", status.toruses);
            DrawStatusRow("Stair Colliders", status.staircases);
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
                } else if (selected->collider.shape == engine::ecs::ColliderShape::Box
                    || selected->collider.shape == engine::ecs::ColliderShape::Pyramid
                    || selected->collider.shape == engine::ecs::ColliderShape::Staircase) {
                    ImGui::Text("Half Extents: %.3f, %.3f, %.3f",
                        selected->collider.halfExtents.x,
                        selected->collider.halfExtents.y,
                        selected->collider.halfExtents.z);
                    if (selected->collider.shape == engine::ecs::ColliderShape::Staircase)
                        ImGui::Text("Steps: %d", selected->collider.steps);
                } else if (selected->collider.shape == engine::ecs::ColliderShape::Capsule
                    || selected->collider.shape == engine::ecs::ColliderShape::Cylinder
                    || selected->collider.shape == engine::ecs::ColliderShape::Cone) {
                    ImGui::Text("Radius / Half Height: %.3f / %.3f",
                        selected->collider.radius, selected->collider.halfHeight);
                } else if (selected->collider.shape == engine::ecs::ColliderShape::Torus) {
                    ImGui::Text("Major / Minor Radius: %.3f / %.3f",
                        selected->collider.majorRadius, selected->collider.minorRadius);
                } else if (selected->collider.shape == engine::ecs::ColliderShape::Plane) {
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
        if (context.showAiDebug) {
            ImGui::Checkbox("AI Debug", context.showAiDebug);
        }
        if (context.useNavMesh) {
            ImGui::Checkbox("AI: Use Navmesh", context.useNavMesh);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Route chase/search paths through the funnel-smoothed "
                                  "navmesh instead of the grid (applied on next Play).");
            }
        }
        if (context.showNavigationPreview) {
            if (ImGui::Checkbox("Show Navigation Areas", context.showNavigationPreview)
                && *context.showNavigationPreview) {
                context.rebuildNavigationPreviewRequested = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Rebuild Navigation")) {
                context.rebuildNavigationPreviewRequested = true;
            }
            ImGui::Text("Walkable polygons: %d", context.navigationPreviewPolygons);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Green areas are locations the AI can navigate to. Rebuild after changing bounds or colliders.");
            }
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
            std::vector<EditorScene::AnimationParameter> parameters = state.parameterDefinitions;
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
                const char* blendClip = node.blendClipIndex >= 0 && node.blendClipIndex < static_cast<int>(state.clips.size())
                    ? state.clips[static_cast<std::size_t>(node.blendClipIndex)].name.c_str() : "Disabled";
                if (ImGui::BeginCombo("Blend Clip", blendClip)) {
                    if (ImGui::Selectable("Disabled", node.blendClipIndex < 0)) {
                        node.blendClipIndex = -1;
                        node.blendClipName.clear();
                        changed = true;
                    }
                    for (std::size_t clipIndex = 0; clipIndex < state.clips.size(); ++clipIndex) {
                        if (ImGui::Selectable(state.clips[clipIndex].name.c_str(), node.blendClipIndex == static_cast<int>(clipIndex))) {
                            node.blendClipIndex = static_cast<int>(clipIndex);
                            node.blendClipName = state.clips[clipIndex].name;
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                if (node.blendClipIndex >= 0) {
                    std::array<char, 64> blendParameter{};
                    std::snprintf(blendParameter.data(), blendParameter.size(), "%s", node.blendParameter.c_str());
                    if (ImGui::InputText("Blend Parameter", blendParameter.data(), blendParameter.size())) {
                        node.blendParameter = blendParameter.data(); changed = true;
                    }
                    changed |= ImGui::DragFloat("Blend Min", &node.blendMin, 0.01f);
                    changed |= ImGui::DragFloat("Blend Max", &node.blendMax, 0.01f);
                }
                if (ImGui::Checkbox("Root Motion", &node.rootMotion)) changed = true;
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
            ImGui::Text("Parameters: %zu", parameters.size());
            int removeParameter = -1;
            const char* parameterTypes[] = {"Float", "Bool", "Trigger"};
            for (std::size_t i = 0; i < parameters.size(); ++i) {
                auto& parameter = parameters[i];
                ImGui::PushID(5000 + static_cast<int>(i));
                std::array<char, 64> name{};
                std::snprintf(name.data(), name.size(), "%s", parameter.name.c_str());
                if (ImGui::InputText("Name", name.data(), name.size())) { parameter.name = name.data(); changed = true; }
                int type = std::clamp(static_cast<int>(parameter.type), 0, 2);
                if (ImGui::Combo("Type", &type, parameterTypes, 3)) {
                    parameter.type = static_cast<EditorScene::AnimationParameter::Type>(type); changed = true;
                }
                if (parameter.type == EditorScene::AnimationParameter::Type::Float) {
                    changed |= ImGui::DragFloat("Default", &parameter.defaultValue, 0.01f);
                } else {
                    bool defaultValue = parameter.defaultValue != 0.0f;
                    if (ImGui::Checkbox("Default", &defaultValue)) { parameter.defaultValue = defaultValue ? 1.0f : 0.0f; changed = true; }
                }
                if (ImGui::Button("Remove Parameter")) removeParameter = static_cast<int>(i);
                ImGui::Separator(); ImGui::PopID();
            }
            if (removeParameter >= 0) { parameters.erase(parameters.begin() + removeParameter); changed = true; }
            if (ImGui::Button("Add Parameter")) {
                parameters.push_back({"Parameter", EditorScene::AnimationParameter::Type::Float, 0.0f}); changed = true;
            }

            ImGui::Separator();
            ImGui::Text("Transitions: %zu", transitions.size());
            const char* compareLabels[] = {">=", "<", "==", "!="};
            for (std::size_t i = 0; i < transitions.size(); ++i) {
                EditorScene::AnimationStateTransition& transition = transitions[i];
                ImGui::PushID(10000 + static_cast<int>(i));

                auto drawStateCombo = [&](const char* label, std::string& value, bool allowAny) {
                    const char* current = value.empty() ? (allowAny ? "Any State" : "-") : value.c_str();
                    if (ImGui::BeginCombo(label, current)) {
                        if (allowAny && ImGui::Selectable("Any State", value.empty())) {
                            value.clear(); changed = true;
                        }
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

                drawStateCombo("From", transition.fromState, true);
                drawStateCombo("To", transition.toState, false);

                if (!parameters.empty() && ImGui::BeginCombo("Parameter", transition.parameter.empty() ? "-" : transition.parameter.c_str())) {
                    for (const auto& parameter : parameters) {
                        if (ImGui::Selectable(parameter.name.c_str(), transition.parameter == parameter.name)) {
                            transition.parameter = parameter.name;
                            if (parameter.type == EditorScene::AnimationParameter::Type::Trigger) {
                                transition.compare = EditorScene::AnimationStateTransition::Compare::NotEqual;
                                transition.threshold = 0.0f;
                            }
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                } else if (parameters.empty()) {
                    std::array<char, 64> parameterBuffer{};
                    std::snprintf(parameterBuffer.data(), parameterBuffer.size(), "%s", transition.parameter.c_str());
                    if (ImGui::InputText("Parameter", parameterBuffer.data(), parameterBuffer.size())) {
                        transition.parameter = parameterBuffer.data(); changed = true;
                    }
                }

                auto parameterType = EditorScene::AnimationParameter::Type::Float;
                for (const auto& parameter : parameters) if (parameter.name == transition.parameter) { parameterType = parameter.type; break; }
                int compare = static_cast<int>(transition.compare);
                compare = std::clamp(compare, 0, 3);
                if (parameterType != EditorScene::AnimationParameter::Type::Trigger && ImGui::BeginCombo("Compare", compareLabels[compare])) {
                    const int firstCompare = parameterType == EditorScene::AnimationParameter::Type::Bool ? 2 : 0;
                    for (int c = firstCompare; c < 4; ++c) {
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
                if (parameterType == EditorScene::AnimationParameter::Type::Float) {
                    if (ImGui::DragFloat("Threshold", &transition.threshold, 0.01f, -1000.0f, 1000.0f, "%.2f")) changed = true;
                } else if (parameterType == EditorScene::AnimationParameter::Type::Bool) {
                    bool expected = transition.threshold != 0.0f;
                    if (ImGui::Checkbox("Expected", &expected)) { transition.threshold = expected ? 1.0f : 0.0f; changed = true; }
                } else {
                    ImGui::TextDisabled("Trigger is consumed after this transition fires.");
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
                context.scene->SetSelectedAnimationStateGraph(states, transitions, parameters);
            }
        }
    }

    if (ImGui::CollapsingHeader("Graph View", ImGuiTreeNodeFlags_DefaultOpen)) {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float width = std::max(ImGui::GetContentRegionAvail().x, 240.0f);
        const float height = std::max(180.0f, 90.0f * std::ceil(static_cast<float>(std::max<std::size_t>(state.states.size(), 1)) / 3.0f));
        ImGui::InvisibleButton("animation_graph_canvas", ImVec2(width, height));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(24, 27, 32, 255), 5.0f);
        std::vector<ImVec2> centers;
        for (std::size_t i = 0; i < state.states.size(); ++i) {
            centers.emplace_back(origin.x + 75.0f + static_cast<float>(i % 3) * ((width - 150.0f) / 2.0f),
                origin.y + 45.0f + static_cast<float>(i / 3) * 90.0f);
        }
        auto findState = [&](const std::string& name) -> int {
            for (std::size_t i = 0; i < state.states.size(); ++i) if (state.states[i].name == name) return static_cast<int>(i);
            return -1;
        };
        for (const auto& transition : state.transitions) {
            const int to = findState(transition.toState);
            if (to < 0) continue;
            const int from = findState(transition.fromState);
            const ImVec2 target = centers[static_cast<std::size_t>(to)];
            const ImVec2 source = transition.fromState.empty() ? ImVec2(origin.x + 10.0f, target.y)
                : (from >= 0 ? centers[static_cast<std::size_t>(from)] : target);
            const ImU32 color = transition.fromState.empty() ? IM_COL32(240, 170, 70, 220) : IM_COL32(110, 155, 220, 210);
            draw->AddLine(source, target, color, 2.0f);
            const float dx = target.x - source.x, dy = target.y - source.y;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length > 1.0f) draw->AddCircleFilled(ImVec2(target.x - dx / length * 39.0f, target.y - dy / length * 19.0f), 3.0f, color);
        }
        for (std::size_t i = 0; i < state.states.size(); ++i) {
            const ImVec2 c = centers[i];
            const bool active = state.playMode && state.currentState == state.states[i].name;
            draw->AddRectFilled(ImVec2(c.x - 55.0f, c.y - 20.0f), ImVec2(c.x + 55.0f, c.y + 20.0f),
                active ? IM_COL32(65, 135, 85, 255) : IM_COL32(55, 62, 74, 255), 5.0f);
            draw->AddRect(ImVec2(c.x - 55.0f, c.y - 20.0f), ImVec2(c.x + 55.0f, c.y + 20.0f),
                active ? IM_COL32(120, 240, 145, 255) : IM_COL32(105, 120, 145, 255), 5.0f);
            const std::string label = state.states[i].name.empty() ? "(unnamed)" : state.states[i].name;
            const ImVec2 size = ImGui::CalcTextSize(label.c_str());
            draw->AddText(ImVec2(c.x - size.x * 0.5f, c.y - size.y * 0.5f), IM_COL32_WHITE, label.c_str());
        }
    }

    if (ImGui::CollapsingHeader("Graph Validation", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (state.graphWarnings.empty()) ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.55f, 1.0f), "Graph is valid.");
        else for (const std::string& warning : state.graphWarnings) ImGui::BulletText("%s", warning.c_str());
    }

    if (ImGui::CollapsingHeader("Graph Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (state.parameters.empty()) {
            ImGui::TextUnformatted("No graph parameters are referenced by transitions.");
        } else if (state.playMode || !context.animationPreviewParameters) {
            for (const EditorDockspace::AnimationPreviewState::ParameterInfo& parameter : state.parameters) {
                if (parameter.type == EditorScene::AnimationParameter::Type::Float) ImGui::Text("%s: %.3f", parameter.name.c_str(), parameter.value);
                else ImGui::Text("%s: %s", parameter.name.c_str(), parameter.value != 0.0f ? "true" : "false");
            }
        } else {
            for (const EditorDockspace::AnimationPreviewState::ParameterInfo& parameter : state.parameters) {
                float value = parameter.value;
                const auto found = context.animationPreviewParameters->find(parameter.name);
                if (found != context.animationPreviewParameters->end()) {
                    value = found->second;
                }
                if (parameter.type == EditorScene::AnimationParameter::Type::Float) {
                    if (ImGui::DragFloat(parameter.name.c_str(), &value, 0.01f, -1000.0f, 1000.0f, "%.3f"))
                        (*context.animationPreviewParameters)[parameter.name] = value;
                } else if (parameter.type == EditorScene::AnimationParameter::Type::Bool) {
                    bool enabled = std::abs(value) > 0.0001f;
                    if (ImGui::Checkbox(parameter.name.c_str(), &enabled))
                        (*context.animationPreviewParameters)[parameter.name] = enabled ? 1.0f : 0.0f;
                } else if (ImGui::Button(parameter.name.c_str())) {
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

    if (ImGui::CollapsingHeader("Transition Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!state.playMode) {
            ImGui::TextUnformatted("Transition diagnostics are live in Play mode.");
        } else if (!state.runtimeAnimated) {
            ImGui::TextUnformatted("Selected object has no runtime animation controller.");
        } else if (state.transitionDebugRows.empty()) {
            ImGui::TextUnformatted("No runtime graph transitions are active on this object.");
        } else {
            for (const EditorDockspace::AnimationPreviewState::TransitionDebugRow& row : state.transitionDebugRows) {
                const char* status = "Blocked";
                if (row.selected) {
                    status = "Selected";
                } else if (row.eligible) {
                    status = "Ready";
                } else if (row.blockedByBlend) {
                    status = "Blend";
                } else if (!row.exitTimeReached) {
                    status = "Exit";
                } else if (!row.conditionMet) {
                    status = "Condition";
                }

                ImGui::Text("%s -> %s", row.fromState.empty() ? "(none)" : row.fromState.c_str(),
                    row.toState.empty() ? "(none)" : row.toState.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("[%s]", status);
                ImGui::Text("Param %s: %.3f / %.3f", row.parameter.empty() ? "Speed" : row.parameter.c_str(),
                    row.value,
                    row.threshold);
                ImGui::Text("Exit %.2f %s  Priority %d%s",
                    row.exitTime,
                    row.exitTimeReached ? "ready" : "waiting",
                    row.priority,
                    row.canInterrupt ? "  interrupt" : "");
                ImGui::Separator();
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
            ImGui::TextWrapped("Audio commands: Audio.Play, Audio.Restart:SourceName, "
                               "Audio.Pause:SourceName, Audio.Resume:SourceName, Audio.Stop:SourceName");
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

    if (!selected->navMeshBoundsVolume) {
        if (ImGui::Button("+ Add Component", ImVec2(-1.0f, 30.0f))) {
            g_componentPopupOpenRequested = true;
        }
        DrawAddComponentPopup(context);
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

    if (selected->navMeshBoundsVolume) {
        if (ImGui::CollapsingHeader("Navigation Bounds", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped("This editor-only volume defines the region baked into the navigation grid and nav mesh. Scale and move it to cover the playable area.");
            if (const engine::ecs::Transform* transform = context.scene->TryGetTransform(selected->entity)) {
                ImGui::Text("Bounds size: %.2f x %.2f x %.2f",
                    std::abs(transform->scale.x), std::abs(transform->scale.y), std::abs(transform->scale.z));
                ImGui::Text("Navigation floor: %.2f", transform->position.y - std::abs(transform->scale.y) * 0.5f);
            }
            ImGui::TextDisabled("The volume is excluded from runtime rendering and collision.");
        }
        ImGui::End();
        return;
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
        if (context.runtimeAssets && !selected->materialAssetPath.empty()
            && std::filesystem::path(selected->materialAssetPath).extension() == ".3dgmat") {
            std::string materialError;
            const engine::RuntimeMaterialAsset* material =
                context.runtimeAssets->LoadMaterial(
                    selected->materialAssetPath, &materialError);
            if (material && !material->shaderPath.empty()
                && ImGui::TreeNode("Material Instance Overrides")) {
                ImGui::TextDisabled("Shader: %s", material->shaderPath.c_str());
                for (const engine::RuntimeShaderParameter& parameter :
                     material->shaderParameters) {
                    const auto authored =
                        selected->materialParameterOverrides.find(parameter.name);
                    std::string value = authored == selected->materialParameterOverrides.end()
                        ? parameter.value : authored->second;
                    bool changed = false;
                    ImGui::PushID(parameter.name.c_str());
                    if (parameter.type == static_cast<int>(engine::ShaderValueType::Bool)) {
                        bool enabled = value == "true" || value == "1";
                        if (ImGui::Checkbox(parameter.name.c_str(), &enabled)) {
                            value = enabled ? "true" : "false";
                            changed = true;
                        }
                    } else if (parameter.type == static_cast<int>(engine::ShaderValueType::Float)) {
                        float number = std::strtof(value.c_str(), nullptr);
                        if (ImGui::DragFloat(parameter.name.c_str(), &number, 0.01f)) {
                            value = std::to_string(number);
                            changed = true;
                        }
                    } else {
                        std::array<char, 256> buffer{};
                        std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
                        if (ImGui::InputText(parameter.name.c_str(),
                                             buffer.data(), buffer.size())) {
                            value = buffer.data();
                            changed = true;
                        }
                    }
                    if (changed)
                        context.scene->SetSelectedMaterialParameterOverride(
                            parameter.name, value);
                    ImGui::PopID();
                }
                ImGui::TreePop();
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

    if (selected->audioSourceEnabled) {
        if (ImGui::CollapsingHeader("Audio Source", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool enabled = selected->audioSourceEnabled;
            std::string path = selected->audioAssetPath;
            float volume = selected->audioVolume;
            float pitch = selected->audioPitch;
            bool spatial = selected->audioSpatial;
            bool loop = selected->audioLoop;
            bool autoplay = selected->audioAutoplay;
            float minDistance = selected->audioMinDistance;
            float maxDistance = selected->audioMaxDistance;
            float rolloff = selected->audioRolloff;
            float dopplerFactor = selected->audioDopplerFactor;
            float coneInnerAngle = selected->audioConeInnerAngle;
            float coneOuterAngle = selected->audioConeOuterAngle;
            float coneOuterGain = selected->audioConeOuterGain;
            float occlusion = selected->audioOcclusion;
            int priority = selected->audioPriority;
            engine::AudioBus bus = selected->audioBus;
            bool changed = false;

            changed |= ImGui::Checkbox("Enabled##AudioSource", &enabled);
            const char* preview = path.empty() ? "Select audio asset..." : path.c_str();
            if (ImGui::BeginCombo("Audio Clip", preview)) {
                if (ImGui::Selectable("None", path.empty())) { path.clear(); changed = true; }
                if (context.assets) {
                    const std::vector<EditorAssets::Asset> audioAssets = FindAudioAssets(*context.assets);
                    for (const EditorAssets::Asset& asset : audioAssets) {
                        const std::string fullPath = (std::filesystem::path(context.assets->RootPath()) / asset.relativePath).string();
                        const bool current = path == fullPath;
                        if (ImGui::Selectable(asset.relativePath.c_str(), current)) {
                            path = fullPath;
                            changed = true;
                        }
                        if (current) ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::Button("Drop audio asset here", ImVec2(-1.0f, 0.0f));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("3DGEDITOR_ASSET")) {
                    const std::string dropped(static_cast<const char*>(payload->Data));
                    std::string extension = std::filesystem::path(dropped).extension().string();
                    std::transform(extension.begin(), extension.end(), extension.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (extension == ".wav" || extension == ".flac" || extension == ".mp3" || extension == ".ogg") {
                        path = dropped;
                        changed = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            changed |= ImGui::SliderFloat("Volume", &volume, 0.0f, 2.0f, "%.2f");
            changed |= ImGui::SliderFloat("Pitch", &pitch, 0.25f, 4.0f, "%.2f");
            if (ImGui::BeginCombo("Mixer Bus", engine::AudioBusName(bus))) {
                for (int i = static_cast<int>(engine::AudioBus::Master);
                     i <= static_cast<int>(engine::AudioBus::Ambient); ++i) {
                    const engine::AudioBus candidate = static_cast<engine::AudioBus>(i);
                    const bool current = candidate == bus;
                    if (ImGui::Selectable(engine::AudioBusName(candidate), current)) {
                        bus = candidate;
                        changed = true;
                    }
                    if (current) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            changed |= ImGui::Checkbox("Spatial (3D)", &spatial);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("Loop", &loop);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("Autoplay", &autoplay);
            if (spatial) {
                changed |= ImGui::DragFloat("Min Distance", &minDistance, 0.1f, 0.01f, 10000.0f);
                changed |= ImGui::DragFloat("Max Distance", &maxDistance, 0.25f, 0.01f, 10000.0f);
                maxDistance = std::max(maxDistance, minDistance);
                changed |= ImGui::DragFloat("Rolloff", &rolloff, 0.05f, 0.0f, 20.0f);
                changed |= ImGui::DragFloat("Doppler", &dopplerFactor, 0.05f, 0.0f, 10.0f);
                changed |= ImGui::SliderFloat("Cone Inner", &coneInnerAngle, 0.0f, 360.0f, "%.0f deg");
                changed |= ImGui::SliderFloat("Cone Outer", &coneOuterAngle,
                    coneInnerAngle, 360.0f, "%.0f deg");
                changed |= ImGui::SliderFloat("Cone Outer Gain", &coneOuterGain, 0.0f, 1.0f);
                changed |= ImGui::SliderFloat("Occlusion", &occlusion, 0.0f, 1.0f);
                ImGui::TextDisabled("Inner and outer attenuation ranges are shown in the viewport.");
            }
            changed |= ImGui::SliderInt("Voice Priority", &priority, 0, 100);
            if (changed) {
                context.scene->SetSelectedAudioSource(enabled, path, volume, pitch, spatial,
                    loop, autoplay, minDistance, maxDistance, rolloff, bus,
                    dopplerFactor, coneInnerAngle, coneOuterAngle, coneOuterGain,
                    occlusion, priority);
            }

            const bool canPreview = context.audioAvailable && !path.empty();
            if (!canPreview) ImGui::BeginDisabled();
            if (ImGui::Button("Preview")) {
                context.previewAudioRequested = true;
                context.previewAudioPath = path;
                context.previewAudioVolume = volume;
                context.previewAudioPitch = pitch;
                context.previewAudioSpatial = spatial;
                context.previewAudioLoop = loop;
                context.previewAudioMinDistance = minDistance;
                context.previewAudioMaxDistance = maxDistance;
                context.previewAudioRolloff = rolloff;
                context.previewAudioBus = bus;
            }
            if (!canPreview) ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Stop Preview")) context.stopAudioPreviewRequested = true;
            if (!context.audioAvailable) {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "No audio device is available.");
            }
            if (context.playMode) {
                ImGui::Separator();
                ImGui::TextUnformatted("Runtime Transport");
                if (!context.selectedRuntimeAudioAvailable) ImGui::BeginDisabled();
                if (ImGui::Button("Restart##RuntimeAudio")) context.runtimeAudioRestartRequested = true;
                ImGui::SameLine();
                const char* pauseLabel = context.selectedRuntimeAudioPaused
                    ? "Resume##RuntimeAudio" : "Pause##RuntimeAudio";
                if (ImGui::Button(pauseLabel)) context.runtimeAudioPauseResumeRequested = true;
                ImGui::SameLine();
                if (ImGui::Button("Stop##RuntimeAudio")) context.runtimeAudioStopRequested = true;
                if (!context.selectedRuntimeAudioAvailable) ImGui::EndDisabled();
                ImGui::Text("State: %s  |  %.2f s",
                    context.selectedRuntimeAudioPlaying ? "Playing"
                    : context.selectedRuntimeAudioPaused ? "Paused" : "Stopped",
                    context.selectedRuntimeAudioCursor);
            }
            if (ImGui::Button("Remove Audio Source")) {
                context.scene->SetSelectedAudioSource(false, path, volume, pitch, spatial,
                    loop, autoplay, minDistance, maxDistance, rolloff, bus,
                    dopplerFactor, coneInnerAngle, coneOuterAngle, coneOuterGain,
                    occlusion, priority);
            }
        }
    }

    if (selected->particleSystemEnabled) {
        if (ImGui::CollapsingHeader("Particle System", ImGuiTreeNodeFlags_DefaultOpen)) {
            engine::ParticleSystemComponent settings;
            settings.config = selected->particleConfig;
            settings.autoplay = selected->particleAutoplay;
            settings.loop = selected->particleLoop;
            settings.prewarm = selected->particlePrewarm;
            settings.duration = selected->particleDuration;
            settings.startDelay = selected->particleStartDelay;
            settings.simulationSpeed = selected->particleSimulationSpeed;
            settings.localSpace = selected->particleLocalSpace;
            settings.burstCount = selected->particleBurstCount;
            settings.burstInterval = selected->particleBurstInterval;
            bool enabled = selected->particleSystemEnabled;
            bool changed = false;

            ImGui::Text("Asset: %s%s", selected->particleAssetPath.empty()
                ? "(none - instance settings)" : selected->particleAssetPath.c_str(),
                selected->particleAssetOverride ? " (overridden)" : "");
            ImGui::Button("Drop .particle asset here", ImVec2(-1.0f, 0.0f));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("3DGEDITOR_ASSET")) {
                    const char* path = static_cast<const char*>(payload->Data);
                    if (path && std::filesystem::path(path).extension() == ".particle") {
                        engine::ParticleSystemComponent loaded;
                        std::string error;
                        if (particle_asset::Load(path, &loaded, &error))
                            context.scene->SetSelectedParticleAsset(path, loaded, false);
                        else if (context.log) context.log->Error("Particle asset: " + error);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            changed |= ImGui::Checkbox("Enabled##ParticleSystem", &enabled);
            if (ImGui::TreeNodeEx("Playback", ImGuiTreeNodeFlags_DefaultOpen)) {
                changed |= ImGui::Checkbox("Autoplay##Particles", &settings.autoplay);
                ImGui::SameLine();
                changed |= ImGui::Checkbox("Loop##Particles", &settings.loop);
                ImGui::SameLine();
                changed |= ImGui::Checkbox("Prewarm##Particles", &settings.prewarm);
                ImGui::SameLine();
                changed |= ImGui::Checkbox("Local Space", &settings.localSpace);
                int simulationBackend = static_cast<int>(settings.config.simulationBackend);
                const char* simulationBackends[] = {"Auto", "CPU", "GPU Compute"};
                if (ImGui::Combo("Simulation Backend##Particles", &simulationBackend,
                                 simulationBackends, 3)) {
                    settings.config.simulationBackend =
                        static_cast<engine::ParticleSimulationBackend>(simulationBackend);
                    changed = true;
                }
                ImGui::TextDisabled("GPU compute requires OpenGL 4.3 and a supported feature set.");
                changed |= ImGui::DragFloat("Duration", &settings.duration, 0.1f, 0.0f, 10000.0f, "%.2f s");
                changed |= ImGui::DragFloat("Start Delay", &settings.startDelay, 0.05f, 0.0f, 10000.0f, "%.2f s");
                changed |= ImGui::DragFloat("Simulation Speed", &settings.simulationSpeed, 0.05f, 0.0f, 20.0f, "%.2fx");
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Emission", ImGuiTreeNodeFlags_DefaultOpen)) {
                changed |= ImGui::DragFloat("Rate", &settings.config.rate, 1.0f, 0.0f, 100000.0f, "%.1f /s");
                changed |= ImGui::DragInt("Maximum", &settings.config.maxParticles, 10.0f, 1, 1000000);
                changed |= ImGui::DragInt("Burst Count", &settings.burstCount, 1.0f, 0, 1000000);
                changed |= ImGui::DragFloat("Burst Interval", &settings.burstInterval, 0.05f, 0.0f, 10000.0f, "%.2f s");
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Shape", ImGuiTreeNodeFlags_DefaultOpen)) {
                int shape = static_cast<int>(settings.config.shape);
                const char* shapes[] = {"Point", "Sphere", "Cone"};
                if (ImGui::Combo("Emit Shape", &shape, shapes, 3)) {
                    settings.config.shape = static_cast<engine::EmitShape>(shape);
                    changed = true;
                }
                changed |= ImGui::DragFloat("Shape Radius", &settings.config.shapeRadius, 0.01f, 0.0f, 10000.0f);
                changed |= ImGui::DragFloat3("Direction##Particles", &settings.config.direction.x, 0.02f);
                if (settings.config.shape == engine::EmitShape::Cone)
                    changed |= ImGui::DragFloat("Cone Angle", &settings.config.coneAngleDeg, 0.5f, 0.0f, 180.0f, "%.1f deg");
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Motion", ImGuiTreeNodeFlags_DefaultOpen)) {
                changed |= ImGui::DragFloatRange2("Speed", &settings.config.speedMin,
                    &settings.config.speedMax, 0.05f, -10000.0f, 10000.0f, "Min %.2f", "Max %.2f");
                changed |= ImGui::DragFloatRange2("Lifetime", &settings.config.lifeMin,
                    &settings.config.lifeMax, 0.05f, 0.001f, 10000.0f, "Min %.2f", "Max %.2f");
                changed |= ImGui::DragFloat3("Gravity##Particles", &settings.config.gravity.x, 0.05f);
                changed |= ImGui::DragFloat("Drag##Particles", &settings.config.drag, 0.02f, 0.0f, 1000.0f);
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Collision", ImGuiTreeNodeFlags_DefaultOpen)) {
                changed |= ImGui::Checkbox("Enable Particle Collision", &settings.config.collisionEnabled);
                if (settings.config.collisionEnabled) {
                    int response = static_cast<int>(settings.config.collisionResponse);
                    const char* responses[] = {"Bounce", "Kill"};
                    if (ImGui::Combo("Response", &response, responses, 2)) {
                        settings.config.collisionResponse =
                            static_cast<engine::ParticleCollisionResponse>(response);
                        changed = true;
                    }
                    changed |= ImGui::DragFloat("Collision Radius", &settings.config.collisionRadius,
                        0.005f, 0.0f, 100.0f);
                    if (settings.config.collisionResponse == engine::ParticleCollisionResponse::Bounce) {
                        changed |= ImGui::SliderFloat("Bounce", &settings.config.collisionBounce, 0.0f, 2.0f);
                        changed |= ImGui::SliderFloat("Collision Friction", &settings.config.collisionFriction, 0.0f, 1.0f);
                        changed |= ImGui::SliderFloat("Lifetime Loss", &settings.config.collisionLifetimeLoss, 0.0f, 1.0f, "%.0f%%");
                    }
                    ImGui::TextDisabled("Plane, sphere and box are exact; other colliders use their bounds.");
                }
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Trails / Ribbons", ImGuiTreeNodeFlags_DefaultOpen)) {
                changed |= ImGui::Checkbox("Enable Trails", &settings.config.trailsEnabled);
                if (settings.config.trailsEnabled) {
                    changed |= ImGui::SliderInt("Trail Segments", &settings.config.trailSegments, 2, 16);
                    changed |= ImGui::DragFloat("Trail Length", &settings.config.trailLength, 0.05f, 0.001f, 1000.0f);
                    changed |= ImGui::DragFloat("Trail Width", &settings.config.trailWidth, 0.01f, 0.0f, 100.0f);
                    changed |= ImGui::SliderFloat("Trail Opacity", &settings.config.trailOpacity, 0.0f, 1.0f);
                    ImGui::TextDisabled("Ribbons face the camera and taper along the motion history.");
                }
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
                int renderMode = static_cast<int>(settings.config.renderMode);
                const char* renderModes[] = {"Billboard", "Mesh"};
                if (ImGui::Combo("Render Mode##Particles", &renderMode, renderModes, 2)) {
                    settings.config.renderMode = static_cast<engine::ParticleRenderMode>(renderMode);
                    changed = true;
                }
                if (settings.config.renderMode == engine::ParticleRenderMode::Mesh) {
                    int meshShape = static_cast<int>(settings.config.meshShape);
                    const char* meshShapes[] = {"Cube", "Sphere", "Cone", "Cylinder", "Model Asset"};
                    if (ImGui::Combo("Particle Mesh", &meshShape, meshShapes, 5)) {
                        settings.config.meshShape = static_cast<engine::ParticleMeshShape>(meshShape);
                        changed = true;
                    }
                    changed |= ImGui::DragFloat("Mesh Scale##Particles", &settings.config.meshScale,
                                                0.01f, 0.001f, 1000.0f);
                    changed |= ImGui::Checkbox("Align To Velocity##Particles",
                                               &settings.config.meshAlignToVelocity);
                    if (settings.config.meshShape == engine::ParticleMeshShape::Model) {
                        std::array<char, 512> particleMesh{};
                        std::snprintf(particleMesh.data(), particleMesh.size(), "%s",
                                      settings.config.meshPath.c_str());
                        if (ImGui::InputText("Model Asset##Particles", particleMesh.data(),
                                             particleMesh.size())) {
                            settings.config.meshPath = particleMesh.data(); changed = true;
                        }
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("3DGEDITOR_ASSET")) {
                                const char* path = static_cast<const char*>(payload->Data);
                                if (path && *path) { settings.config.meshPath = path; changed = true; }
                            }
                            ImGui::EndDragDropTarget();
                        }
                        ImGui::TextDisabled("Drag a model from Assets. Failed models fall back to a cube.");
                    }
                }
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
                changed |= ImGui::ColorEdit4("Start Color", &settings.config.startColor.x,
                    ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
                changed |= ImGui::ColorEdit4("End Color", &settings.config.endColor.x,
                    ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
                changed |= ImGui::DragFloat("Start Size", &settings.config.startSize, 0.01f, 0.0f, 10000.0f);
                changed |= ImGui::DragFloat("End Size", &settings.config.endSize, 0.01f, 0.0f, 10000.0f);
                changed |= ImGui::DragFloatRange2("Start Rotation", &settings.config.rotationMinDeg,
                    &settings.config.rotationMaxDeg, 1.0f, -360.0f, 360.0f, "Min %.0f", "Max %.0f");
                changed |= ImGui::DragFloatRange2("Rotation Speed", &settings.config.angularVelocityMinDeg,
                    &settings.config.angularVelocityMaxDeg, 1.0f, -2000.0f, 2000.0f, "Min %.0f", "Max %.0f");
                int blend = static_cast<int>(settings.config.blend);
                const char* blends[] = {"Additive", "Alpha"};
                if (ImGui::Combo("Blend", &blend, blends, 2)) {
                    settings.config.blend = static_cast<engine::ParticleBlend>(blend);
                    changed = true;
                }
                changed |= ImGui::Checkbox("Size Curve##Particles", &settings.config.useSizeCurve);
                if (settings.config.useSizeCurve)
                    changed |= ImGui::SliderFloat4("Size Keys", settings.config.sizeCurve.data(), 0.0f, 1.0f, "%.2f");
                changed |= ImGui::Checkbox("Color Curve##Particles", &settings.config.useColorCurve);
                if (settings.config.useColorCurve)
                    changed |= ImGui::SliderFloat4("Color Keys", settings.config.colorCurve.data(), 0.0f, 1.0f, "%.2f");

                std::array<char, 512> particleTexture{};
                std::snprintf(particleTexture.data(), particleTexture.size(), "%s",
                              settings.config.texturePath.c_str());
                if (ImGui::InputText("Texture##Particles", particleTexture.data(), particleTexture.size())) {
                    settings.config.texturePath = particleTexture.data(); changed = true;
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("3DGEDITOR_ASSET")) {
                        const char* path = static_cast<const char*>(payload->Data);
                        if (path && *path) { settings.config.texturePath = path; changed = true; }
                    }
                    ImGui::EndDragDropTarget();
                }
                changed |= ImGui::DragInt("Texture Columns", &settings.config.textureColumns, 1.0f, 1, 64);
                changed |= ImGui::DragInt("Texture Rows", &settings.config.textureRows, 1.0f, 1, 64);
                changed |= ImGui::DragFloat("Texture FPS", &settings.config.textureFps, 0.5f, 0.0f, 240.0f);
                changed |= ImGui::Checkbox("Loop Flipbook", &settings.config.textureLoop);
                changed |= ImGui::Checkbox("Frustum Culling", &settings.config.cullingEnabled);
                changed |= ImGui::DragFloat("Bounds Radius", &settings.config.boundsRadius,
                                            0.05f, 0.01f, 10000.0f);
                ImGui::TreePop();
            }
            ImGui::TextUnformatted("Presets:");
            ImGui::SameLine();
            for (int i = 0; i < 5; ++i) {
                if (i > 0) ImGui::SameLine();
                const auto preset = static_cast<ParticlePreset>(i);
                if (ImGui::SmallButton(ParticlePresetName(preset))) {
                    settings = MakeParticlePreset(preset);
                    enabled = true;
                    context.scene->SetSelectedParticleSystem(true, settings);
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset")) {
                settings = engine::ParticleSystemComponent{};
                enabled = true;
                context.scene->SetSelectedParticleSystem(true, settings);
            }
            if (changed) {
                if (ImGui::IsAnyItemActive()) context.scene->BeginParticleEdit();
                context.scene->SetSelectedParticleSystem(enabled, settings);
            }
            if (!ImGui::IsAnyItemActive()) context.scene->EndParticleEdit();
            for (const std::string& warning : ValidateParticleSettings(settings))
                ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.2f, 1.0f), "! %s", warning.c_str());
            if (ImGui::Button("Remove Particle System"))
                context.scene->SetSelectedParticleSystem(false, settings);
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
                    case engine::ecs::ColliderShape::Cylinder:
                    case engine::ecs::ColliderShape::Cone:
                        collider.radius = std::max(std::max(s.x, s.z) * 0.5f, 0.001f);
                        collider.halfHeight = std::max(s.y * 0.5f, 0.001f);
                        collider.halfExtents = glm::vec3(collider.radius, collider.halfHeight, collider.radius);
                        break;
                    case engine::ecs::ColliderShape::Pyramid:
                    case engine::ecs::ColliderShape::Staircase:
                        collider.halfExtents = glm::max(s * 0.5f, glm::vec3(0.001f));
                        break;
                    case engine::ecs::ColliderShape::Torus:
                        collider.minorRadius = std::max(s.y * 0.5f, 0.001f);
                        collider.majorRadius = std::max(std::max(s.x, s.z) * 0.5f - collider.minorRadius, 0.001f);
                        collider.halfExtents = glm::vec3(collider.majorRadius + collider.minorRadius,
                            collider.minorRadius, collider.majorRadius + collider.minorRadius);
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
            if (ImGui::Checkbox("Kinematic", &rigidBody.kinematic)) {
                context.scene->SetSelectedRigidBody(rigidBody);
            }
            if (rigidBody.kinematic && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Moved by its velocity only (script/animation). Pushes dynamics; never pushed, never sleeps.");
            }
        }

        bool colliderEnabled = selected->colliderEnabled;
        if (ImGui::Checkbox("Collider", &colliderEnabled)) {
            context.scene->SetSelectedColliderEnabled(colliderEnabled);
        }
        if (colliderEnabled) {
            engine::ecs::Collider collider = selected->collider;
            int shapeIndex = ColliderShapeIndex(collider.shape);
            const char* shapes[] = {"Sphere", "Box", "Plane", "Capsule", "Cylinder", "Cone", "Pyramid", "Torus", "Staircase"};
            if (ImGui::Combo("Collider Shape", &shapeIndex, shapes, 9)) {
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
            } else if (collider.shape == engine::ecs::ColliderShape::Cylinder
                || collider.shape == engine::ecs::ColliderShape::Cone) {
                bool changed = false;
                changed |= ImGui::DragFloat("Radius", &collider.radius, 0.02f, 0.001f, 1000.0f);
                changed |= ImGui::DragFloat("Half Height", &collider.halfHeight, 0.02f, 0.001f, 1000.0f);
                if (changed) {
                    collider.halfExtents = glm::vec3(collider.radius, collider.halfHeight, collider.radius);
                    context.scene->SetSelectedCollider(collider);
                }
            } else if (collider.shape == engine::ecs::ColliderShape::Pyramid
                || collider.shape == engine::ecs::ColliderShape::Staircase) {
                bool changed = ImGui::DragFloat3("Half Extents", &collider.halfExtents.x, 0.02f, 0.001f, 1000.0f);
                if (collider.shape == engine::ecs::ColliderShape::Staircase)
                    changed |= ImGui::DragInt("Steps", &collider.steps, 0.1f, 1, 32);
                if (changed) context.scene->SetSelectedCollider(collider);
            } else if (collider.shape == engine::ecs::ColliderShape::Torus) {
                bool changed = false;
                changed |= ImGui::DragFloat("Major Radius", &collider.majorRadius, 0.02f, 0.001f, 1000.0f);
                changed |= ImGui::DragFloat("Minor Radius", &collider.minorRadius, 0.02f, 0.001f, 1000.0f);
                if (changed) {
                    const float outer = collider.majorRadius + collider.minorRadius;
                    collider.halfExtents = glm::vec3(outer, collider.minorRadius, outer);
                    context.scene->SetSelectedCollider(collider);
                }
            } else if (collider.shape == engine::ecs::ColliderShape::Plane) {
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

            // Collision filtering. Layer = which group this collider is in; Mask =
            // which groups it collides with. Edited as bitfields (0..31 checkboxes
            // would be noisy, so expose the raw hex-ish integers).
            int layer = static_cast<int>(collider.layer);
            int mask  = static_cast<int>(collider.mask);
            if (ImGui::InputInt("Layer bits", &layer, 1, 16)) {
                collider.layer = static_cast<std::uint32_t>(std::max(layer, 0));
                context.scene->SetSelectedCollider(collider);
            }
            if (ImGui::InputInt("Collides-with mask", &mask, 1, 16)) {
                collider.mask = static_cast<std::uint32_t>(std::max(mask, 0));
                context.scene->SetSelectedCollider(collider);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Bitfields: two colliders touch only if each side's mask includes the other's layer bit.");
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
            if (!player.firstPerson && ImGui::TreeNodeEx("Camera Collision",
                    ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
                changed |= ImGui::Checkbox("Collision Enabled", &player.cameraCollision);
                ImGui::BeginDisabled(!player.cameraCollision);
                changed |= ImGui::DragFloat("Probe Radius", &player.cameraProbeRadius,
                                            0.01f, 0.0f, 5.0f);
                changed |= ImGui::DragFloat("Wall Padding", &player.cameraCollisionPadding,
                                            0.005f, 0.0f, 2.0f);
                changed |= ImGui::DragFloat("Return Speed", &player.cameraReturnSpeed,
                                            0.1f, 0.0f, 100.0f);
                ImGui::TextDisabled("Retracts immediately; Return Speed smooths recovery.");
                ImGui::EndDisabled();
                ImGui::TreePop();
            }
            if (!player.firstPerson && ImGui::TreeNodeEx("Shoulder Camera",
                    ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
                changed |= ImGui::Checkbox("Shoulder Camera Enabled", &player.shoulderCamera);
                ImGui::BeginDisabled(!player.shoulderCamera);
                changed |= ImGui::DragFloat("Shoulder Offset", &player.shoulderOffset,
                                            0.01f, 0.0f, 5.0f);
                changed |= ImGui::DragFloat("Shoulder Switch Speed", &player.shoulderSwitchSpeed,
                                            0.1f, 0.0f, 100.0f);
                changed |= ImGui::Checkbox("Start On Right Shoulder", &player.rightShoulder);
                ImGui::TextDisabled("Press Q in Play mode to switch shoulders.");
                ImGui::EndDisabled();
                ImGui::TreePop();
            }
            if (!player.firstPerson && ImGui::TreeNodeEx("Lock-On Targeting",
                    ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
                changed |= ImGui::Checkbox("Lock-On Enabled", &player.lockOnEnabled);
                ImGui::BeginDisabled(!player.lockOnEnabled);
                changed |= ImGui::DragFloat("Lock-On Range", &player.lockOnRange,
                                            0.25f, 0.0f, 1000.0f);
                changed |= ImGui::SliderFloat("Acquisition View Angle", &player.lockOnViewAngle,
                                              0.0f, 180.0f, "%.1f deg");
                changed |= ImGui::DragFloat("Target Height", &player.lockOnTargetHeight,
                                            0.01f, -100.0f, 100.0f);
                changed |= ImGui::DragFloat("Tracking Speed", &player.lockOnTrackingSpeed,
                                            0.1f, 0.0f, 100.0f);
                ImGui::TextDisabled("Press T in Play mode to lock or release a living Health target.");
                ImGui::EndDisabled();
                ImGui::TreePop();
            }
            changed |= ImGui::DragFloat("Max Slope", &player.maxSlopeDegrees, 0.5f, 0.0f, 89.0f);
            changed |= ImGui::DragFloat("Step Height", &player.stepHeight, 0.01f, 0.0f, 10.0f);
            player.capsuleHeight = std::max(player.capsuleHeight, player.capsuleRadius * 2.0f);
            if (changed) {
                context.scene->SetSelectedPlayerController(player);
            }
        }

        bool cameraZoneEnabled = selected->cameraZoneEnabled;
        if (ImGui::Checkbox("Camera Zone", &cameraZoneEnabled)) {
            context.scene->SetSelectedCameraZone(
                cameraZoneEnabled,
                selected->cameraZonePresetName,
                selected->cameraZoneRestoreOnExit,
                selected->cameraZonePriority,
                selected->cameraZoneReturnBlend);
        }
        if (cameraZoneEnabled) {
            std::string presetName = selected->cameraZonePresetName;
            bool restoreOnExit = selected->cameraZoneRestoreOnExit;
            int priority = selected->cameraZonePriority;
            float returnBlend = selected->cameraZoneReturnBlend;
            bool changed = false;

            const char* preview = presetName.empty() ? "Choose camera..." : presetName.c_str();
            if (ImGui::BeginCombo("Zone Camera", preview)) {
                for (const EditorScene::CameraPreset& camera : context.scene->CameraPresets()) {
                    const bool selectedCamera = camera.name == presetName;
                    if (ImGui::Selectable(camera.name.c_str(), selectedCamera)) {
                        presetName = camera.name;
                        changed = true;
                    }
                    if (selectedCamera) ImGui::SetItemDefaultFocus();
                }
                if (context.scene->CameraPresets().empty()) {
                    ImGui::TextDisabled("Create a camera in Camera Manager first.");
                }
                ImGui::EndCombo();
            }
            changed |= ImGui::Checkbox("Restore Camera On Exit", &restoreOnExit);
            changed |= ImGui::DragInt("Zone Priority", &priority, 1.0f, -1000, 1000);
            changed |= ImGui::DragFloat("Exit Blend", &returnBlend, 0.05f, 0.0f, 30.0f, "%.2f s");
            if (!selected->colliderEnabled || !selected->collider.isTrigger) {
                ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.2f, 1.0f),
                                   "Camera Zone requires a trigger collider.");
                if (ImGui::Button("Configure Trigger Collider")) changed = true;
            }
            if (changed) {
                context.scene->SetSelectedCameraZone(
                    true, presetName, restoreOnExit, priority, returnBlend);
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
            engine::ecs::AudioAction enterAudioAction = selected->triggerEnterAudioAction;
            engine::ecs::AudioAction exitAudioAction = selected->triggerExitAudioAction;
            engine::ParticleAction enterParticleAction = selected->triggerEnterParticleAction;
            engine::ParticleAction exitParticleAction = selected->triggerExitParticleAction;
            const char* preview = targetName.empty() ? "None" : targetName.c_str();
            if (ImGui::BeginCombo("Trigger Target", preview)) {
                if (ImGui::Selectable("None", targetName.empty())) {
                    targetName.clear();
                    context.scene->SetSelectedTriggerAction(targetName,
                        enterMoverAction,
                        enterRotatorAction,
                        exitMoverAction,
                        exitRotatorAction,
                        enterAudioAction,
                        exitAudioAction,
                        enterParticleAction,
                        exitParticleAction);
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
                            exitRotatorAction,
                            enterAudioAction,
                            exitAudioAction,
                            enterParticleAction,
                            exitParticleAction);
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
            changed |= DrawAudioActionCombo("Enter Audio", &enterAudioAction);
            changed |= DrawAudioActionCombo("Exit Audio", &exitAudioAction);
            changed |= DrawParticleActionCombo("Enter Particles", &enterParticleAction);
            changed |= DrawParticleActionCombo("Exit Particles", &exitParticleAction);
            if ((enterAudioAction != engine::ecs::AudioAction::None
                 || exitAudioAction != engine::ecs::AudioAction::None)
                && !targetName.empty()) {
                const EditorScene::Object* targetObject = nullptr;
                for (const EditorScene::Object& object : context.scene->Objects()) {
                    if (object.name == targetName) { targetObject = &object; break; }
                }
                if (!targetObject || !targetObject->audioSourceEnabled) {
                    ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
                        "Target has no Audio Source component.");
                }
            }
            if ((enterParticleAction != engine::ParticleAction::None
                 || exitParticleAction != engine::ParticleAction::None)
                && !targetName.empty()) {
                const EditorScene::Object* targetObject = nullptr;
                for (const EditorScene::Object& object : context.scene->Objects()) {
                    if (object.name == targetName) { targetObject = &object; break; }
                }
                if (!targetObject || !targetObject->particleSystemEnabled) {
                    ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
                        "Target has no Particle System component.");
                }
            }
            if (changed) {
                context.scene->SetSelectedTriggerAction(targetName,
                    enterMoverAction,
                    enterRotatorAction,
                    exitMoverAction,
                    exitRotatorAction,
                    enterAudioAction,
                    exitAudioAction,
                    enterParticleAction,
                    exitParticleAction);
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Cinematic Trigger");
            std::string sequenceName = selected->triggerCameraSequenceName;
            auto enterCameraAction = selected->triggerEnterCameraAction;
            auto exitCameraAction = selected->triggerExitCameraAction;
            bool lockInput = selected->triggerCameraLockInput;
            bool skippable = selected->triggerCameraSkippable;
            bool cameraChanged = false;
            const char* sequencePreview = sequenceName.empty()
                ? "Choose sequence..." : sequenceName.c_str();
            if (ImGui::BeginCombo("Cinematic Sequence", sequencePreview)) {
                if (ImGui::Selectable("None", sequenceName.empty())) {
                    sequenceName.clear();
                    cameraChanged = true;
                }
                for (const EditorScene::CameraSequence& sequence
                     : context.scene->CameraSequences()) {
                    const bool isSelected = sequence.name == sequenceName;
                    if (ImGui::Selectable(sequence.name.c_str(), isSelected)) {
                        sequenceName = sequence.name;
                        cameraChanged = true;
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            cameraChanged |= DrawCameraSequenceTriggerActionCombo(
                "Enter Cinematic", &enterCameraAction);
            cameraChanged |= DrawCameraSequenceTriggerActionCombo(
                "Exit Cinematic", &exitCameraAction);
            cameraChanged |= ImGui::Checkbox("Lock Player Input", &lockInput);
            cameraChanged |= ImGui::Checkbox("Allow Enter To Skip", &skippable);
            if (cameraChanged) {
                context.scene->SetSelectedTriggerCameraSequence(
                    sequenceName, enterCameraAction, exitCameraAction,
                    lockInput, skippable);
            }
            if ((enterCameraAction == EditorScene::CameraSequenceTriggerAction::Play
                 || exitCameraAction == EditorScene::CameraSequenceTriggerAction::Play)
                && sequenceName.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
                                   "Choose a sequence for the Play action.");
            }
        }
    }

    if (ImGui::CollapsingHeader("AI Agent", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool  enabled  = selected->navAgentEnabled;
        float speed    = selected->navAgentSpeed;
        float maxForce = selected->navAgentMaxForce;
        float reach    = selected->navAgentReachRadius;
        float repath   = selected->navAgentRepathInterval;
        std::string targetName = selected->navAgentTargetName;
        float visionRange = selected->navAgentVisionRange;
        float visionHalfAngle = selected->navAgentVisionHalfAngle;
        bool changed = false;
        changed |= ImGui::Checkbox("Nav Agent", &enabled);
        if (enabled) {
            changed |= ImGui::DragFloat("Speed", &speed, 0.05f, 0.0f, 50.0f, "%.2f");
            changed |= ImGui::DragFloat("Max Force", &maxForce, 0.1f, 0.0f, 200.0f, "%.1f");
            changed |= ImGui::DragFloat("Reach Radius", &reach, 0.02f, 0.05f, 10.0f, "%.2f");
            changed |= ImGui::DragFloat("Repath (s)", &repath, 0.02f, 0.05f, 5.0f, "%.2f");

            // Chase target: an object the agent pursues when it can see it.
            const char* targetPreview = targetName.empty() ? "None (patrol only)" : targetName.c_str();
            if (ImGui::BeginCombo("Chase Target", targetPreview)) {
                if (ImGui::Selectable("None (patrol only)", targetName.empty())) {
                    targetName.clear();
                    changed = true;
                }
                for (const EditorScene::Object& object : context.scene->Objects()) {
                    if (object.name == selected->name) {
                        continue;
                    }
                    const bool isTarget = object.name == targetName;
                    if (ImGui::Selectable(object.name.c_str(), isTarget)) {
                        targetName = object.name;
                        changed = true;
                    }
                    if (isTarget) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            if (!targetName.empty()) {
                changed |= ImGui::DragFloat("Vision Range", &visionRange, 0.1f, 0.0f, 100.0f, "%.1f");
                changed |= ImGui::DragFloat("Vision Half-Angle", &visionHalfAngle, 0.5f, 1.0f, 180.0f, "%.0f deg");
            }

            // Faction targeting: team id + auto-acquire nearest hostile.
            int  team = selected->navAgentTeam;
            bool autoTarget = selected->navAgentAutoTarget;
            bool teamChanged = false;
            teamChanged |= ImGui::DragInt("Team (0 = neutral)", &team, 0.1f, 0, 32);
            teamChanged |= ImGui::Checkbox("Auto-target nearest hostile", &autoTarget);
            if (autoTarget) {
                ImGui::TextDisabled("Chases the nearest agent on a different non-zero team.");
            }
            if (teamChanged) {
                context.scene->SetSelectedNavAgentTeam(team, autoTarget);
            }
        }
        if (changed) {
            context.scene->SetSelectedNavAgent(enabled, speed, maxForce, reach, repath,
                                               targetName, visionRange, visionHalfAngle);
        }
        if (enabled) {
            ImGui::Text("Patrol points: %d", static_cast<int>(selected->patrolPoints.size()));
            if (ImGui::Button("Add Waypoint (object pos)")) {
                if (const engine::ecs::Transform* t = context.scene->TryGetTransform(selected->entity)) {
                    context.scene->AddSelectedPatrolPoint(t->position);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Waypoints")) {
                context.scene->ClearSelectedPatrolPoints();
            }
            ImGui::TextDisabled("Move the object between adds to build a patrol loop, then Play.");

            // Behaviour-tree brain: an optional .btgraph asset that overrides the
            // built-in patrol/chase/search brain at Play time. Author it in the
            // Behavior Graph panel (F11), then drag it here from the Content browser.
            ImGui::Separator();
            const std::string& brain = selected->navAgentBrainAsset;
            ImGui::Text("Brain: %s", brain.empty() ? "Built-in (patrol/chase/search)" : brain.c_str());
            ImGui::Button(brain.empty() ? "Drop .btgraph here" : "Replace brain (drop .btgraph)");
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("3DGEDITOR_ASSET")) {
                    const std::string dropped(static_cast<const char*>(payload->Data),
                                              static_cast<std::size_t>(payload->DataSize));
                    const std::string trimmed = dropped.c_str();   // stop at first NUL if padded
                    if (trimmed.size() >= 9 && trimmed.substr(trimmed.size() - 9) == ".btgraph") {
                        context.scene->SetSelectedNavAgentBrain(trimmed);
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (!brain.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Clear Brain")) {
                    context.scene->SetSelectedNavAgentBrain(std::string());
                }
            }
        }
    }

    // Terrain controls only appear for terrain objects (created via Add > Terrain),
    // so an ordinary mesh can't be accidentally converted into terrain.
    if (selected->isTerrain && ImGui::CollapsingHeader("Terrain", ImGuiTreeNodeFlags_DefaultOpen)) {
        int   res = selected->terrainRes;
        float size = selected->terrainSize;
        float maxHeight = selected->terrainMaxHeight;
        int   seed = selected->terrainSeed;
        int   octaves = selected->terrainOctaves;
        float frequency = selected->terrainFrequency;
        bool changed = false;
        {
            changed |= ImGui::DragInt("Resolution", &res, 1.0f, 8, 512);
            changed |= ImGui::DragFloat("Size (m)", &size, 0.5f, 1.0f, 2000.0f, "%.1f");
            changed |= ImGui::DragFloat("Max Height", &maxHeight, 0.1f, 0.0f, 500.0f, "%.1f");
            changed |= ImGui::DragInt("Seed", &seed, 1.0f, 0, 1000000);
            changed |= ImGui::DragInt("Octaves", &octaves, 0.1f, 1, 10);
            changed |= ImGui::DragFloat("Frequency", &frequency, 0.05f, 0.1f, 16.0f, "%.2f");
            if (ImGui::Button("Randomize Seed")) {
                seed = static_cast<int>((static_cast<unsigned>(seed) * 1103515245u + 12345u) & 0x7fffffffu);
                changed = true;
            }
            ImGui::TextDisabled("Regenerates on change. Positioned by the object's Transform; "
                                "grass/rock/snow colouring is by height + slope.");

            // Sculpting: brush tools that edit the heightmap directly in the viewport.
            if (context.terrainSculpt && context.terrainSculptMode &&
                context.terrainBrushRadius && context.terrainBrushStrength) {
                ImGui::Separator();
                ImGui::Checkbox("Sculpt mode (left-drag in viewport)", context.terrainSculpt);
                if (*context.terrainSculpt) {
                    const char* modes[] = {"Raise", "Lower", "Smooth", "Flatten", "Paint"};
                    ImGui::Combo("Brush", context.terrainSculptMode, modes, IM_ARRAYSIZE(modes));
                    if (*context.terrainSculptMode == 4 && context.terrainPaintLayer) {
                        const char* layers[] = {"Erase (auto)", "Grass", "Rock", "Dirt", "Snow", "Sand"};
                        ImGui::Combo("Layer", context.terrainPaintLayer, layers, IM_ARRAYSIZE(layers));
                    }
                    ImGui::DragFloat("Brush Radius", context.terrainBrushRadius, 0.1f, 0.5f, 100.0f, "%.1f");
                    if (*context.terrainSculptMode != 4) {
                        ImGui::DragFloat("Brush Strength", context.terrainBrushStrength, 0.1f, 0.1f, 50.0f, "%.1f");
                    }
                    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f),
                                       "Editing: object selection/move is paused.");
                }
            }
        }
        if (changed) {
            context.scene->SetSelectedTerrain(true, res, size, maxHeight, seed, octaves, frequency);
        }
    }

    if (selected->isWater && ImGui::CollapsingHeader("Water", ImGuiTreeNodeFlags_DefaultOpen)) {
        float size = selected->waterSize;
        int   res = selected->waterResolution;
        float level = selected->waterLevel;
        glm::vec3 shallow = selected->waterShallow;
        glm::vec3 deep = selected->waterDeep;
        glm::vec3 refl = selected->waterReflection;
        float transparency = selected->waterTransparency;
        float fresnel = selected->waterFresnel;
        float spec = selected->waterSpecular;
        float shininess = selected->waterShininess;
        bool changed = false;
        changed |= ImGui::DragFloat("Size (m)", &size, 0.5f, 1.0f, 4000.0f, "%.1f");
        changed |= ImGui::DragInt("Resolution", &res, 1.0f, 8, 512);
        changed |= ImGui::DragFloat("Surface Level (Y)", &level, 0.05f, -1000.0f, 1000.0f, "%.2f");
        changed |= ImGui::ColorEdit3("Shallow", &shallow.x);
        changed |= ImGui::ColorEdit3("Deep", &deep.x);
        changed |= ImGui::ColorEdit3("Reflection (sky)", &refl.x);
        changed |= ImGui::DragFloat("Transparency", &transparency, 0.01f, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::DragFloat("Fresnel Power", &fresnel, 0.05f, 0.1f, 12.0f, "%.2f");
        changed |= ImGui::DragFloat("Specular", &spec, 0.02f, 0.0f, 8.0f, "%.2f");
        changed |= ImGui::DragFloat("Shininess", &shininess, 1.0f, 1.0f, 1000.0f, "%.0f");
        ImGui::TextDisabled("Animated Gerstner waves. Centre = object Transform XZ; level = calm Y.");
        if (changed) {
            context.scene->SetSelectedWater(size, res, level, shallow, deep, refl,
                                            transparency, fresnel, spec, shininess);
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
        if (ImGui::IsItemHovered()
            && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
            && asset.type == EditorAssets::Type::Scene) {
            context.sceneAssetOpenRequested =
                (std::filesystem::path(context.assets->RootPath())
                    / asset.relativePath).string();
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

void DrawAudioEditor(EditorDockspace::Context& context, bool* open) {
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::AudioEditor), open)) {
        ImGui::End();
        return;
    }

    DrawAudioCueAuthoring(context);
    DrawAdaptiveMusicAuthoring(context);
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Create Sound", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* generators[] = {"Sine Tone", "Square Tone", "Noise"};
        ImGui::Combo("Generator", &g_audioEditor.generator, generators, 3);
        if (g_audioEditor.generator != 2) {
            ImGui::DragFloat("Frequency", &g_audioEditor.generatorFrequency, 1.0f, 20.0f, 20000.0f, "%.0f Hz");
        }
        ImGui::DragFloat("Duration", &g_audioEditor.generatorDuration, 0.05f, 0.05f, 30.0f, "%.2f s");
        ImGui::SliderFloat("Amplitude", &g_audioEditor.generatorAmplitude, 0.0f, 1.0f, "%.2f");
        if (ImGui::Button("Generate New Clip")) {
            GenerateAudioEditorClip();
            if (context.assets) {
                const std::filesystem::path output = std::filesystem::path(context.assets->RootPath())
                    / "Audio" / "generated_sound.wav";
                std::snprintf(g_audioEditor.outputPath.data(), g_audioEditor.outputPath.size(), "%s", output.string().c_str());
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Selected Audio")) {
            if (context.assets) {
                const EditorAssets::Asset* selected = context.assets->SelectedAsset();
                const std::string path = context.assets->SelectedAssetFullPath();
                if (selected && IsEditableAudioPath(path)) LoadAudioIntoEditor(path, context.log);
                else if (context.log) context.log->Warning("Audio Editor: select an audio asset first");
            }
        }
        ImGui::Button("Drop audio clip here", ImVec2(-1.0f, 0.0f));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("3DGEDITOR_ASSET")) {
                const std::string path(static_cast<const char*>(payload->Data));
                if (IsEditableAudioPath(path)) LoadAudioIntoEditor(path, context.log);
            }
            ImGui::EndDragDropTarget();
        }
    }

    if (g_audioEditor.buffer.Empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("Generate a sound or load an audio asset to begin editing.");
        ImGui::End();
        return;
    }

    ImGui::Separator();
    ImGui::Text("%s%s", g_audioEditor.sourcePath.empty() ? "Generated Clip" : g_audioEditor.sourcePath.c_str(),
        g_audioEditor.dirty ? " *" : "");
    ImGui::Text("%.2f seconds  |  %u Hz  |  %u channel%s  |  %d frames",
        g_audioEditor.buffer.DurationSeconds(), g_audioEditor.buffer.sampleRate,
        g_audioEditor.buffer.channels, g_audioEditor.buffer.channels == 1 ? "" : "s",
        static_cast<int>(g_audioEditor.buffer.FrameCount()));
    DrawAudioWaveform(g_audioEditor.buffer);

    const float duration = g_audioEditor.buffer.DurationSeconds();
    ImGui::SliderFloat("Waveform Zoom", &g_audioEditor.waveformZoom, 1.0f, 32.0f, "%.1fx",
                       ImGuiSliderFlags_Logarithmic);
    const float visibleDuration = duration / std::max(g_audioEditor.waveformZoom, 1.0f);
    ImGui::SliderFloat("Waveform Scroll", &g_audioEditor.waveformOffset, 0.0f,
        std::max(duration - visibleDuration, 0.001f), "%.2f s");
    g_audioEditor.selectionStart = std::clamp(g_audioEditor.selectionStart, 0.0f, duration);
    g_audioEditor.selectionEnd = std::clamp(g_audioEditor.selectionEnd, g_audioEditor.selectionStart, duration);
    ImGui::DragFloatRange2("Selection", &g_audioEditor.selectionStart, &g_audioEditor.selectionEnd,
        0.01f, 0.0f, duration, "Start %.2f s", "End %.2f s");
    const auto [selectionBegin, selectionEnd] = AudioSelectionFrames();
    const bool validSelection = selectionEnd > selectionBegin;

    if (ImGui::CollapsingHeader("Edit", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!validSelection) ImGui::BeginDisabled();
        if (ImGui::Button("Trim to Selection")) {
            PushAudioEditUndo();
            const std::size_t channels = g_audioEditor.buffer.channels;
            std::vector<float> trimmed(g_audioEditor.buffer.samples.begin() + selectionBegin * channels,
                g_audioEditor.buffer.samples.begin() + selectionEnd * channels);
            g_audioEditor.buffer.samples = std::move(trimmed);
            g_audioEditor.selectionStart = 0.0f;
            g_audioEditor.selectionEnd = g_audioEditor.buffer.DurationSeconds();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reverse Selection")) {
            PushAudioEditUndo();
            const std::size_t channels = g_audioEditor.buffer.channels;
            for (std::size_t left = selectionBegin, right = selectionEnd - 1; left < right; ++left, --right) {
                for (std::size_t channel = 0; channel < channels; ++channel)
                    std::swap(g_audioEditor.buffer.samples[left * channels + channel],
                              g_audioEditor.buffer.samples[right * channels + channel]);
            }
        }
        if (ImGui::Button("Copy")) {
            const std::size_t channels = g_audioEditor.buffer.channels;
            g_audioEditor.clipboard = {};
            g_audioEditor.clipboard.channels = g_audioEditor.buffer.channels;
            g_audioEditor.clipboard.sampleRate = g_audioEditor.buffer.sampleRate;
            g_audioEditor.clipboard.samples.assign(
                g_audioEditor.buffer.samples.begin() + selectionBegin * channels,
                g_audioEditor.buffer.samples.begin() + selectionEnd * channels);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cut")) {
            const std::size_t channels = g_audioEditor.buffer.channels;
            g_audioEditor.clipboard = {};
            g_audioEditor.clipboard.channels = g_audioEditor.buffer.channels;
            g_audioEditor.clipboard.sampleRate = g_audioEditor.buffer.sampleRate;
            g_audioEditor.clipboard.samples.assign(
                g_audioEditor.buffer.samples.begin() + selectionBegin * channels,
                g_audioEditor.buffer.samples.begin() + selectionEnd * channels);
            PushAudioEditUndo();
            g_audioEditor.buffer.samples.erase(
                g_audioEditor.buffer.samples.begin() + selectionBegin * channels,
                g_audioEditor.buffer.samples.begin() + selectionEnd * channels);
            g_audioEditor.selectionEnd = g_audioEditor.selectionStart;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            const std::size_t channels = g_audioEditor.buffer.channels;
            PushAudioEditUndo();
            g_audioEditor.buffer.samples.erase(
                g_audioEditor.buffer.samples.begin() + selectionBegin * channels,
                g_audioEditor.buffer.samples.begin() + selectionEnd * channels);
            g_audioEditor.selectionEnd = g_audioEditor.selectionStart;
        }
        ImGui::SameLine();
        if (ImGui::Button("Silence")) {
            PushAudioEditUndo();
            std::fill(g_audioEditor.buffer.samples.begin()
                    + selectionBegin * g_audioEditor.buffer.channels,
                g_audioEditor.buffer.samples.begin()
                    + selectionEnd * g_audioEditor.buffer.channels, 0.0f);
        }
        if (!g_audioEditor.clipboard.Empty()
            && g_audioEditor.clipboard.channels == g_audioEditor.buffer.channels
            && g_audioEditor.clipboard.sampleRate == g_audioEditor.buffer.sampleRate) {
            if (ImGui::Button("Paste at Selection Start")) {
                PushAudioEditUndo();
                const auto at = g_audioEditor.buffer.samples.begin()
                    + selectionBegin * g_audioEditor.buffer.channels;
                g_audioEditor.buffer.samples.insert(at, g_audioEditor.clipboard.samples.begin(),
                    g_audioEditor.clipboard.samples.end());
            }
        }
        ImGui::DragFloat("Gain", &g_audioEditor.gainDb, 0.1f, -60.0f, 24.0f, "%.1f dB");
        if (ImGui::Button("Apply Gain")) {
            PushAudioEditUndo();
            const float gain = std::pow(10.0f, g_audioEditor.gainDb / 20.0f);
            for (std::size_t i = selectionBegin * g_audioEditor.buffer.channels;
                 i < selectionEnd * g_audioEditor.buffer.channels; ++i)
                g_audioEditor.buffer.samples[i] = std::clamp(g_audioEditor.buffer.samples[i] * gain, -1.0f, 1.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Normalize")) {
            float peak = 0.0f;
            for (std::size_t i = selectionBegin * g_audioEditor.buffer.channels;
                 i < selectionEnd * g_audioEditor.buffer.channels; ++i)
                peak = std::max(peak, std::abs(g_audioEditor.buffer.samples[i]));
            if (peak > 0.000001f) {
                PushAudioEditUndo();
                const float gain = 0.98f / peak;
                for (std::size_t i = selectionBegin * g_audioEditor.buffer.channels;
                     i < selectionEnd * g_audioEditor.buffer.channels; ++i)
                    g_audioEditor.buffer.samples[i] *= gain;
            }
        }
        ImGui::DragFloat("Fade In", &g_audioEditor.fadeInSeconds, 0.01f, 0.0f, duration, "%.2f s");
        ImGui::DragFloat("Fade Out", &g_audioEditor.fadeOutSeconds, 0.01f, 0.0f, duration, "%.2f s");
        if (ImGui::Button("Apply Fades")) {
            PushAudioEditUndo();
            const std::size_t channels = g_audioEditor.buffer.channels;
            const std::size_t fadeInFrames = std::min(selectionEnd - selectionBegin,
                static_cast<std::size_t>(g_audioEditor.fadeInSeconds * g_audioEditor.buffer.sampleRate));
            const std::size_t fadeOutFrames = std::min(selectionEnd - selectionBegin,
                static_cast<std::size_t>(g_audioEditor.fadeOutSeconds * g_audioEditor.buffer.sampleRate));
            for (std::size_t frame = 0; frame < fadeInFrames; ++frame) {
                const float gain = fadeInFrames > 1 ? static_cast<float>(frame) / (fadeInFrames - 1) : 1.0f;
                for (std::size_t channel = 0; channel < channels; ++channel)
                    g_audioEditor.buffer.samples[(selectionBegin + frame) * channels + channel] *= gain;
            }
            for (std::size_t frame = 0; frame < fadeOutFrames; ++frame) {
                const float gain = fadeOutFrames > 1 ? 1.0f - static_cast<float>(frame) / (fadeOutFrames - 1) : 1.0f;
                const std::size_t target = selectionEnd - fadeOutFrames + frame;
                for (std::size_t channel = 0; channel < channels; ++channel)
                    g_audioEditor.buffer.samples[target * channels + channel] *= gain;
            }
        }
        if (!validSelection) ImGui::EndDisabled();
        ImGui::Separator();
        const bool hasUndo = !g_audioEditor.undoStack.empty();
        if (!hasUndo) ImGui::BeginDisabled();
        if (ImGui::Button("Undo")) {
            g_audioEditor.redoStack.push_back(g_audioEditor.buffer);
            g_audioEditor.buffer = std::move(g_audioEditor.undoStack.back());
            g_audioEditor.undoStack.pop_back();
            g_audioEditor.selectionStart = 0.0f;
            g_audioEditor.selectionEnd = g_audioEditor.buffer.DurationSeconds();
            g_audioEditor.dirty = true;
        }
        if (!hasUndo) ImGui::EndDisabled();
        ImGui::SameLine();
        const bool hasRedo = !g_audioEditor.redoStack.empty();
        if (!hasRedo) ImGui::BeginDisabled();
        if (ImGui::Button("Redo")) {
            g_audioEditor.undoStack.push_back(g_audioEditor.buffer);
            g_audioEditor.buffer = std::move(g_audioEditor.redoStack.back());
            g_audioEditor.redoStack.pop_back();
            g_audioEditor.selectionStart = 0.0f;
            g_audioEditor.selectionEnd = g_audioEditor.buffer.DurationSeconds();
            g_audioEditor.dirty = true;
        }
        if (!hasRedo) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Reset Original") && !g_audioEditor.original.Empty()) {
            PushAudioEditUndo();
            g_audioEditor.buffer = g_audioEditor.original;
            g_audioEditor.selectionStart = 0.0f;
            g_audioEditor.selectionEnd = g_audioEditor.buffer.DurationSeconds();
        }
    }

    if (ImGui::CollapsingHeader("Preview and Export", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Preview Edited Clip")) {
            std::string error;
            const std::filesystem::path previewPath = std::filesystem::temp_directory_path()
                / "3dg_audio_editor_preview.wav";
            if (engine::WriteAudioWav(previewPath.string(), g_audioEditor.buffer, &error)) {
                context.previewAudioRequested = true;
                context.previewAudioPath = previewPath.string();
                context.previewAudioVolume = 1.0f;
                context.previewAudioPitch = 1.0f;
                context.previewAudioSpatial = false;
                context.previewAudioLoop = false;
            } else if (context.log) context.log->Error("Audio Editor preview failed: " + error);
        }
        ImGui::SameLine();
        if (ImGui::Button("Preview Selection")) {
            const auto [begin, end] = AudioSelectionFrames();
            engine::AudioBuffer selection;
            selection.sampleRate = g_audioEditor.buffer.sampleRate;
            selection.channels = g_audioEditor.buffer.channels;
            selection.samples.assign(g_audioEditor.buffer.samples.begin()
                    + begin * selection.channels,
                g_audioEditor.buffer.samples.begin() + end * selection.channels);
            std::string error;
            const std::filesystem::path previewPath = std::filesystem::temp_directory_path()
                / "3dg_audio_editor_selection.wav";
            if (engine::WriteAudioWav(previewPath.string(), selection, &error)) {
                context.previewAudioRequested = true;
                context.previewAudioPath = previewPath.string();
                context.previewAudioSpatial = false;
                context.previewAudioLoop = false;
            } else if (context.log) context.log->Error("Audio Editor preview failed: " + error);
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) context.stopAudioPreviewRequested = true;
        ImGui::InputText("Export WAV", g_audioEditor.outputPath.data(), g_audioEditor.outputPath.size());
        if (ImGui::Button("Export to Project")) {
            std::string error;
            if (engine::WriteAudioWav(g_audioEditor.outputPath.data(), g_audioEditor.buffer, &error)) {
                g_audioEditor.dirty = false;
                if (context.assets) {
                    std::string refreshError;
                    context.assets->Refresh(context.assets->RootPath(), &refreshError);
                }
                if (context.log) context.log->Info("Audio Editor exported: " + std::string(g_audioEditor.outputPath.data()));
            } else if (context.log) context.log->Error("Audio Editor export failed: " + error);
        }
    }

    ImGui::End();
}

void DrawAudioMixer(EditorDockspace::Context& context, bool* open) {
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::AudioMixer), open)) {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted("Runtime Mixer");
    ImGui::TextDisabled("Bus levels affect previews and Play mode immediately.");
    ImGui::Text("Device: %s | %u Hz | %u channels",
        context.audioDeviceInfo.backend.c_str(), context.audioDeviceInfo.sampleRate,
        context.audioDeviceInfo.channels);
    int voiceLimit = static_cast<int>(context.audioMaxVoices);
    if (ImGui::SliderInt("Voice Budget", &voiceLimit, 8, 256)) {
        context.audioMaxVoices = static_cast<std::size_t>(voiceLimit);
        context.audioMaxVoicesChanged = true;
    }
    const auto& stats = context.audioDebugStats;
    ImGui::Text("Active %d / %d | Managed %d | Streamed %d | Assets %d",
        static_cast<int>(stats.activeVoices), voiceLimit,
        static_cast<int>(stats.managedSources), static_cast<int>(stats.streamedVoices),
        static_cast<int>(stats.pooledAssets));
    ImGui::Text("Voice stealing: %d | Rejected: %d",
        static_cast<int>(stats.stolenVoices), static_cast<int>(stats.rejectedVoices));
    ImGui::Separator();
    for (int i = static_cast<int>(engine::AudioBus::Master);
         i <= static_cast<int>(engine::AudioBus::Ambient); ++i) {
        const engine::AudioBus bus = static_cast<engine::AudioBus>(i);
        ImGui::PushID(i);
        ImGui::TextUnformatted(engine::AudioBusName(bus));
        ImGui::SameLine(90.0f);
        ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - 72.0f, 80.0f));
        ImGui::SliderFloat("##Volume", &context.audioBusVolumes[static_cast<std::size_t>(i)],
                           0.0f, 2.0f, "%.2f");
        ImGui::SameLine();
        ImGui::Checkbox("Mute", &context.audioBusMuted[static_cast<std::size_t>(i)]);
        ImGui::PopID();
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Snapshots");
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::BeginCombo("Preset", engine::AudioSnapshotPresetName(context.activeAudioSnapshot))) {
        for (int i = static_cast<int>(engine::AudioSnapshotPreset::Default);
             i <= static_cast<int>(engine::AudioSnapshotPreset::Cinematic); ++i) {
            const auto preset = static_cast<engine::AudioSnapshotPreset>(i);
            const bool selected = preset == context.activeAudioSnapshot;
            if (ImGui::Selectable(engine::AudioSnapshotPresetName(preset), selected)) {
                context.requestedAudioSnapshot = preset;
                context.audioSnapshotRequested = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::DragFloat("Transition", &context.audioSnapshotTransition,
                     0.05f, 0.0f, 10.0f, "%.2f s");
    ImGui::Checkbox("Dialogue ducks Music", &context.dialogueDucking);

    ImGui::Separator();
    ImGui::TextUnformatted("Bus Processing");
    static int selectedEffectsBus = static_cast<int>(engine::AudioBus::SFX);
    engine::AudioBus effectsBus = static_cast<engine::AudioBus>(selectedEffectsBus);
    if (ImGui::BeginCombo("Process Bus", engine::AudioBusName(effectsBus))) {
        for (int i = static_cast<int>(engine::AudioBus::Music);
             i <= static_cast<int>(engine::AudioBus::Ambient); ++i) {
            const bool selected = i == selectedEffectsBus;
            if (ImGui::Selectable(engine::AudioBusName(static_cast<engine::AudioBus>(i)), selected))
                selectedEffectsBus = i;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    effectsBus = static_cast<engine::AudioBus>(selectedEffectsBus);
    engine::AudioBusEffects& fx = context.audioBusEffects[static_cast<std::size_t>(effectsBus)];
    ImGui::SliderFloat("Low Pass", &fx.lowPassHz, 20.0f, 20000.0f, "%.0f Hz",
                       ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("High Pass", &fx.highPassHz, 20.0f, 20000.0f, "%.0f Hz",
                       ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Reverb Wet", &fx.reverbWet, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Reverb Decay", &fx.reverbDecay, 0.0f, 0.95f, "%.2f");
    ImGui::SliderFloat("Comp Threshold", &fx.compressorThresholdDb, -60.0f, 0.0f, "%.1f dB");
    ImGui::SliderFloat("Comp Ratio", &fx.compressorRatio, 1.0f, 20.0f, "%.1f:1");
    if (ImGui::Button("Reset Processing")) fx = engine::AudioBusEffects{};

    ImGui::Separator();
    ImGui::TextUnformatted("Mixer Preset");
    static std::array<char, 320> mixerPath = [] {
        std::array<char, 320> value{};
        std::snprintf(value.data(), value.size(), "%s",
                      "Content/Audio/project_mixer.3dgmixer");
        return value;
    }();
    ImGui::InputText("Preset Path", mixerPath.data(), mixerPath.size());
    if (ImGui::Button("Save Mixer Preset")) {
        context.audioMixerPresetPath = mixerPath;
        context.saveAudioMixerPresetRequested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Mixer Preset")) {
        context.audioMixerPresetPath = mixerPath;
        context.loadAudioMixerPresetRequested = true;
    }
    ImGui::End();
}

void DrawCreationPalette(EditorDockspace::Context& context) {
    if (g_creationPaletteOpenRequested) {
        g_creationSearch.fill('\0');
        ImGui::OpenPopup("Create Object");
        g_creationPaletteOpenRequested = false;
    }
    ImGui::SetNextWindowSize(ImVec2(470.0f, 430.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopup("Create Object")) return;

    ImGui::TextUnformatted("Create Object");
    ImGui::TextDisabled("Search primitives, lights, physics, and gameplay objects");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##CreationSearch", "Search...", g_creationSearch.data(), g_creationSearch.size());
    ImGui::Separator();

    enum class Action {
        Cube, Plane, Sphere, Capsule, Cylinder, Cone, Pyramid, Torus, Staircase,
        DynamicCube, StaticFloor, TriggerVolume,
        NavMeshBoundsVolume,
        DirectionalLight, PointLight, SpotLight, AreaLight,
        PlayerStart, Door, Pickup, DamageZone, MovingPlatform
    };
    struct Entry { const char* label; const char* description; Action action; };
    constexpr Entry geometry[] = {
        {"Cube", "Box-shaped mesh", Action::Cube}, {"Plane", "Flat surface", Action::Plane},
        {"Sphere", "Round mesh", Action::Sphere}, {"Capsule", "Rounded vertical body", Action::Capsule},
        {"Cylinder", "Circular column", Action::Cylinder}, {"Cone", "Tapered circular mesh", Action::Cone},
        {"Pyramid", "Tapered square mesh", Action::Pyramid}, {"Torus", "Ring-shaped mesh", Action::Torus},
        {"Staircase", "Stepped mesh", Action::Staircase}
    };
    constexpr Entry physics[] = {
        {"Dynamic Cube", "Rigid body ready for simulation", Action::DynamicCube},
        {"Static Floor", "Static collision floor", Action::StaticFloor},
        {"Trigger Volume", "Non-solid event volume", Action::TriggerVolume}
    };
    constexpr Entry navigation[] = {
        {"Nav Mesh Bounds Volume", "Defines the level area used for navigation baking", Action::NavMeshBoundsVolume}
    };
    constexpr Entry lights[] = {
        {"Directional Light", "Scene-wide directional lighting", Action::DirectionalLight},
        {"Point Light", "Omnidirectional local light", Action::PointLight},
        {"Spot Light", "Focused cone light", Action::SpotLight},
        {"Area Light", "Rectangular soft light", Action::AreaLight}
    };
    constexpr Entry gameplay[] = {
        {"Player Start", "Player spawn position", Action::PlayerStart},
        {"Door", "Interactive door setup", Action::Door},
        {"Pickup", "Collectible gameplay object", Action::Pickup},
        {"Damage Zone", "Trigger that applies damage", Action::DamageZone},
        {"Moving Platform", "Animated gameplay platform", Action::MovingPlatform}
    };

    bool close = false;
    auto execute = [&](Action action) {
        switch (action) {
        case Action::Cube: ResetPrimitiveCreator(EditorScene::Primitive::Cube); break;
        case Action::Plane: ResetPrimitiveCreator(EditorScene::Primitive::Plane); break;
        case Action::Sphere: ResetPrimitiveCreator(EditorScene::Primitive::Sphere); break;
        case Action::Capsule: ResetPrimitiveCreator(EditorScene::Primitive::Capsule); break;
        case Action::Cylinder: ResetPrimitiveCreator(EditorScene::Primitive::Cylinder); break;
        case Action::Cone: ResetPrimitiveCreator(EditorScene::Primitive::Cone); break;
        case Action::Pyramid: ResetPrimitiveCreator(EditorScene::Primitive::Pyramid); break;
        case Action::Torus: ResetPrimitiveCreator(EditorScene::Primitive::Torus); break;
        case Action::Staircase: ResetPrimitiveCreator(EditorScene::Primitive::Staircase); break;
        case Action::DynamicCube: context.addDynamicCubeRequested = true; break;
        case Action::StaticFloor: context.addStaticFloorRequested = true; break;
        case Action::TriggerVolume: context.addTriggerVolumeRequested = true; break;
        case Action::NavMeshBoundsVolume: context.addNavMeshBoundsVolumeRequested = true; break;
        case Action::DirectionalLight: context.addDirectionalLightRequested = true; break;
        case Action::PointLight: context.addPointLightRequested = true; break;
        case Action::SpotLight: context.addSpotLightRequested = true; break;
        case Action::AreaLight: context.addAreaLightRequested = true; break;
        case Action::PlayerStart: context.addPlayerStartRequested = true; break;
        case Action::Door: context.addDoorRequested = true; break;
        case Action::Pickup: context.addPickupRequested = true; break;
        case Action::DamageZone: context.addDamageZoneRequested = true; break;
        case Action::MovingPlatform: context.addMovingPlatformRequested = true; break;
        }
        if (action >= Action::DynamicCube) context.frameSelectedRequested = true;
        close = true;
    };
    auto drawCategory = [&](const char* category, const Entry* entries, std::size_t count) {
        bool any = false;
        for (std::size_t i = 0; i < count; ++i)
            any |= MatchesCreationSearch(entries[i].label, category);
        if (!any) return;
        ImGui::TextDisabled("%s", category);
        for (std::size_t i = 0; i < count; ++i) {
            const Entry& entry = entries[i];
            if (!MatchesCreationSearch(entry.label, category)) continue;
            ImGui::PushID(static_cast<int>(entry.action));
            if (ImGui::Selectable(entry.label, false, 0, ImVec2(0.0f, 36.0f))) execute(entry.action);
            ImGui::SameLine(190.0f);
            ImGui::TextDisabled("%s", entry.description);
            ImGui::PopID();
        }
        ImGui::Spacing();
    };

    ImGui::BeginChild("##CreationResults", ImVec2(0.0f, 0.0f), false);
    drawCategory("Geometry", geometry, std::size(geometry));
    drawCategory("Physics", physics, std::size(physics));
    drawCategory("Navigation", navigation, std::size(navigation));
    drawCategory("Lights", lights, std::size(lights));
    drawCategory("Gameplay", gameplay, std::size(gameplay));
    ImGui::EndChild();
    if (close) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void DrawPrimitiveCreator(EditorDockspace::Context& context) {
    if (g_primitiveCreator.openRequested) {
        ImGui::OpenPopup("Create Primitive");
        g_primitiveCreator.openRequested = false;
    }

    ImGui::SetNextWindowSize(ImVec2(430.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Create Primitive", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    constexpr EditorScene::Primitive types[] = {
        EditorScene::Primitive::Cube,
        EditorScene::Primitive::Plane,
        EditorScene::Primitive::Sphere,
        EditorScene::Primitive::Capsule,
        EditorScene::Primitive::Cylinder,
        EditorScene::Primitive::Cone,
        EditorScene::Primitive::Pyramid,
        EditorScene::Primitive::Torus,
        EditorScene::Primitive::Staircase,
    };
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##PrimitiveName", "Object name (automatic if empty)",
        g_primitiveCreator.name.data(), g_primitiveCreator.name.size());
    if (ImGui::BeginCombo("Type", PrimitiveName(g_primitiveCreator.type))) {
        for (EditorScene::Primitive type : types) {
            const bool selected = type == g_primitiveCreator.type;
            if (ImGui::Selectable(PrimitiveName(type), selected)) {
                ResetPrimitiveCreator(type);
                g_primitiveCreator.openRequested = false;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    constexpr float kMinSize = 0.01f;
    switch (g_primitiveCreator.type) {
    case EditorScene::Primitive::Cube:
        ImGui::DragFloat3("Dimensions", &g_primitiveCreator.dimensions.x, 0.05f, kMinSize, 1000.0f);
        break;
    case EditorScene::Primitive::Plane:
        ImGui::DragFloat("Width", &g_primitiveCreator.dimensions.x, 0.05f, kMinSize, 1000.0f);
        ImGui::DragFloat("Depth", &g_primitiveCreator.dimensions.z, 0.05f, kMinSize, 1000.0f);
        break;
    case EditorScene::Primitive::Sphere:
        ImGui::DragFloat("Radius", &g_primitiveCreator.radius, 0.02f, kMinSize, 500.0f);
        break;
    case EditorScene::Primitive::Capsule:
        ImGui::DragFloat("Radius", &g_primitiveCreator.radius, 0.02f, kMinSize, 500.0f);
        ImGui::DragFloat("Height", &g_primitiveCreator.height, 0.05f,
            std::max(g_primitiveCreator.radius * 2.0f, kMinSize), 1000.0f);
        g_primitiveCreator.height = std::max(g_primitiveCreator.height, g_primitiveCreator.radius * 2.0f);
        break;
    case EditorScene::Primitive::Cylinder:
    case EditorScene::Primitive::Cone:
        ImGui::DragFloat("Radius", &g_primitiveCreator.radius, 0.02f, kMinSize, 500.0f);
        ImGui::DragFloat("Height", &g_primitiveCreator.height, 0.05f, kMinSize, 1000.0f);
        break;
    case EditorScene::Primitive::Pyramid:
        ImGui::DragFloat3("Dimensions", &g_primitiveCreator.dimensions.x, 0.05f, kMinSize, 1000.0f);
        break;
    case EditorScene::Primitive::Staircase:
        ImGui::DragFloat3("Dimensions", &g_primitiveCreator.dimensions.x, 0.05f, kMinSize, 1000.0f);
        ImGui::DragInt("Collider Steps", &g_primitiveCreator.staircaseSteps, 0.25f, 1, 64);
        g_primitiveCreator.staircaseSteps = std::clamp(g_primitiveCreator.staircaseSteps, 1, 64);
        break;
    case EditorScene::Primitive::Torus:
        ImGui::DragFloat("Outer Radius", &g_primitiveCreator.radius, 0.02f, kMinSize, 500.0f);
        ImGui::DragFloat("Thickness", &g_primitiveCreator.height, 0.02f, kMinSize, 1000.0f);
        break;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Transform");
    ImGui::DragFloat3("Position", &g_primitiveCreator.position.x, 0.05f);
    ImGui::DragFloat3("Rotation", &g_primitiveCreator.rotationDegrees.x, 0.5f);
    if (ImGui::Button("At Origin")) g_primitiveCreator.position = glm::vec3(0.0f);
    ImGui::SameLine();
    if (ImGui::Button("Place on Ground")) g_primitiveCreator.position.y = PrimitiveGroundHeight();
    ImGui::SameLine();
    const bool hasSelection = context.scene && context.scene->SelectedObject();
    if (!hasSelection) ImGui::BeginDisabled();
    if (ImGui::Button("Beside Selected") && hasSelection) {
        if (const engine::ecs::Transform* selected = context.scene->SelectedTransform()) {
            g_primitiveCreator.position = selected->position;
            g_primitiveCreator.position.x += std::max(1.0f, selected->scale.x * 0.5f + 0.75f);
        }
    }
    if (!hasSelection) ImGui::EndDisabled();
    ImGui::Checkbox("Add collider", &g_primitiveCreator.addCollider);
    ImGui::SameLine();
    ImGui::Checkbox("Frame after creation", &g_primitiveCreator.frameAfterCreate);

    ImGui::Separator();
    const bool createRequested = ImGui::Button("Create", ImVec2(100.0f, 0.0f));
    ImGui::SameLine();
    const bool createAndContinue = ImGui::Button("Create & New", ImVec2(120.0f, 0.0f));
    if (createRequested || createAndContinue) {
        engine::ecs::Transform transform;
        transform.position = g_primitiveCreator.position;
        transform.rotation = glm::quat(glm::radians(g_primitiveCreator.rotationDegrees));

        engine::ecs::Collider collider;
        switch (g_primitiveCreator.type) {
        case EditorScene::Primitive::Cube:
            transform.scale = glm::max(g_primitiveCreator.dimensions, glm::vec3(kMinSize));
            collider = engine::ecs::Collider::MakeBox(transform.scale * 0.5f);
            break;
        case EditorScene::Primitive::Plane: {
            transform.scale = glm::vec3(std::max(g_primitiveCreator.dimensions.x, kMinSize), 1.0f,
                                        std::max(g_primitiveCreator.dimensions.z, kMinSize));
            const glm::vec3 normal = transform.rotation * glm::vec3(0.0f, 1.0f, 0.0f);
            collider = engine::ecs::Collider::MakePlane(normal, glm::dot(normal, transform.position));
            break;
        }
        case EditorScene::Primitive::Sphere: {
            const float diameter = std::max(g_primitiveCreator.radius * 2.0f, kMinSize);
            transform.scale = glm::vec3(diameter);
            collider = engine::ecs::Collider::MakeSphere(std::max(g_primitiveCreator.radius, kMinSize));
            break;
        }
        case EditorScene::Primitive::Capsule:
            transform.scale = glm::vec3(
                std::max(g_primitiveCreator.radius / 0.4f, kMinSize),
                std::max(g_primitiveCreator.height / 1.8f, kMinSize),
                std::max(g_primitiveCreator.radius / 0.4f, kMinSize));
            collider = engine::ecs::Collider::MakeCapsuleFromHeight(
                std::max(g_primitiveCreator.radius, kMinSize),
                std::max(g_primitiveCreator.height, g_primitiveCreator.radius * 2.0f));
            break;
        case EditorScene::Primitive::Cylinder: {
            const float diameter = std::max(g_primitiveCreator.radius * 2.0f, kMinSize);
            transform.scale = glm::vec3(diameter, std::max(g_primitiveCreator.height, kMinSize), diameter);
            collider = engine::ecs::Collider::MakeCylinder(
                std::max(g_primitiveCreator.radius, kMinSize),
                std::max(g_primitiveCreator.height, kMinSize));
            break;
        }
        case EditorScene::Primitive::Cone: {
            const float diameter = std::max(g_primitiveCreator.radius * 2.0f, kMinSize);
            transform.scale = glm::vec3(diameter, std::max(g_primitiveCreator.height, kMinSize), diameter);
            collider = engine::ecs::Collider::MakeCone(
                std::max(g_primitiveCreator.radius, kMinSize),
                std::max(g_primitiveCreator.height, kMinSize));
            break;
        }
        case EditorScene::Primitive::Pyramid:
            transform.scale = glm::max(g_primitiveCreator.dimensions, glm::vec3(kMinSize));
            collider = engine::ecs::Collider::MakePyramid(transform.scale * 0.5f);
            break;
        case EditorScene::Primitive::Staircase:
            transform.scale = glm::max(g_primitiveCreator.dimensions, glm::vec3(kMinSize));
            collider = engine::ecs::Collider::MakeStaircase(transform.scale * 0.5f,
                g_primitiveCreator.staircaseSteps);
            break;
        case EditorScene::Primitive::Torus: {
            const float outerRadius = std::max(g_primitiveCreator.radius, kMinSize);
            const float thickness = std::max(g_primitiveCreator.height, kMinSize);
            transform.scale = glm::vec3(outerRadius / 0.5f, thickness / 0.3f, outerRadius / 0.5f);
            const float minorRadius = thickness * 0.5f;
            collider = engine::ecs::Collider::MakeTorus(
                std::max(outerRadius - minorRadius, kMinSize), minorRadius);
            break;
        }
        }

        context.addConfiguredPrimitiveRequested = true;
        context.configuredPrimitive = g_primitiveCreator.type;
        context.configuredPrimitiveName = g_primitiveCreator.name.data();
        context.configuredPrimitiveTransform = transform;
        context.configuredPrimitiveColliderEnabled = g_primitiveCreator.addCollider;
        context.configuredPrimitiveCollider = collider;
        context.frameSelectedRequested = g_primitiveCreator.frameAfterCreate;
        if (createAndContinue) {
            g_primitiveCreator.name.fill('\0');
            g_primitiveCreator.position.x += std::max(1.0f, transform.scale.x + 0.25f);
        } else {
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void DrawCameraSequences(EditorDockspace::Context& context) {
    EditorScene& scene = *context.scene;
    const auto& cameras = scene.CameraPresets();
    const auto& sequences = scene.CameraSequences();
    if (g_cameraSequenceSelection >= static_cast<int>(sequences.size())) {
        g_cameraSequenceSelection = sequences.empty()
            ? -1 : static_cast<int>(sequences.size()) - 1;
        g_cameraSequenceShotSelection = -1;
        g_cameraSequenceNameSelection = -1;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Cinematic Sequences");
    if (context.showCameraRails) {
        ImGui::Checkbox("Show Rails In Viewport", context.showCameraRails);
    }
    if (ImGui::Button("New Sequence", ImVec2(112.0f, 0.0f))) {
        EditorScene::CameraSequence sequence;
        sequence.name = "Sequence_" + std::to_string(sequences.size() + 1);
        if (!cameras.empty()) {
            sequence.shots.push_back({cameras.front().name, 1.0f, 0.25f, 1});
        }
        g_cameraSequenceSelection = static_cast<int>(scene.AddCameraSequence(sequence));
        g_cameraSequenceShotSelection = sequence.shots.empty() ? -1 : 0;
        g_cameraSequenceNameSelection = -1;
    }

    const char* sequencePreview = g_cameraSequenceSelection >= 0
        && g_cameraSequenceSelection < static_cast<int>(scene.CameraSequences().size())
        ? scene.CameraSequences()[static_cast<std::size_t>(g_cameraSequenceSelection)].name.c_str()
        : "Choose sequence...";
    if (ImGui::BeginCombo("Sequence", sequencePreview)) {
        for (std::size_t i = 0; i < scene.CameraSequences().size(); ++i) {
            const bool selected = static_cast<int>(i) == g_cameraSequenceSelection;
            if (ImGui::Selectable(scene.CameraSequences()[i].name.c_str(), selected)) {
                g_cameraSequenceSelection = static_cast<int>(i);
                g_cameraSequenceShotSelection = -1;
                g_cameraSequenceNameSelection = -1;
            }
        }
        ImGui::EndCombo();
    }

    if (g_cameraSequenceSelection < 0
        || g_cameraSequenceSelection >= static_cast<int>(scene.CameraSequences().size())) {
        ImGui::TextDisabled("Create a sequence to arrange saved cameras into a cinematic.");
        return;
    }

    const std::size_t sequenceIndex = static_cast<std::size_t>(g_cameraSequenceSelection);
    EditorScene::CameraSequence sequence = scene.CameraSequences()[sequenceIndex];
    if (g_cameraSequenceNameSelection != g_cameraSequenceSelection) {
        g_cameraSequenceName.fill('\0');
        std::snprintf(g_cameraSequenceName.data(), g_cameraSequenceName.size(),
                      "%s", sequence.name.c_str());
        g_cameraSequenceNameSelection = g_cameraSequenceSelection;
    }

    bool sequenceChanged = false;
    if (ImGui::InputText("Sequence Name", g_cameraSequenceName.data(),
                         g_cameraSequenceName.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
        sequence.name = g_cameraSequenceName.data();
        sequenceChanged = true;
    }
    sequenceChanged |= ImGui::Checkbox("Loop Sequence", &sequence.loop);

    ImGui::BeginDisabled(sequence.shots.empty());
    if (ImGui::Button("Play Sequence", ImVec2(112.0f, 0.0f))) {
        context.cameraSequence = sequence;
        context.cameraSequencePlayRequested = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!context.cameraSequenceActive);
    if (ImGui::Button("Stop Sequence", ImVec2(112.0f, 0.0f))) {
        context.cameraSequenceStopRequested = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!context.cameraSequenceActive);
    if (ImGui::Button(context.cameraSequencePaused ? "Resume" : "Pause")) {
        context.cameraSequencePauseToggleRequested = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (context.cameraSequenceActive) {
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "Playing");
        if (!context.cameraSequenceActiveName.empty()) {
            ImGui::Text("Active: %s", context.cameraSequenceActiveName.c_str());
        }
        if (context.cameraSequenceInputLocked) {
            ImGui::TextDisabled("Player input is locked for this cinematic.");
        }
        if (context.cameraSequenceSkippable) {
            ImGui::TextDisabled("Press Enter during Play mode to skip.");
        }
    }
    if (context.cameraSequenceActive && context.cameraSequenceDuration > 0.0f) {
        float timeline = context.cameraSequenceTime;
        if (ImGui::SliderFloat("Timeline", &timeline, 0.0f,
                               context.cameraSequenceDuration, "%.2f s")) {
            context.cameraSequenceSeekTime = timeline;
            context.cameraSequenceSeekRequested = true;
        }
        ImGui::Text("%.2f / %.2f seconds",
                    context.cameraSequenceTime, context.cameraSequenceDuration);
    }

    ImGui::BeginDisabled(cameras.empty());
    if (ImGui::Button("Add Shot")) {
        sequence.shots.push_back({cameras.front().name, 1.0f, 0.25f, 1});
        g_cameraSequenceShotSelection = static_cast<int>(sequence.shots.size()) - 1;
        sequenceChanged = true;
    }
    ImGui::EndDisabled();
    if (cameras.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("Save a camera before adding shots.");
    }

    if (ImGui::BeginChild("CameraSequenceShots", ImVec2(0.0f, 112.0f), true)) {
        for (std::size_t i = 0; i < sequence.shots.size(); ++i) {
            const EditorScene::CameraSequenceShot& shot = sequence.shots[i];
            const std::string label = std::to_string(i + 1) + ". "
                + (shot.cameraName.empty() ? "[Missing Camera]" : shot.cameraName)
                + "##sequence_shot_" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(),
                                  g_cameraSequenceShotSelection == static_cast<int>(i))) {
                g_cameraSequenceShotSelection = static_cast<int>(i);
            }
        }
    }
    ImGui::EndChild();

    if (g_cameraSequenceShotSelection >= 0
        && g_cameraSequenceShotSelection < static_cast<int>(sequence.shots.size())) {
        const std::size_t shotIndex = static_cast<std::size_t>(g_cameraSequenceShotSelection);
        EditorScene::CameraSequenceShot& shot = sequence.shots[shotIndex];
        const char* cameraPreview = shot.cameraName.empty()
            ? "Choose camera..." : shot.cameraName.c_str();
        if (ImGui::BeginCombo("Shot Camera", cameraPreview)) {
            for (const EditorScene::CameraPreset& camera : cameras) {
                const bool selected = camera.name == shot.cameraName;
                if (ImGui::Selectable(camera.name.c_str(), selected)) {
                    shot.cameraName = camera.name;
                    sequenceChanged = true;
                }
            }
            ImGui::EndCombo();
        }
        sequenceChanged |= ImGui::DragFloat("Travel Time", &shot.travelDuration,
                                            0.05f, 0.0f, 120.0f, "%.2f s");
        sequenceChanged |= ImGui::DragFloat("Hold Time", &shot.holdDuration,
                                            0.05f, 0.0f, 120.0f, "%.2f s");
        const char* easingNames[] = {"Linear", "Smooth Step", "Ease In", "Ease Out"};
        sequenceChanged |= ImGui::Combo("Shot Easing", &shot.easing, easingNames, 4);
        const char* pathNames[] = {"Linear Rail", "Catmull-Rom Spline"};
        sequenceChanged |= ImGui::Combo("Rail Path", &shot.pathMode, pathNames, 2);
        std::array<char, 128> eventName{};
        std::snprintf(eventName.data(), eventName.size(), "%s", shot.eventName.c_str());
        if (ImGui::InputText("Shot Event", eventName.data(), eventName.size())) {
            shot.eventName = eventName.data();
            sequenceChanged = true;
        }
        ImGui::TextDisabled("The event fires when the camera reaches this shot.");

        ImGui::BeginDisabled(shotIndex == 0);
        if (ImGui::Button("Move Up")) {
            std::swap(sequence.shots[shotIndex], sequence.shots[shotIndex - 1]);
            --g_cameraSequenceShotSelection;
            sequenceChanged = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(shotIndex + 1 >= sequence.shots.size());
        if (ImGui::Button("Move Down")) {
            std::swap(sequence.shots[shotIndex], sequence.shots[shotIndex + 1]);
            ++g_cameraSequenceShotSelection;
            sequenceChanged = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Remove Shot")) {
            sequence.shots.erase(sequence.shots.begin()
                + static_cast<std::ptrdiff_t>(shotIndex));
            g_cameraSequenceShotSelection = std::min(
                g_cameraSequenceShotSelection,
                static_cast<int>(sequence.shots.size()) - 1);
            sequenceChanged = true;
        }
    }

    float authoredDuration = 0.0f;
    for (const EditorScene::CameraSequenceShot& shot : sequence.shots) {
        authoredDuration += std::max(shot.travelDuration, 0.0f)
            + std::max(shot.holdDuration, 0.0f);
    }
    ImGui::Separator();
    ImGui::Text("Timeline Tracks (%.2f s)", authoredDuration);
    {
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float width = std::max(ImGui::GetContentRegionAvail().x, 40.0f);
        ImGui::InvisibleButton("##CinematicTimelineRuler", ImVec2(width, 24.0f));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float y = start.y + 12.0f;
        draw->AddLine(ImVec2(start.x, y), ImVec2(start.x + width, y),
                      IM_COL32(100, 120, 150, 255), 2.0f);
        for (const EditorScene::CinematicCue& cue : sequence.cues) {
            const float normalized = authoredDuration > 0.0f
                ? std::clamp(cue.time / authoredDuration, 0.0f, 1.0f) : 0.0f;
            const float x = start.x + normalized * width;
            const ImU32 color = cue.type == EditorScene::CinematicCueType::Audio
                ? IM_COL32(70, 190, 255, 255)
                : cue.type == EditorScene::CinematicCueType::Animation
                    ? IM_COL32(190, 100, 255, 255)
                    : IM_COL32(255, 180, 55, 255);
            draw->AddLine(ImVec2(x, start.y + 3.0f), ImVec2(x, start.y + 21.0f),
                          color, 3.0f);
        }
    }
    if (ImGui::Button("Add Event")) {
        sequence.cues.push_back({
            EditorScene::CinematicCueType::Event, 0.0f, "Event"});
        g_cameraSequenceCueSelection = static_cast<int>(sequence.cues.size()) - 1;
        sequenceChanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Audio")) {
        EditorScene::CinematicCue cue;
        cue.type = EditorScene::CinematicCueType::Audio;
        sequence.cues.push_back(cue);
        g_cameraSequenceCueSelection = static_cast<int>(sequence.cues.size()) - 1;
        sequenceChanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Animation")) {
        EditorScene::CinematicCue cue;
        cue.type = EditorScene::CinematicCueType::Animation;
        sequence.cues.push_back(cue);
        g_cameraSequenceCueSelection = static_cast<int>(sequence.cues.size()) - 1;
        sequenceChanged = true;
    }

    if (ImGui::BeginChild("CinematicTimelineCues", ImVec2(0.0f, 105.0f), true)) {
        for (std::size_t i = 0; i < sequence.cues.size(); ++i) {
            const EditorScene::CinematicCue& cue = sequence.cues[i];
            const char* type = cue.type == EditorScene::CinematicCueType::Audio
                ? "Audio" : cue.type == EditorScene::CinematicCueType::Animation
                    ? "Animation" : "Event";
            const std::string label = std::to_string(cue.time) + "s  [" + type + "] "
                + (cue.name.empty() ? cue.assetPath : cue.name)
                + "##cinematic_cue_" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(),
                    g_cameraSequenceCueSelection == static_cast<int>(i))) {
                g_cameraSequenceCueSelection = static_cast<int>(i);
            }
        }
    }
    ImGui::EndChild();

    if (g_cameraSequenceCueSelection >= 0
        && g_cameraSequenceCueSelection < static_cast<int>(sequence.cues.size())) {
        const std::size_t cueIndex = static_cast<std::size_t>(g_cameraSequenceCueSelection);
        EditorScene::CinematicCue& cue = sequence.cues[cueIndex];
        int cueType = static_cast<int>(cue.type);
        const char* cueTypes[] = {"Event", "Audio", "Animation"};
        if (ImGui::Combo("Track Type", &cueType, cueTypes, 3)) {
            cue.type = static_cast<EditorScene::CinematicCueType>(cueType);
            sequenceChanged = true;
        }
        sequenceChanged |= ImGui::DragFloat("Cue Time", &cue.time, 0.02f,
                                            0.0f, std::max(authoredDuration, 0.0f), "%.2f s");
        std::array<char, 256> text{};
        if (cue.type == EditorScene::CinematicCueType::Event) {
            std::snprintf(text.data(), text.size(), "%s", cue.name.c_str());
            if (ImGui::InputText("Event Name", text.data(), text.size())) {
                cue.name = text.data();
                sequenceChanged = true;
            }
        } else if (cue.type == EditorScene::CinematicCueType::Audio) {
            std::snprintf(text.data(), text.size(), "%s", cue.assetPath.c_str());
            if (ImGui::InputText("Audio Asset", text.data(), text.size())) {
                cue.assetPath = text.data();
                sequenceChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Paste Audio")) {
                if (const char* clipboard = ImGui::GetClipboardText()) {
                    cue.assetPath = clipboard;
                    sequenceChanged = true;
                }
            }
            sequenceChanged |= ImGui::SliderFloat("Audio Volume", &cue.volume, 0.0f, 2.0f);
        } else {
            const char* targetPreview = cue.targetObject.empty()
                ? "Choose object..." : cue.targetObject.c_str();
            if (ImGui::BeginCombo("Animation Target", targetPreview)) {
                for (const EditorScene::Object& object : scene.Objects()) {
                    const bool selectedTarget = object.name == cue.targetObject;
                    if (ImGui::Selectable(object.name.c_str(), selectedTarget)) {
                        cue.targetObject = object.name;
                        sequenceChanged = true;
                    }
                }
                ImGui::EndCombo();
            }
            std::snprintf(text.data(), text.size(), "%s", cue.animationClip.c_str());
            if (ImGui::InputText("Animation Clip", text.data(), text.size())) {
                cue.animationClip = text.data();
                sequenceChanged = true;
            }
            ImGui::TextDisabled("Animation cues run against animated objects in Play mode.");
        }
        if (ImGui::Button("Remove Cue")) {
            sequence.cues.erase(sequence.cues.begin()
                + static_cast<std::ptrdiff_t>(cueIndex));
            g_cameraSequenceCueSelection = std::min(
                g_cameraSequenceCueSelection,
                static_cast<int>(sequence.cues.size()) - 1);
            sequenceChanged = true;
        }
    }

    if (sequenceChanged) scene.SetCameraSequence(sequenceIndex, sequence);

    if (ImGui::Button("Delete Sequence")) {
        scene.RemoveCameraSequence(sequenceIndex);
        g_cameraSequenceSelection = std::min(
            g_cameraSequenceSelection,
            static_cast<int>(scene.CameraSequences().size()) - 1);
        g_cameraSequenceShotSelection = -1;
        g_cameraSequenceNameSelection = -1;
    }
}

void DrawCameraManager(EditorDockspace::Context& context, bool* open) {
    if (!context.scene) return;
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::CameraManager), open)) {
        ImGui::End();
        return;
    }

    EditorScene& scene = *context.scene;
    const std::vector<EditorScene::CameraPreset>& cameras = scene.CameraPresets();
    if (g_cameraPresetSelection >= static_cast<int>(cameras.size())) {
        g_cameraPresetSelection = cameras.empty() ? -1 : static_cast<int>(cameras.size()) - 1;
    }

    ImGui::TextDisabled("Saved with the current scene.");
    if (!context.camera) ImGui::TextDisabled("Viewport camera is unavailable.");

    if (ImGui::Button("Capture Current", ImVec2(132.0f, 0.0f)) && context.camera) {
        EditorScene::CameraPreset preset;
        preset.name = "Camera_" + std::to_string(cameras.size() + 1);
        preset.position = context.camera->Position();
        preset.target = preset.position + context.camera->Front() * 10.0f;
        preset.fov = context.camera->fov;
        preset.nearPlane = context.camera->nearPlane;
        preset.farPlane = context.camera->farPlane;
        preset.primary = cameras.empty();
        g_cameraPresetSelection = static_cast<int>(scene.AddCameraPreset(preset));
        g_cameraPresetNameSelection = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button("New Default", ImVec2(112.0f, 0.0f))) {
        EditorScene::CameraPreset preset;
        preset.name = "Camera_" + std::to_string(cameras.size() + 1);
        preset.primary = cameras.empty();
        g_cameraPresetSelection = static_cast<int>(scene.AddCameraPreset(preset));
        g_cameraPresetNameSelection = -1;
    }

    ImGui::Separator();
    if (ImGui::BeginChild("CameraPresetList", ImVec2(0.0f, 150.0f), true)) {
        for (std::size_t i = 0; i < cameras.size(); ++i) {
            const EditorScene::CameraPreset& preset = cameras[i];
            std::string label;
            if (preset.primary) label += "[Primary] ";
            if (preset.useInPlay) label += "[Play] ";
            label += preset.name;
            label += "##camera_" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), g_cameraPresetSelection == static_cast<int>(i))) {
                g_cameraPresetSelection = static_cast<int>(i);
                g_cameraPresetNameSelection = -1;
            }
        }
        if (cameras.empty()) {
            ImGui::TextDisabled("No saved cameras. Capture the current viewport.");
        }
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::TextUnformatted("Camera Shake Preview");
    static engine::CameraShakeSettings shake;
    ImGui::DragFloat("Shake Duration", &shake.duration, 0.01f, 0.01f, 10.0f, "%.2f s");
    ImGui::DragFloat("Shake Frequency", &shake.frequency, 0.25f, 0.0f, 120.0f, "%.1f Hz");
    ImGui::DragFloat3("Position Amplitude", &shake.translationAmplitude.x,
                      0.005f, 0.0f, 10.0f, "%.3f");
    ImGui::DragFloat2("Rotation Amplitude", &shake.rotationAmplitudeDegrees.x,
                      0.05f, 0.0f, 45.0f, "%.2f deg");
    ImGui::DragFloat("FOV Amplitude", &shake.fovAmplitude,
                     0.05f, 0.0f, 30.0f, "%.2f deg");
    if (ImGui::Button("Preview Shake", ImVec2(112.0f, 0.0f))) {
        context.cameraShakeSettings = shake;
        context.cameraShakeRequested = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!context.cameraShakeActive);
    if (ImGui::Button("Stop Shake", ImVec2(96.0f, 0.0f))) {
        context.cameraShakeStopRequested = true;
    }
    ImGui::EndDisabled();
    if (context.cameraShakeActive) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.25f, 1.0f), "Active");
    }

    DrawCameraSequences(context);

    if (g_cameraPresetSelection < 0
        || g_cameraPresetSelection >= static_cast<int>(scene.CameraPresets().size())) {
        ImGui::End();
        return;
    }

    const std::size_t selected = static_cast<std::size_t>(g_cameraPresetSelection);
    EditorScene::CameraPreset preset = scene.CameraPresets()[selected];
    if (g_cameraPresetNameSelection != g_cameraPresetSelection) {
        g_cameraPresetName.fill('\0');
        std::snprintf(g_cameraPresetName.data(), g_cameraPresetName.size(), "%s", preset.name.c_str());
        g_cameraPresetNameSelection = g_cameraPresetSelection;
    }

    ImGui::BeginDisabled(context.playMode);
    if (ImGui::Button("Pilot", ImVec2(82.0f, 0.0f)) && context.camera) {
        context.cameraBlendRequested = true;
        context.cameraBlendPreset = preset;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Update From View", ImVec2(132.0f, 0.0f)) && context.camera) {
        preset.position = context.camera->Position();
        preset.target = preset.position + context.camera->Front() * 10.0f;
        preset.fov = context.camera->fov;
        preset.nearPlane = context.camera->nearPlane;
        preset.farPlane = context.camera->farPlane;
        scene.SetCameraPreset(selected, preset);
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate", ImVec2(88.0f, 0.0f))) {
        const std::size_t duplicate = scene.DuplicateCameraPreset(selected);
        if (duplicate != static_cast<std::size_t>(-1)) {
            g_cameraPresetSelection = static_cast<int>(duplicate);
            g_cameraPresetNameSelection = -1;
        }
    }

    if (ImGui::InputText("Name", g_cameraPresetName.data(), g_cameraPresetName.size(),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        preset.name = g_cameraPresetName.data();
        scene.SetCameraPreset(selected, preset);
    }

    bool changed = false;
    changed |= ImGui::DragFloat3("Position", &preset.position.x, 0.05f);
    changed |= ImGui::DragFloat3("Target", &preset.target.x, 0.05f);
    changed |= ImGui::SliderFloat("Field of View", &preset.fov, 10.0f, 120.0f, "%.1f deg");
    changed |= ImGui::DragFloat("Near Plane", &preset.nearPlane, 0.01f, 0.001f, 100.0f, "%.3f");
    changed |= ImGui::DragFloat("Far Plane", &preset.farPlane, 1.0f, 0.1f, 100000.0f, "%.1f");
    changed |= ImGui::DragFloat("Blend Duration", &preset.blendDuration,
                                0.05f, 0.0f, 30.0f, "%.2f s");
    const char* easingNames[] = {"Linear", "Smooth Step", "Ease In", "Ease Out"};
    changed |= ImGui::Combo("Blend Easing", &preset.blendEasing,
                            easingNames, 4);
    changed |= ImGui::Checkbox("Use as fixed camera in Play", &preset.useInPlay);
    if (changed) scene.SetCameraPreset(selected, preset);

    if (preset.primary) {
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "Primary camera");
    } else if (ImGui::Button("Set as Primary")) {
        scene.SetPrimaryCameraPreset(selected);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(context.playMode);
    if (ImGui::Button("Delete")) {
        scene.RemoveCameraPreset(selected);
        g_cameraPresetSelection = std::min(
            g_cameraPresetSelection,
            static_cast<int>(scene.CameraPresets().size()) - 1);
        g_cameraPresetNameSelection = -1;
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::TextWrapped("The primary camera is used in Play only when its Play option is enabled. "
                       "Otherwise the Player Controller camera remains active.");
    ImGui::End();
}

void DrawGizmoToolbar(EditorDockspace::Context& context, bool* open) {
    if (!context.gizmo) {
        return;
    }

    ImGui::SetNextWindowBgAlpha(0.85f);
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::Gizmo), open,
        ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

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
    ImGui::TextUnformatted("Orientation");
    const EditorGizmo::Space space = context.gizmo->CurrentSpace();
    const ImGuiSelectableFlags worldFlags = mode == EditorGizmo::Mode::Scale
        ? ImGuiSelectableFlags_Disabled : 0;
    if (ImGui::Selectable("World", space == EditorGizmo::Space::World, worldFlags, ImVec2(104.0f, 0.0f))) {
        context.gizmo->SetSpace(EditorGizmo::Space::World);
    }
    ImGui::SameLine();
    if (ImGui::Selectable("Local", space == EditorGizmo::Space::Local, 0, ImVec2(104.0f, 0.0f))) {
        context.gizmo->SetSpace(EditorGizmo::Space::Local);
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Axis");
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

    if (mode == EditorGizmo::Mode::Scale) {
        ImGui::SameLine();
        if (ImGui::Button(axis == EditorGizmo::Axis::All ? "All *" : "All", ImVec2(68.0f, 0.0f))) {
            context.gizmo->SetAxis(EditorGizmo::Axis::All);
        }
    }

    ImGui::Separator();
    bool snapping = context.gizmo->SnappingEnabled();
    if (ImGui::Checkbox("Snap", &snapping)) context.gizmo->SetSnappingEnabled(snapping);
    float snapValue = mode == EditorGizmo::Mode::Translate ? context.gizmo->TranslationSnap()
        : mode == EditorGizmo::Mode::Rotate ? context.gizmo->RotationSnap()
        : context.gizmo->ScaleSnap();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(105.0f);
    const char* snapLabel = mode == EditorGizmo::Mode::Translate ? "Units##GizmoSnap"
        : mode == EditorGizmo::Mode::Rotate ? "Degrees##GizmoSnap"
        : "Step##GizmoSnap";
    if (ImGui::DragFloat(snapLabel, &snapValue, mode == EditorGizmo::Mode::Rotate ? 0.5f : 0.01f,
            mode == EditorGizmo::Mode::Rotate ? 0.1f : 0.001f, 1000.0f)) {
        if (mode == EditorGizmo::Mode::Translate) context.gizmo->SetTranslationSnap(snapValue);
        else if (mode == EditorGizmo::Mode::Rotate) context.gizmo->SetRotationSnap(snapValue);
        else context.gizmo->SetScaleSnap(snapValue);
    }

    float visualScale = context.gizmo->VisualScale();
    ImGui::SetNextItemWidth(210.0f);
    if (ImGui::SliderFloat("Size", &visualScale, 0.5f, 2.5f, "%.2fx")) {
        context.gizmo->SetVisualScale(visualScale);
    }

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

    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && io.KeyShift && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        g_creationPaletteOpenRequested = true;
    }

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
            ImGui::Text("Particles: %zu rendered | %d draws | %d culled | %.3f ms CPU | %.3f ms GPU",
                context.particleRenderedCount, context.particleDrawCalls,
                context.particleCulledEmitters, context.particleCpuMilliseconds,
                context.particleGpuMilliseconds);
            ImGui::Text("Scene dirty: %s", context.sceneDirty ? "yes" : "no");
            if (context.gizmo) {
                ImGui::Text("Gizmo: %s %s (%s)", context.gizmo->ModeName(), context.gizmo->AxisName(), context.gizmo->SpaceName());
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
            if (ImGui::MenuItem("Create Object...", "Ctrl+Shift+A")) {
                g_creationPaletteOpenRequested = true;
            }
            if (ImGui::BeginMenu("Component", context.scene && context.scene->SelectedObject()
                    && !context.scene->SelectedObject()->navMeshBoundsVolume)) {
                DrawComponentMenuItems(context);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Primitives")) {
                if (ImGui::MenuItem("Cube")) {
                    ResetPrimitiveCreator(EditorScene::Primitive::Cube);
                }
                if (ImGui::MenuItem("Plane")) {
                    ResetPrimitiveCreator(EditorScene::Primitive::Plane);
                }
                if (ImGui::MenuItem("Sphere")) {
                    ResetPrimitiveCreator(EditorScene::Primitive::Sphere);
                }
                if (ImGui::MenuItem("Capsule")) {
                    ResetPrimitiveCreator(EditorScene::Primitive::Capsule);
                }
                if (ImGui::MenuItem("Cylinder")) {
                    ResetPrimitiveCreator(EditorScene::Primitive::Cylinder);
                }
                if (ImGui::MenuItem("Cone")) {
                    ResetPrimitiveCreator(EditorScene::Primitive::Cone);
                }
                if (ImGui::MenuItem("Pyramid")) {
                    ResetPrimitiveCreator(EditorScene::Primitive::Pyramid);
                }
                if (ImGui::MenuItem("Torus")) {
                    ResetPrimitiveCreator(EditorScene::Primitive::Torus);
                }
                if (ImGui::MenuItem("Staircase")) {
                    ResetPrimitiveCreator(EditorScene::Primitive::Staircase);
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Terrain")) {
                context.addTerrainRequested = true;
                context.frameSelectedRequested = true;
            }
            if (ImGui::MenuItem("Water")) {
                context.addWaterRequested = true;
                context.frameSelectedRequested = true;
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
            if (ImGui::BeginMenu("Navigation")) {
                if (ImGui::MenuItem("Nav Mesh Bounds Volume")) {
                    context.addNavMeshBoundsVolumeRequested = true;
                    context.frameSelectedRequested = true;
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
                if (context.gizmo->CurrentMode() == EditorGizmo::Mode::Scale &&
                    ImGui::MenuItem("All (Uniform)", nullptr, axis == EditorGizmo::Axis::All)) {
                    context.gizmo->SetAxis(EditorGizmo::Axis::All);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Orientation")) {
                const EditorGizmo::Space space = context.gizmo->CurrentSpace();
                if (ImGui::MenuItem("World", nullptr, space == EditorGizmo::Space::World,
                        context.gizmo->CurrentMode() != EditorGizmo::Mode::Scale))
                    context.gizmo->SetSpace(EditorGizmo::Space::World);
                if (ImGui::MenuItem("Local", nullptr, space == EditorGizmo::Space::Local))
                    context.gizmo->SetSpace(EditorGizmo::Space::Local);
                ImGui::EndMenu();
            }
            bool snapping = context.gizmo->SnappingEnabled();
            if (ImGui::MenuItem("Snapping", nullptr, snapping))
                context.gizmo->SetSnappingEnabled(!snapping);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Debug")) {
            if (ImGui::BeginMenu("Navigation")) {
                if (context.showNavigationPreview) {
                    const bool shown = *context.showNavigationPreview;
                    if (ImGui::MenuItem("Show Walkable Areas", nullptr, shown)) {
                        *context.showNavigationPreview = !shown;
                        if (!shown) context.rebuildNavigationPreviewRequested = true;
                    }
                }
                if (ImGui::MenuItem("Rebuild Navigation")) {
                    context.rebuildNavigationPreviewRequested = true;
                }
                ImGui::Separator();
                ImGui::TextDisabled("%d walkable polygons", context.navigationPreviewPolygons);
                ImGui::EndMenu();
            }
            if (context.showAiDebug) {
                bool shown = *context.showAiDebug;
                if (ImGui::MenuItem("AI Agent Debug", nullptr, shown)) {
                    *context.showAiDebug = !shown;
                }
            }
            if (ImGui::BeginMenu("Particles")) {
                if (context.showParticleDebug) {
                    ImGui::MenuItem("Show Guides", nullptr, context.showParticleDebug);
                }
                const bool enabled = !context.showParticleDebug || *context.showParticleDebug;
                ImGui::BeginDisabled(!enabled);
                if (context.particleDebugSelectedOnly) {
                    ImGui::MenuItem("Selected Only", nullptr, context.particleDebugSelectedOnly);
                }
                ImGui::Separator();
                if (context.particleDebugShapes) {
                    ImGui::MenuItem("Emitter Shapes", nullptr, context.particleDebugShapes);
                }
                if (context.particleDebugDirections) {
                    ImGui::MenuItem("Emission Directions", nullptr, context.particleDebugDirections);
                }
                if (context.particleDebugBounds) {
                    ImGui::MenuItem("Culling Bounds", nullptr, context.particleDebugBounds);
                }
                if (context.particleDebugCullingState) {
                    ImGui::MenuItem("Culling Status", nullptr, context.particleDebugCullingState);
                }
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.15f, 0.85f, 1.0f, 1.0f), "Cyan: culling enabled");
                ImGui::TextColored(ImVec4(0.72f, 0.32f, 0.92f, 1.0f), "Purple: culling disabled");
                ImGui::EndDisabled();
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

    DrawCreationPalette(context);
    DrawPrimitiveCreator(context);

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
                DrawWorldSettings(*context.scene, context, &open);
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
        case EditorPanels::Panel::BehaviorGraph:
            break;   // drawn by EditorApp::DrawBehaviorGraphPanel (needs app-owned state)
        case EditorPanels::Panel::AudioEditor:
            DrawAudioEditor(context, &open);
            break;
        case EditorPanels::Panel::AudioMixer:
            DrawAudioMixer(context, &open);
            break;
        case EditorPanels::Panel::ParticleEditor:
            break; // drawn by EditorApp (owns the isolated preview renderer)
        case EditorPanels::Panel::ShaderEditor:
            break; // drawn by EditorApp (owns isolated shader preview resources)
        case EditorPanels::Panel::Hud:
            break; // drawn by EditorApp::DrawHudEditorPanel (owns the HUD document)
        case EditorPanels::Panel::PhysicsStatus:
            DrawPhysicsStatus(context, &open);
            break;
        case EditorPanels::Panel::GameplayDebug:
            DrawGameplayDebug(context, &open);
            break;
        case EditorPanels::Panel::AnimationPreview:
            DrawAnimationPreview(context, &open);
            break;
        case EditorPanels::Panel::Gizmo:
            DrawGizmoToolbar(context, &open);
            break;
        case EditorPanels::Panel::CameraManager:
            DrawCameraManager(context, &open);
            break;
        case EditorPanels::Panel::Count:
            break;
        }

        context.panels->SetOpen(panel, open);
    }

    ImGui::End();
    return true;
}

bool EditorDockspace::IsCompiledWithImGui() const {
    return true;
}
