#include "CharacterEditorPanel.h"

#include "AnimationGraphBuilder.h"

#include <engine/animation/Animator.h>
#include <engine/graphics/Camera.h>
#include <engine/graphics/Primitives.h>
#include <engine/graphics/Shader.h>
#include <engine/graphics/SkinnedModel.h>
#include <engine/graphics/SkinnedRenderer.h>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <cctype>
#include <limits>
#include <unordered_map>

namespace {
template <std::size_t N> void Copy(std::array<char, N>& dst, const std::string& value) {
    std::fill(dst.begin(), dst.end(), '\0');
    const std::size_t count = std::min(value.size(), N - 1);
    std::memcpy(dst.data(), value.data(), count);
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

struct CharacterCollisionChannel {
    const char* name;
    std::uint32_t bit;
};
constexpr CharacterCollisionChannel kCharacterChannels[] = {
    {"Default", engine::ecs::CollisionLayer::Default},
    {"World Static", engine::ecs::CollisionLayer::WorldStatic},
    {"World Dynamic", engine::ecs::CollisionLayer::WorldDynamic},
    {"Player", engine::ecs::CollisionLayer::Player},
    {"Enemy", engine::ecs::CollisionLayer::Enemy},
    {"Collectible", engine::ecs::CollisionLayer::Collectible},
    {"Projectile", engine::ecs::CollisionLayer::Projectile},
    {"Camera Blocker", engine::ecs::CollisionLayer::CameraBlocker},
    {"Trigger", engine::ecs::CollisionLayer::Trigger},
};
struct CharacterCollisionPreset {
    const char* name;
    std::uint32_t layer;
    std::uint32_t mask;
    bool trigger;
};
constexpr CharacterCollisionPreset kCharacterPresets[] = {
    {"Player", engine::ecs::CollisionLayer::Player, engine::ecs::CollisionLayer::CharacterBlockers, false},
    {"Enemy", engine::ecs::CollisionLayer::Enemy, engine::ecs::CollisionLayer::CharacterBlockers, false},
    {"Default", engine::ecs::CollisionLayer::Default, engine::ecs::CollisionLayer::All, false},
    {"Trigger Volume", engine::ecs::CollisionLayer::Trigger, engine::ecs::CollisionLayer::All, true},
    {"No Collision", engine::ecs::CollisionLayer::Default, 0u, false},
};
}

CharacterEditorPanel::~CharacterEditorPanel() = default;

void CharacterEditorPanel::QueueOpen(const std::string& path) { m_pendingOpen = path; }

void CharacterEditorPanel::ResetPreviewModel() {
    m_previewModel = nullptr;
    m_previewModelPath.clear();
    m_previewAnimSignature.clear();
    m_previewError.clear();
    m_previewPose.clear();
    m_previewTime = 0.0f;
    m_previewClip = 0;
    m_previewGraphDirty = true;
    m_previewController = {};
    m_previewGraphParameters.clear();
    m_colliderGuideDirty = true;
}

void CharacterEditorPanel::RebuildColliderGuide() {
    const auto& c = m_asset.collider;
    switch (c.shape) {
    case engine::ecs::ColliderShape::Sphere:
        m_colliderGuideMesh.emplace(engine::primitives::Sphere(18)); break;
    case engine::ecs::ColliderShape::Plane:
        m_colliderGuideMesh.emplace(engine::primitives::Plane(1.0f)); break;
    case engine::ecs::ColliderShape::Box:
        m_colliderGuideMesh.emplace(engine::primitives::Cube()); break;
    case engine::ecs::ColliderShape::Capsule:
        m_colliderGuideMesh.emplace(engine::primitives::Capsule(
            std::max(c.radius, .001f), std::max(2.0f * (c.halfHeight + c.radius), .002f), 16)); break;
    case engine::ecs::ColliderShape::Cylinder:
        m_colliderGuideMesh.emplace(engine::primitives::Cylinder(24)); break;
    case engine::ecs::ColliderShape::Cone:
        m_colliderGuideMesh.emplace(engine::primitives::Cone(24)); break;
    case engine::ecs::ColliderShape::Pyramid:
        m_colliderGuideMesh.emplace(engine::primitives::Pyramid()); break;
    case engine::ecs::ColliderShape::Torus:
        m_colliderGuideMesh.emplace(engine::primitives::Torus(
            std::max(c.majorRadius, .001f), std::max(c.minorRadius, .001f), 32, 12)); break;
    case engine::ecs::ColliderShape::Staircase:
        m_colliderGuideMesh.emplace(engine::primitives::Staircase(std::max(c.steps, 1))); break;
    }
    m_cachedGuideCollider = c;
    m_colliderGuideDirty = false;
}

void CharacterEditorPanel::RebuildPreviewGraph() {
    m_previewController = {};
    m_previewGraphParameters.clear();
    m_previewTime = 0.0f;
    m_previewGraphDirty = false;
    if (!m_previewModel || m_asset.animationStates.empty()) return;

    const auto& clips = m_previewModel->Animations();
    const auto resolveClip = [&](int fallback, const std::string& name) {
        int clip = fallback;
        if (!name.empty()) {
            for (std::size_t i = 0; i < clips.size(); ++i)
                if (clips[i].name == name) { clip = static_cast<int>(i); break; }
        }
        return clips.empty() ? -1 : std::clamp(clip, 0, static_cast<int>(clips.size() - 1));
    };
    const auto duration = [&](int clip) {
        if (clip < 0 || clip >= static_cast<int>(clips.size())) return 0.0f;
        const auto& animation = clips[static_cast<std::size_t>(clip)];
        return animation.ticksPerSecond > 0.0f ? animation.duration / animation.ticksPerSecond : 0.0f;
    };
    editor::BuildAnimationController(m_previewController,
        m_asset.animationStates, m_asset.animationParameters, m_asset.animationTransitions,
        resolveClip, duration);

    // The preview UI keeps its own editable copy of each parameter's value.
    for (const auto& parameter : m_asset.animationParameters) {
        m_previewGraphParameters[parameter.name] = parameter.defaultValue;
    }
}

unsigned int CharacterEditorPanel::RenderModelPreview(int width, int height, float deltaTime) {
    width = std::max(width, 32);
    height = std::max(height, 32);
    if (!m_previewFramebuffer) m_previewFramebuffer.emplace(width, height, GL_RGBA8, true);
    else m_previewFramebuffer->Resize(width, height);
    if (!m_previewRenderer) m_previewRenderer = std::make_unique<engine::SkinnedRenderer>();
    if (!m_colliderGuideShader) {
        static const char* vertex = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPosition;
uniform mat4 uViewProjection;
uniform mat4 uModel;
void main(){gl_Position=uViewProjection*uModel*vec4(aPosition,1.0);}
)GLSL";
        static const char* fragment = R"GLSL(
#version 330 core
out vec4 FragColor;
uniform vec3 uColor;
void main(){FragColor=vec4(uColor,1.0);}
)GLSL";
        engine::ShaderCompileReport report;
        m_colliderGuideShader = engine::Shader::TryCompile(vertex, fragment, report);
    }

    // Reload the preview model when the model OR its merged animation sources change.
    std::string animSignature;
    for (const auto& source : m_asset.animationSources) {
        animSignature += source.file + '|' + source.clipName + '|' +
                         (source.stripRootMotion ? "1" : "0") + '\n';
    }
    if (m_previewModelPath != m_asset.modelAssetPath || m_previewAnimSignature != animSignature) {
        ResetPreviewModel();
        m_previewModelPath = m_asset.modelAssetPath;
        m_previewAnimSignature = animSignature;
        if (!m_previewModelPath.empty()) {
            std::vector<engine::RuntimeAssetManager::SkinnedAnimationSource> sources;
            for (const auto& source : m_asset.animationSources) {
                if (!source.file.empty()) {
                    sources.push_back({source.file, source.clipName, source.stripRootMotion});
                }
            }
            m_previewModel = sources.empty()
                ? m_previewAssets.LoadSkinnedModel(m_previewModelPath, &m_previewError)
                : m_previewAssets.LoadSkinnedModel(m_previewModelPath, sources, &m_previewError);
            m_previewGraphDirty = true;
        }
    }
    if (m_previewModel && m_previewGraphDirty) RebuildPreviewGraph();

    GLint oldFbo = 0, oldViewport[4]{}, oldProgram = 0, oldVao = 0, oldPolygonMode[2]{};
    GLfloat oldLineWidth = 1.0f;
    GLfloat oldClear[4]{};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
    glGetIntegerv(GL_VIEWPORT, oldViewport);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, oldClear);
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
    glGetIntegerv(GL_POLYGON_MODE, oldPolygonMode);
    glGetFloatv(GL_LINE_WIDTH, &oldLineWidth);
    const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
    const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    const GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);

    m_previewFramebuffer->Bind();
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.055f, 0.070f, 0.095f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (m_previewModel) {
        const auto& clips = m_previewModel->Animations();
        if (!clips.empty() && !m_asset.animationStates.empty()) {
            for (const auto& parameter : m_asset.animationParameters) {
                const float value = m_previewGraphParameters[parameter.name];
                if (parameter.type == EditorScene::AnimationParameter::Type::Bool)
                    m_previewController.SetBoolParameter(parameter.name, value != 0.0f);
                else if (parameter.type == EditorScene::AnimationParameter::Type::Float)
                    m_previewController.SetParameter(parameter.name, value);
            }
            if (m_previewPlaying) m_previewController.Update(std::max(deltaTime, 0.0f));
            const int current = m_previewController.CurrentClip();
            const int previous = m_previewController.PrevClip();
            const int blendClip = m_previewController.CurrentBlendClip();
            if (current >= 0 && current < static_cast<int>(clips.size())) {
                auto sampleState = [&](int fallback, float time,
                                       const engine::AnimationController::BlendSpaceResult& space,
                                       std::vector<engine::BoneLocal>& output) {
                    if (space.active && !space.samples.empty()) {
                        const auto clipSeconds = [](const engine::Animation& animation) {
                            const float ticks = animation.ticksPerSecond > 0.0f ? animation.ticksPerSecond : 25.0f;
                            return animation.duration > 0.0f ? animation.duration / ticks : 0.0f;
                        };
                        const float referenceLength = fallback >= 0 && fallback < static_cast<int>(clips.size())
                            ? clipSeconds(clips[static_cast<std::size_t>(fallback)]) : 0.0f;
                        float accumulated = 0.0f; bool sampled = false;
                        for (const auto& weighted : space.samples) {
                            if (weighted.clip < 0 || weighted.clip >= static_cast<int>(clips.size()) || weighted.weight <= 0.0f) continue;
                            const auto& animation = clips[static_cast<std::size_t>(weighted.clip)];
                            float sampleTime = time;
                            const float sampleLength = clipSeconds(animation);
                            if (space.synchronized && referenceLength > 0.0001f && sampleLength > 0.0001f)
                                sampleTime = (std::fmod(std::max(time, 0.0f), referenceLength) / referenceLength) * sampleLength;
                            std::vector<engine::BoneLocal> pose;
                            engine::Animator::SampleLocal(m_previewModel->GetSkeleton(), animation, sampleTime, pose);
                            if (!sampled) { output=std::move(pose); accumulated=weighted.weight; sampled=true; }
                            else {
                                std::vector<engine::BoneLocal> mixed;
                                engine::Animator::BlendLocal(output, pose,
                                    weighted.weight/(accumulated+weighted.weight), mixed);
                                output=std::move(mixed); accumulated+=weighted.weight;
                            }
                        }
                        return sampled;
                    }
                    const int aIndex = space.active ? space.clipA : fallback;
                    if (aIndex < 0 || aIndex >= static_cast<int>(clips.size())) return false;
                    engine::Animator::SampleLocal(m_previewModel->GetSkeleton(),
                        clips[static_cast<std::size_t>(aIndex)], time, output);
                    if (space.active && space.clipB != aIndex
                        && space.clipB >= 0 && space.clipB < static_cast<int>(clips.size())) {
                        std::vector<engine::BoneLocal> b, mixed;
                        engine::Animator::SampleLocal(m_previewModel->GetSkeleton(),
                            clips[static_cast<std::size_t>(space.clipB)], time, b);
                        engine::Animator::BlendLocal(output, b, space.alpha, mixed);
                        output = std::move(mixed);
                    }
                    return true;
                };
                const auto currentSpace = m_previewController.CurrentBlendSpace();
                std::vector<engine::BoneLocal> currentPose;
                sampleState(current, m_previewController.CurrentTime(), currentSpace, currentPose);
                if (!currentSpace.active && blendClip >= 0 && blendClip < static_cast<int>(clips.size())) {
                    std::vector<engine::BoneLocal> b, mixed;
                    engine::Animator::SampleLocal(m_previewModel->GetSkeleton(),
                        clips[static_cast<std::size_t>(blendClip)], m_previewController.CurrentTime(), b);
                    engine::Animator::BlendLocal(currentPose, b, m_previewController.CurrentBlendWeight(), mixed);
                    currentPose = std::move(mixed);
                }
                if (m_previewController.Blending() && previous >= 0 && previous < static_cast<int>(clips.size())) {
                    std::vector<engine::BoneLocal> previousPose, mixed;
                    sampleState(previous, m_previewController.PrevTime(),
                        m_previewController.PreviousBlendSpace(), previousPose);
                    engine::Animator::BlendLocal(previousPose, currentPose,
                        m_previewController.Blend(), mixed);
                    currentPose = std::move(mixed);
                }
                engine::Animator::Compose(m_previewModel->GetSkeleton(), currentPose, m_previewPose);
            } else {
                engine::Animator::ComputeBindPose(m_previewModel->GetSkeleton(), m_previewPose);
            }
        } else {
            engine::Animator::ComputeBindPose(m_previewModel->GetSkeleton(), m_previewPose);
        }

        const float radius = std::max(m_previewModel->BoundingRadius(), 0.001f);
        const float fit = (0.88f * m_previewZoom) / radius;
        // View framing: orbit + fit-scale. The offset axes live in this frame, so the
        // gizmo handles follow the orbit.
        glm::mat4 frame(1.0f);
        frame = glm::rotate(frame, glm::radians(m_previewYaw), glm::vec3(0, 1, 0));
        frame = glm::rotate(frame, glm::radians(m_previewPitch), glm::vec3(1, 0, 0));
        frame = glm::scale(frame, glm::vec3(fit));

        // Render-only model offset (pos/rot/scale about the model centre). For a fresh
        // Z-up rig with no offset set yet, preview an automatic stand-up so it matches
        // what dropping it into the scene will do.
        const glm::vec3 boundsSize = m_previewModel->Max() - m_previewModel->Min();
        const glm::vec3 scaleDelta = m_asset.modelOffsetScale - glm::vec3(1.0f);
        const bool assetHasOffset =
            glm::dot(m_asset.modelOffsetPosition, m_asset.modelOffsetPosition) > 1e-8f
            || glm::dot(m_asset.modelOrientationEuler, m_asset.modelOrientationEuler) > 1e-4f
            || glm::dot(scaleDelta, scaleDelta) > 1e-8f;
        glm::vec3 previewEuler = m_asset.modelOrientationEuler;
        if (!assetHasOffset && boundsSize.z > boundsSize.y * 1.25f)
            previewEuler = glm::vec3(-90.0f, 0.0f, 0.0f);
        const glm::vec3 previewScale(
            m_asset.modelOffsetScale.x != 0.0f ? m_asset.modelOffsetScale.x : 1e-4f,
            m_asset.modelOffsetScale.y != 0.0f ? m_asset.modelOffsetScale.y : 1e-4f,
            m_asset.modelOffsetScale.z != 0.0f ? m_asset.modelOffsetScale.z : 1e-4f);
        glm::mat4 offset(1.0f);
        offset = glm::translate(offset, m_asset.modelOffsetPosition);
        offset *= glm::mat4_cast(glm::quat(glm::radians(previewEuler)));
        offset = glm::scale(offset, previewScale);
        offset = glm::translate(offset, -m_previewModel->Center());

        const glm::mat4 model = frame * offset;
        engine::Camera camera(glm::vec3(0.0f, 0.0f, 2.5f));
        camera.LookAt(glm::vec3(0.0f));
        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        m_previewRenderer->Draw(*m_previewModel, m_previewPose, model, camera,
            aspect,
            glm::normalize(glm::vec3(0.45f, -1.0f, -0.35f)),
            glm::vec3(1.0f, 0.96f, 0.90f), glm::vec3(0.16f));

        // Cache the transforms so Draw() can project the gizmo handles onto the image.
        m_previewViewProj = camera.ProjectionMatrix(aspect) * camera.ViewMatrix();
        m_previewModelMatrix = model;
        m_previewGizmoFrame = frame;
        m_previewModelCenter = m_previewModel->Center();

        if (m_showColliderGuide && m_asset.colliderEnabled && m_colliderGuideShader) {
            if (m_colliderGuideDirty || !m_colliderGuideMesh) RebuildColliderGuide();
            if (m_colliderGuideMesh) {
                const float worldHeight = std::max(m_asset.playerController.capsuleHeight, .001f);
                const float units = (1.76f * m_previewZoom) / worldHeight;
                glm::mat4 colliderModel(1.0f);
                colliderModel = glm::rotate(colliderModel, glm::radians(m_previewYaw), glm::vec3(0,1,0));
                colliderModel = glm::rotate(colliderModel, glm::radians(m_previewPitch), glm::vec3(1,0,0));
                const auto& c = m_asset.collider;
                if (c.shape == engine::ecs::ColliderShape::Sphere)
                    colliderModel = glm::scale(colliderModel, glm::vec3(2.0f*c.radius*units));
                else if (c.shape == engine::ecs::ColliderShape::Box
                    || c.shape == engine::ecs::ColliderShape::Pyramid
                    || c.shape == engine::ecs::ColliderShape::Staircase)
                    colliderModel = glm::scale(colliderModel, 2.0f*c.halfExtents*units);
                else if (c.shape == engine::ecs::ColliderShape::Cylinder
                    || c.shape == engine::ecs::ColliderShape::Cone)
                    colliderModel = glm::scale(colliderModel,
                        glm::vec3(2.0f*c.radius*units, 2.0f*c.halfHeight*units, 2.0f*c.radius*units));
                else if (c.shape == engine::ecs::ColliderShape::Plane) {
                    colliderModel = glm::translate(colliderModel, glm::vec3(0,c.planeOffset*units,0));
                    colliderModel = glm::scale(colliderModel, glm::vec3(2.0f));
                } else {
                    colliderModel = glm::scale(colliderModel, glm::vec3(units));
                }
                glDisable(GL_DEPTH_TEST);
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                glLineWidth(2.0f);
                m_colliderGuideShader->Bind();
                m_colliderGuideShader->SetMat4("uViewProjection",
                    camera.ProjectionMatrix(static_cast<float>(width)/static_cast<float>(height)) * camera.ViewMatrix());
                m_colliderGuideShader->SetMat4("uModel", colliderModel);
                m_colliderGuideShader->SetVec3("uColor", c.isTrigger
                    ? glm::vec3(0.15f, 0.85f, 1.0f) : glm::vec3(1.0f, 0.72f, 0.12f));
                m_colliderGuideMesh->Draw();
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            }
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(oldFbo));
    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
    glClearColor(oldClear[0], oldClear[1], oldClear[2], oldClear[3]);
    glUseProgram(static_cast<GLuint>(oldProgram));
    glBindVertexArray(static_cast<GLuint>(oldVao));
    glPolygonMode(GL_FRONT, static_cast<GLenum>(oldPolygonMode[0]));
    glPolygonMode(GL_BACK, static_cast<GLenum>(oldPolygonMode[1]));
    glLineWidth(oldLineWidth);
    if (depthWasEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (cullWasEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (blendWasEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (scissorWasEnabled) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    return m_previewFramebuffer->ColorTexture();
}

void CharacterEditorPanel::SyncBuffers() {
    Copy(m_pathBuffer, m_path); Copy(m_nameBuffer, m_asset.name);
    Copy(m_modelBuffer, m_asset.modelAssetPath); Copy(m_materialBuffer, m_asset.materialAssetPath);
    Copy(m_idleBuffer, m_asset.idleClipName); Copy(m_walkBuffer, m_asset.walkClipName); Copy(m_runBuffer, m_asset.runClipName);
    Copy(m_behaviorBuffer, m_asset.behaviorTreeAsset); Copy(m_scriptClassBuffer, m_asset.scriptClassName);
    Copy(m_scriptPathBuffer, m_asset.scriptPath);
}

void CharacterEditorPanel::RefreshAssetChoices(const std::string& assetRoot) {
    m_modelChoices.clear();
    m_materialChoices.clear();
    m_scannedAssetRoot = assetRoot;
    std::error_code ec;
    const std::filesystem::path root(assetRoot);
    if (!std::filesystem::exists(root, ec)) return;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end;
         it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::filesystem::path& file = it->path();
        const std::string extension = Lower(file.extension().string());
        AssetChoice choice{file.generic_string(), file.filename().string()};
        if (extension == ".fbx" || extension == ".gltf" || extension == ".glb"
            || extension == ".dae") {
            m_modelChoices.push_back(std::move(choice));
        } else if (extension == ".3dgmat") {
            m_materialChoices.push_back(std::move(choice));
        }
    }
    const auto byName = [](const AssetChoice& a, const AssetChoice& b) {
        return Lower(a.displayName) < Lower(b.displayName);
    };
    std::sort(m_modelChoices.begin(), m_modelChoices.end(), byName);
    std::sort(m_materialChoices.begin(), m_materialChoices.end(), byName);
}

void CharacterEditorPanel::Draw(EditorScene& scene, const std::string& assetRoot, bool* open,
                                bool* assetSaved, std::string* message,
                                float deltaTime) {
    if (m_scannedAssetRoot != assetRoot) RefreshAssetChoices(assetRoot);
    if (!m_pendingOpen.empty()) {
        std::string error;
        if (m_asset.Load(m_pendingOpen, &error)) { m_path = m_pendingOpen; m_dirty = false; if (message) *message = "Opened character: " + m_path; }
        else if (message) *message = error;
        m_pendingOpen.clear(); SyncBuffers(); ResetPreviewModel();
    }
    if (m_path.empty()) {
        m_path = (std::filesystem::path(assetRoot) / "Assets" / "Characters" / "Character.3dgcharacter").string();
        SyncBuffers();
    }
    if (!ImGui::Begin("Character Editor", open, ImGuiWindowFlags_MenuBar)) { ImGui::End(); return; }
    if (ImGui::BeginMenuBar()) {
        if (ImGui::MenuItem("New")) { m_asset = {}; m_path.clear(); m_dirty = false; SyncBuffers(); ResetPreviewModel(); }
        if (ImGui::MenuItem("Capture Selected", nullptr, false, scene.SelectedObject() != nullptr)) {
            m_asset.Capture(*scene.SelectedObject()); m_dirty = true; SyncBuffers(); ResetPreviewModel();
        }
        if (ImGui::MenuItem("Apply to Selected", nullptr, false, scene.SelectedObject() != nullptr)) {
            if (m_asset.Apply(scene) && message) *message = "Applied character setup to selected object";
        }
        if (ImGui::MenuItem("Add to Scene")) {
            m_addToSceneRequested = true;   // the app instantiates it as a new object
        }
        if (ImGui::MenuItem("Save")) {
            m_path = m_pathBuffer.data();
            if (std::filesystem::path(m_path).extension() != ".3dgcharacter") m_path += ".3dgcharacter";
            std::string error;
            if (m_asset.Save(m_path, &error)) { m_dirty = false; if (assetSaved) *assetSaved = true; if (message) *message = "Saved character: " + m_path; SyncBuffers(); }
            else if (message) *message = error;
        }
        ImGui::EndMenuBar();
    }

    ImGui::SetNextItemWidth(-1.0f); ImGui::InputText("##CharacterPath", m_pathBuffer.data(), m_pathBuffer.size());
    ImGui::Separator();
    const float leftWidth = 175.0f, rightWidth = 330.0f;
    ImGui::BeginChild("CharacterComponents", ImVec2(leftWidth, 0), true);
    ImGui::TextDisabled("COMPONENTS");
    const char* components[] = { "Character", "Mesh", "Collision", "Movement", "Animation", "Gameplay", "AI", "Script" };
    for (int i = 0; i < 8; ++i) if (ImGui::Selectable(components[i], m_component == i)) m_component = i;
    ImGui::EndChild(); ImGui::SameLine();

    ImGui::BeginChild("CharacterPreview", ImVec2(-rightWidth - 12.0f, 0), true);
    ImGui::TextDisabled("CHARACTER PREVIEW");
    ImVec2 available = ImGui::GetContentRegionAvail();
    const float previewWidth = std::max(available.x, 160.0f);
    const float h = std::max(220.0f, available.y - 145.0f);
    const unsigned int previewTexture = RenderModelPreview(
        static_cast<int>(previewWidth), static_cast<int>(h), deltaTime);
    ImGui::Image((ImTextureID)(std::intptr_t)previewTexture,
        ImVec2(previewWidth, h), ImVec2(0, 1), ImVec2(1, 0));
    const ImVec2 imgMin = ImGui::GetItemRectMin();
    const ImVec2 imgMax = ImGui::GetItemRectMax();
    const bool imageHovered = ImGui::IsItemHovered();

    // Model-offset gizmo, drawn as an overlay on the preview image. Handles follow the
    // orbit; dragging one edits the asset's render-only offset (Move / Rotate / Scale).
    bool gizmoConsumedMouse = false;
    if (m_previewModel) {
        const float imgW = imgMax.x - imgMin.x;
        const float imgH = imgMax.y - imgMin.y;
        auto project = [&](const glm::vec3& world, ImVec2& out) -> bool {
            const glm::vec4 clip = m_previewViewProj * glm::vec4(world, 1.0f);
            if (clip.w <= 1e-5f) return false;
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            out = ImVec2(imgMin.x + (ndc.x * 0.5f + 0.5f) * imgW,
                         imgMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * imgH);
            return true;
        };
        const glm::vec3 pivotWorld =
            glm::vec3(m_previewModelMatrix * glm::vec4(m_previewModelCenter, 1.0f));
        ImVec2 pivotPx;
        if (project(pivotWorld, pivotPx)) {
            const glm::mat3 frameAxes(m_previewGizmoFrame);
            const ImU32 axisColors[3] = {
                IM_COL32(232, 68, 68, 255), IM_COL32(72, 210, 72, 255), IM_COL32(70, 120, 240, 255) };
            const float handleLen = 46.0f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 tipPx[3];
            ImVec2 dirPx[3];
            float pixelsPerUnit[3] = { 1.0f, 1.0f, 1.0f };
            for (int a = 0; a < 3; ++a) {
                glm::vec3 unit(0.0f); unit[a] = 1.0f;
                const glm::vec3 axisWorld = frameAxes * unit;   // 1 model-unit along this axis
                ImVec2 endPx;
                glm::vec2 screenDir(1.0f, 0.0f);
                if (project(pivotWorld + axisWorld, endPx)) {
                    screenDir = glm::vec2(endPx.x - pivotPx.x, endPx.y - pivotPx.y);
                    const float len = std::sqrt(screenDir.x * screenDir.x + screenDir.y * screenDir.y);
                    pixelsPerUnit[a] = std::max(len, 1e-3f);
                    if (len > 1e-3f) screenDir /= len;
                }
                dirPx[a] = ImVec2(screenDir.x, screenDir.y);
                tipPx[a] = ImVec2(pivotPx.x + screenDir.x * handleLen, pivotPx.y + screenDir.y * handleLen);
            }
            for (int a = 0; a < 3; ++a) {
                const bool active = (m_activeGizmoAxis == a);
                dl->AddLine(pivotPx, tipPx[a], axisColors[a], active ? 3.5f : 2.0f);
                if (m_gizmoMode == 2)
                    dl->AddRectFilled(ImVec2(tipPx[a].x - 4, tipPx[a].y - 4),
                                      ImVec2(tipPx[a].x + 4, tipPx[a].y + 4), axisColors[a]);
                else if (m_gizmoMode == 1)
                    dl->AddCircle(tipPx[a], 5.0f, axisColors[a], 12, active ? 3.0f : 2.0f);
                else
                    dl->AddCircleFilled(tipPx[a], 4.0f, axisColors[a]);
            }
            dl->AddCircleFilled(pivotPx, 3.0f, IM_COL32(230, 230, 230, 255));

            // Forward reference arrow (object-local -Z, the gameplay "front"). Rotate the
            // model so its face points along this cyan arrow and it will face forward in
            // the scene.
            {
                const glm::vec3 fwdWorld = glm::mat3(m_previewGizmoFrame) * glm::vec3(0.0f, 0.0f, -1.0f);
                ImVec2 fwdEnd;
                if (project(pivotWorld + fwdWorld, fwdEnd)) {
                    glm::vec2 fd(fwdEnd.x - pivotPx.x, fwdEnd.y - pivotPx.y);
                    const float fl = std::sqrt(fd.x * fd.x + fd.y * fd.y);
                    if (fl > 1e-3f) {
                        fd /= fl;
                        const float reach = 58.0f;
                        const ImVec2 tip(pivotPx.x + fd.x * reach, pivotPx.y + fd.y * reach);
                        const ImU32 cyan = IM_COL32(60, 220, 235, 255);
                        dl->AddLine(pivotPx, tip, cyan, 2.5f);
                        const glm::vec2 perp(-fd.y, fd.x);
                        dl->AddTriangleFilled(
                            ImVec2(tip.x + fd.x * 9, tip.y + fd.y * 9),
                            ImVec2(tip.x + perp.x * 5 - fd.x * 2, tip.y + perp.y * 5 - fd.y * 2),
                            ImVec2(tip.x - perp.x * 5 - fd.x * 2, tip.y - perp.y * 5 - fd.y * 2),
                            cyan);
                    }
                }
            }

            auto distToSegment = [](const ImVec2& p, const ImVec2& a, const ImVec2& b) {
                const float vx = b.x - a.x, vy = b.y - a.y;
                const float wx = p.x - a.x, wy = p.y - a.y;
                const float len2 = vx * vx + vy * vy;
                float t = len2 > 1e-5f ? (wx * vx + wy * vy) / len2 : 0.0f;
                t = std::clamp(t, 0.0f, 1.0f);
                const float dx = p.x - (a.x + t * vx), dy = p.y - (a.y + t * vy);
                return std::sqrt(dx * dx + dy * dy);
            };
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            if (!m_gizmoDragging && imageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                int best = -1; float bestDist = 9.0f;   // pixel pick threshold
                for (int a = 0; a < 3; ++a) {
                    const float d = distToSegment(mouse, pivotPx, tipPx[a]);
                    if (d < bestDist) { bestDist = d; best = a; }
                }
                if (best >= 0) { m_gizmoDragging = true; m_activeGizmoAxis = best; gizmoConsumedMouse = true; }
            }
            if (m_gizmoDragging && m_activeGizmoAxis >= 0) {
                gizmoConsumedMouse = true;
                const int a = m_activeGizmoAxis;
                const ImVec2 d = ImGui::GetIO().MouseDelta;
                const float along = d.x * dirPx[a].x + d.y * dirPx[a].y;   // px moved along the axis
                if (m_gizmoMode == 0)
                    m_asset.modelOffsetPosition[a] += along / pixelsPerUnit[a];
                else if (m_gizmoMode == 1)
                    m_asset.modelOrientationEuler[a] += along * 0.5f;
                else
                    m_asset.modelOffsetScale[a] = std::max(0.001f, m_asset.modelOffsetScale[a] + along * 0.01f);
                m_dirty = true;
            }
        }
    }
    if (m_gizmoDragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_gizmoDragging = false; m_activeGizmoAxis = -1;
    }

    if (imageHovered && !gizmoConsumedMouse && !m_gizmoDragging) {
        ImGui::SetTooltip("Left-drag to orbit. Wheel to zoom. Drag the coloured handles to transform.");
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 drag = ImGui::GetIO().MouseDelta;
            m_previewYaw += drag.x * 0.45f;
            m_previewPitch = std::clamp(m_previewPitch + drag.y * 0.35f, -80.0f, 80.0f);
        }
        if (ImGui::GetIO().MouseWheel != 0.0f)
            m_previewZoom = std::clamp(m_previewZoom + ImGui::GetIO().MouseWheel * 0.08f,
                                       0.35f, 2.5f);
    }
    if (m_asset.modelAssetPath.empty()) {
        ImGui::TextDisabled("Choose a skeletal model in Mesh details.");
    } else if (!m_previewError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, .35f, .3f, 1.0f), "Preview load failed: %s",
                           m_previewError.c_str());
    } else if (m_previewModel) {
        ImGui::Text("%s  |  %zu bones", std::filesystem::path(m_asset.modelAssetPath).filename().string().c_str(),
                    m_previewModel->BoneCount());
        if (!m_asset.animationStates.empty()) {
            ImGui::Text("Graph state: %s", m_previewController.CurrentStateName().c_str());
        } else {
            ImGui::TextColored(ImVec4(1.0f, .72f, .25f, 1.0f),
                "Bind pose - capture or create an Animation Graph.");
        }
    }
    if (ImGui::Button(m_previewPlaying ? "Pause" : "Play")) m_previewPlaying = !m_previewPlaying;
    ImGui::SameLine();
    if (ImGui::Button("Reset View")) { m_previewYaw = 0.0f; m_previewPitch = 0.0f; m_previewZoom = 1.0f; m_previewTime = 0.0f; }
    ImGui::Checkbox("Show Collider", &m_showColliderGuide);
    ImGui::EndChild(); ImGui::SameLine();

    ImGui::BeginChild("CharacterDetails", ImVec2(0, 0), true);
    ImGui::TextDisabled("DETAILS");
    bool changed = false;
    // Searchable Content-folder asset picker (shared by the Mesh + Animation tabs).
    const auto drawPicker = [&](const char* label, const char* popupId,
                                std::array<char, 128>& search,
                                const std::vector<AssetChoice>& choices,
                                std::string& value) {
        bool picked = false;
        const std::string preview = value.empty()
            ? std::string("None") : std::filesystem::path(value).filename().string();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo(label, preview.c_str())) {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint(popupId, "Search assets...", search.data(), search.size());
            ImGui::Separator();
            if (ImGui::Selectable("None", value.empty())) {
                value.clear(); picked = true; ImGui::CloseCurrentPopup();
            }
            const std::string filter = Lower(search.data());
            for (const AssetChoice& choice : choices) {
                if (!filter.empty() && Lower(choice.displayName).find(filter) == std::string::npos
                    && Lower(choice.path).find(filter) == std::string::npos) continue;
                const bool selected = value == choice.path;
                ImGui::PushID(choice.path.c_str());
                if (ImGui::Selectable(choice.displayName.c_str(), selected)) {
                    value = choice.path; picked = true; ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", choice.path.c_str());
                ImGui::PopID();
            }
            if (choices.empty()) ImGui::TextDisabled("No matching assets in Content");
            ImGui::EndCombo();
        }
        return picked;
    };
    if (m_component == 0) {
        changed |= ImGui::InputText("Name", m_nameBuffer.data(), m_nameBuffer.size());
        if (changed) m_asset.name = m_nameBuffer.data();
        ImGui::TextWrapped("A reusable character asset combines the visible mesh with movement, collision, animation and gameplay setup.");
    } else if (m_component == 1) {
        const std::string previousModelPath = m_asset.modelAssetPath;
        changed |= drawPicker("Skeletal Model", "##ModelSearch", m_modelSearch,
                              m_modelChoices, m_asset.modelAssetPath);
        changed |= drawPicker("Material", "##MaterialSearch", m_materialSearch,
                              m_materialChoices, m_asset.materialAssetPath);
        if (ImGui::Button("Refresh Asset Lists")) RefreshAssetChoices(assetRoot);
        ImGui::TextDisabled("Type in an opened list to filter by name or folder.");
        if (changed) {
            Copy(m_modelBuffer, m_asset.modelAssetPath);
            Copy(m_materialBuffer, m_asset.materialAssetPath);
            if (m_asset.modelAssetPath != previousModelPath) ResetPreviewModel();
        }

        ImGui::SeparatorText("Model Transform");
        ImGui::TextDisabled("Render-only: moves/rotates/scales the mesh, not the collider.");
        const char* modes[] = { "Move", "Rotate", "Scale" };
        for (int i = 0; i < 3; ++i) {
            if (i > 0) ImGui::SameLine();
            if (ImGui::RadioButton(modes[i], m_gizmoMode == i)) m_gizmoMode = i;
        }
        bool transformChanged = false;
        transformChanged |= ImGui::DragFloat3("Offset Pos", &m_asset.modelOffsetPosition.x, 0.01f, -1000.0f, 1000.0f);
        transformChanged |= ImGui::DragFloat3("Model Rot (deg)", &m_asset.modelOrientationEuler.x, 0.5f, -180.0f, 180.0f);
        transformChanged |= ImGui::DragFloat3("Model Scale", &m_asset.modelOffsetScale.x, 0.01f, 0.001f, 100.0f);
        if (ImGui::SmallButton("Stand up (Z-up)")) {
            m_asset.modelOrientationEuler = glm::vec3(-90.0f, 0.0f, 0.0f);
            transformChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset Transform")) {
            m_asset.modelOffsetPosition = glm::vec3(0.0f);
            m_asset.modelOrientationEuler = glm::vec3(0.0f);
            m_asset.modelOffsetScale = glm::vec3(1.0f);
            transformChanged = true;
        }
        ImGui::TextDisabled("Drag the coloured handles in the preview, or edit the fields.");
        if (transformChanged) { changed = true; m_dirty = true; }
    } else if (m_component == 2) {
        auto& collider = m_asset.collider;
        changed |= ImGui::Checkbox("Collider Enabled", &m_asset.colliderEnabled);
        if (!m_asset.colliderEnabled) ImGui::BeginDisabled();
        int shape = std::clamp(static_cast<int>(collider.shape), 0, 8);
        const char* shapes[] = {"Sphere", "Plane", "Box", "Capsule", "Cylinder", "Cone", "Pyramid", "Torus", "Staircase"};
        if (ImGui::Combo("Collider Shape", &shape, shapes, IM_ARRAYSIZE(shapes))) {
            collider.shape = static_cast<engine::ecs::ColliderShape>(shape); changed = true;
        }
        if (collider.shape == engine::ecs::ColliderShape::Sphere) {
            changed |= ImGui::DragFloat("Radius", &collider.radius, .01f, .001f, 1000.0f);
        } else if (collider.shape == engine::ecs::ColliderShape::Box
            || collider.shape == engine::ecs::ColliderShape::Pyramid
            || collider.shape == engine::ecs::ColliderShape::Staircase) {
            changed |= ImGui::DragFloat3("Half Extents", &collider.halfExtents.x, .01f, .001f, 1000.0f);
            if (collider.shape == engine::ecs::ColliderShape::Staircase)
                changed |= ImGui::DragInt("Steps", &collider.steps, .1f, 1, 64);
        } else if (collider.shape == engine::ecs::ColliderShape::Capsule
            || collider.shape == engine::ecs::ColliderShape::Cylinder
            || collider.shape == engine::ecs::ColliderShape::Cone) {
            changed |= ImGui::DragFloat("Radius", &collider.radius, .01f, .001f, 1000.0f);
            changed |= ImGui::DragFloat("Half Height", &collider.halfHeight, .01f, 0.0f, 1000.0f);
            if (collider.shape != engine::ecs::ColliderShape::Capsule)
                collider.halfExtents = glm::vec3(collider.radius, collider.halfHeight, collider.radius);
            ImGui::TextDisabled("Total capsule height = 2 x (Radius + Half Height)");
        } else if (collider.shape == engine::ecs::ColliderShape::Torus) {
            changed |= ImGui::DragFloat("Major Radius", &collider.majorRadius, .01f, .001f, 1000.0f);
            changed |= ImGui::DragFloat("Minor Radius", &collider.minorRadius, .01f, .001f, 1000.0f);
            const float outer = collider.majorRadius + collider.minorRadius;
            collider.halfExtents = glm::vec3(outer, collider.minorRadius, outer);
        } else if (collider.shape == engine::ecs::ColliderShape::Plane) {
            changed |= ImGui::DragFloat3("Plane Normal", &collider.planeNormal.x, .01f);
            changed |= ImGui::DragFloat("Plane Offset", &collider.planeOffset, .01f);
        }
        if (ImGui::Button("Copy Controller Capsule Size")) {
            collider.shape = engine::ecs::ColliderShape::Capsule;
            collider.radius = std::max(m_asset.playerController.capsuleRadius, .001f);
            collider.halfHeight = std::max(m_asset.playerController.capsuleHeight * .5f - collider.radius, 0.0f);
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Use Size for Controller") && collider.shape == engine::ecs::ColliderShape::Capsule) {
            m_asset.playerController.capsuleRadius = collider.radius;
            m_asset.playerController.capsuleHeight = 2.0f * (collider.halfHeight + collider.radius);
            changed = true;
        }
        ImGui::SeparatorText("Collision Response");
        int preset = -1;
        for (int i=0; i<IM_ARRAYSIZE(kCharacterPresets); ++i) {
            const auto& p = kCharacterPresets[i];
            if (collider.layer == p.layer && collider.mask == p.mask && collider.isTrigger == p.trigger) { preset=i; break; }
        }
        if (ImGui::BeginCombo("Collision Preset", preset >= 0 ? kCharacterPresets[preset].name : "Custom")) {
            for (int i=0; i<IM_ARRAYSIZE(kCharacterPresets); ++i) {
                if (ImGui::Selectable(kCharacterPresets[i].name, preset == i)) {
                    collider.layer=kCharacterPresets[i].layer; collider.mask=kCharacterPresets[i].mask;
                    collider.isTrigger=kCharacterPresets[i].trigger; changed=true;
                }
            }
            ImGui::EndCombo();
        }
        int channel=0;
        for (int i=0; i<IM_ARRAYSIZE(kCharacterChannels); ++i) if (collider.layer == kCharacterChannels[i].bit) { channel=i; break; }
        if (ImGui::BeginCombo("Object Channel", kCharacterChannels[channel].name)) {
            for (int i=0; i<IM_ARRAYSIZE(kCharacterChannels); ++i)
                if (ImGui::Selectable(kCharacterChannels[i].name, channel == i)) {
                    collider.layer=kCharacterChannels[i].bit; changed=true;
                }
            ImGui::EndCombo();
        }
        changed |= ImGui::Checkbox("Overlap Only (Trigger)", &collider.isTrigger);
        if (ImGui::TreeNodeEx("Channel Responses (Block / Ignore)", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const auto& response : kCharacterChannels) {
                ImGui::PushID(static_cast<int>(response.bit));
                bool blocks = (collider.mask & response.bit) != 0u;
                if (ImGui::Checkbox(response.name, &blocks)) {
                    if (blocks) collider.mask |= response.bit; else collider.mask &= ~response.bit;
                    changed = true;
                }
                ImGui::SameLine(); ImGui::TextDisabled(blocks ? "Block" : "Ignore");
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::SeparatorText("Surface");
        changed |= ImGui::SliderFloat("Restitution", &collider.restitution, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Friction", &collider.friction, 0.0f, 2.0f);
        if (!m_asset.colliderEnabled) ImGui::EndDisabled();
    } else if (m_component == 3) {
        auto& v=m_asset.playerController;
        changed |= ImGui::Checkbox("Player Controller", &m_asset.playerControllerEnabled);
        changed |= ImGui::DragFloat("Walk Speed", &v.walkSpeed,.05f,0.0f,100.0f);
        changed |= ImGui::DragFloat("Run Speed", &v.runSpeed,.05f,0.0f,100.0f);
        changed |= ImGui::DragFloat("Jump Speed", &v.jumpSpeed,.05f,0.0f,100.0f);
        changed |= ImGui::DragFloat("Step Height", &v.stepHeight,.01f,0.0f,5.0f);
        changed |= ImGui::DragFloat("Max Slope", &v.maxSlopeDegrees,.5f,0.0f,89.0f);
        changed |= ImGui::Checkbox("First Person", &v.firstPerson);
    } else if (m_component == 4) {
        ImGui::SeparatorText("Animation Sources (separate files)");
        ImGui::TextDisabled("Merge clips from external FBX files onto this model by bone name.");
        ImGui::TextDisabled("The animation file must share the model's skeleton (e.g. Mixamo rigs).");
        int removeSource = -1;
        for (std::size_t i = 0; i < m_asset.animationSources.size(); ++i) {
            auto& source = m_asset.animationSources[i];
            ImGui::PushID(9000 + static_cast<int>(i));
            changed |= drawPicker("File", "##AnimFileSearch", m_animSearch, m_modelChoices, source.file);
            std::array<char, 96> nameBuf{}; Copy(nameBuf, source.clipName);
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::InputText("Clip Name", nameBuf.data(), nameBuf.size())) { source.clipName = nameBuf.data(); changed = true; }
            ImGui::SameLine();
            changed |= ImGui::Checkbox("Strip Root Motion", &source.stripRootMotion);
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) removeSource = static_cast<int>(i);
            ImGui::Separator();
            ImGui::PopID();
        }
        if (removeSource >= 0) {
            m_asset.animationSources.erase(m_asset.animationSources.begin() + removeSource);
            changed = true;
        }
        if (ImGui::Button("Add Animation File")) {
            m_asset.animationSources.push_back({});
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh Files")) RefreshAssetChoices(assetRoot);
        ImGui::TextDisabled("Name them Idle / Walk / Run, then use Create 1D Locomotion below.");
        ImGui::TextDisabled("Strip root motion on Walk / Run so the character doesn't slide.");
        ImGui::Spacing();

        ImGui::Text("Animation Graph: %zu states, %zu transitions",
            m_asset.animationStates.size(), m_asset.animationTransitions.size());
        ImGui::TextDisabled("This graph uses the Character Editor's loaded skeletal model.");
        const auto* clips = m_previewModel ? &m_previewModel->Animations() : nullptr;

        ImGui::SeparatorText("States");
        int removeState = -1;
        for (std::size_t i=0; i<m_asset.animationStates.size(); ++i) {
            auto& state = m_asset.animationStates[i];
            ImGui::PushID(1000 + static_cast<int>(i));
            const std::string header = state.name.empty() ? "Unnamed State" : state.name;
            if (ImGui::TreeNodeEx("State", ImGuiTreeNodeFlags_DefaultOpen, "%s", header.c_str())) {
                std::array<char,96> name{}; Copy(name, state.name);
                const std::string oldName = state.name;
                if (ImGui::InputText("Name", name.data(), name.size())) {
                    state.name=name.data();
                    for (auto& t : m_asset.animationTransitions) {
                        if (t.fromState==oldName) t.fromState=state.name;
                        if (t.toState==oldName) t.toState=state.name;
                    }
                    changed=true;
                }
                if (clips && !clips->empty()) {
                    state.clipIndex=std::clamp(state.clipIndex,0,static_cast<int>(clips->size()-1));
                    const char* clipLabel=state.clipName.empty()?(*clips)[static_cast<std::size_t>(state.clipIndex)].name.c_str():state.clipName.c_str();
                    if (ImGui::BeginCombo("Clip",clipLabel)) {
                        for (std::size_t ci=0;ci<clips->size();++ci)
                            if (ImGui::Selectable((*clips)[ci].name.c_str(),state.clipIndex==static_cast<int>(ci))) {
                                state.clipIndex=static_cast<int>(ci); state.clipName=(*clips)[ci].name; changed=true;
                            }
                        ImGui::EndCombo();
                    }
                } else ImGui::TextDisabled("Load a skeletal model to choose clips.");
                changed |= ImGui::Checkbox("Loop",&state.loop);
                changed |= ImGui::DragFloat("Speed",&state.speed,.01f,0.0f,5.0f);
                bool blendSpaceEnabled = !state.blendSamples.empty();
                if (ImGui::Checkbox("Use Blend Space 1D", &blendSpaceEnabled)) {
                    if (blendSpaceEnabled) {
                        if (state.blendParameter.empty()) state.blendParameter = "Speed";
                        state.blendSamples.push_back({state.clipIndex, state.clipName, 0.0f});
                        if (clips && clips->size() > 1) {
                            state.blendSamples.push_back({1, (*clips)[1].name, 1.0f});
                        }
                    } else {
                        state.blendSamples.clear();
                    }
                    changed = true;
                }
                if (blendSpaceEnabled) {
                    changed |= ImGui::Checkbox("2D Directional Blend Space", &state.blendSpace2D);
                    changed |= ImGui::Checkbox("Synchronize Animation Cycles", &state.synchronizeBlendSpace);
                    if (ImGui::BeginCombo("Blend Parameter",
                            state.blendParameter.empty() ? "Choose parameter..." : state.blendParameter.c_str())) {
                        for (const auto& parameter : m_asset.animationParameters) {
                            if (parameter.type != EditorScene::AnimationParameter::Type::Float) continue;
                            if (ImGui::Selectable(parameter.name.c_str(), state.blendParameter == parameter.name)) {
                                state.blendParameter = parameter.name; changed = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (state.blendSpace2D) {
                        if (state.blendParameterY.empty()) state.blendParameterY = "Direction";
                        if (ImGui::BeginCombo("Direction Parameter", state.blendParameterY.c_str())) {
                            for (const auto& parameter : m_asset.animationParameters) {
                                if (parameter.type != EditorScene::AnimationParameter::Type::Float) continue;
                                if (ImGui::Selectable(parameter.name.c_str(), state.blendParameterY == parameter.name)) {
                                    state.blendParameterY = parameter.name; changed = true;
                                }
                            }
                            ImGui::EndCombo();
                        }
                    }
                    ImGui::TextDisabled("The parameter blends continuously between sample positions.");
                    int removeSample = -1;
                    for (std::size_t sampleIndex = 0; sampleIndex < state.blendSamples.size(); ++sampleIndex) {
                        auto& sample = state.blendSamples[sampleIndex];
                        ImGui::PushID(5000 + static_cast<int>(sampleIndex));
                        ImGui::Text("Sample %zu", sampleIndex + 1); ImGui::SameLine();
                        if (ImGui::SmallButton("Remove")) removeSample = static_cast<int>(sampleIndex);
                        if (clips && !clips->empty()) {
                            sample.clipIndex = std::clamp(sample.clipIndex, 0, static_cast<int>(clips->size() - 1));
                            const char* sampleLabel = sample.clipName.empty()
                                ? (*clips)[static_cast<std::size_t>(sample.clipIndex)].name.c_str()
                                : sample.clipName.c_str();
                            if (ImGui::BeginCombo("Animation", sampleLabel)) {
                                for (std::size_t clipIndex = 0; clipIndex < clips->size(); ++clipIndex) {
                                    if (ImGui::Selectable((*clips)[clipIndex].name.c_str(),
                                            sample.clipIndex == static_cast<int>(clipIndex))) {
                                        sample.clipIndex = static_cast<int>(clipIndex);
                                        sample.clipName = (*clips)[clipIndex].name;
                                        changed = true;
                                    }
                                }
                                ImGui::EndCombo();
                            }
                        }
                        changed |= ImGui::DragFloat("Axis Value", &sample.value, 0.05f);
                        if (state.blendSpace2D)
                            changed |= ImGui::DragFloat("Direction (deg)", &sample.valueY, 1.0f, -180.0f, 180.0f);
                        ImGui::PopID();
                    }
                    if (removeSample >= 0) {
                        state.blendSamples.erase(state.blendSamples.begin() + removeSample); changed = true;
                    }
                    if (ImGui::Button("Add Blend Sample")) {
                        const int clipIndex = clips && !clips->empty()
                            ? std::min(static_cast<int>(state.blendSamples.size()), static_cast<int>(clips->size() - 1)) : 0;
                        const std::string clipName = clips && !clips->empty()
                            ? (*clips)[static_cast<std::size_t>(clipIndex)].name : std::string{};
                        const float value = state.blendSamples.empty() ? 0.0f : state.blendSamples.back().value + 1.0f;
                        state.blendSamples.push_back({clipIndex, clipName, value, 0.0f}); changed = true;
                    }
                }
                if (ImGui::Button("Remove State")) removeState=static_cast<int>(i);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (removeState>=0) {
            const std::string removed=m_asset.animationStates[static_cast<std::size_t>(removeState)].name;
            m_asset.animationStates.erase(m_asset.animationStates.begin()+removeState);
            m_asset.animationTransitions.erase(std::remove_if(m_asset.animationTransitions.begin(),m_asset.animationTransitions.end(),
                [&](const auto& t){return t.fromState==removed||t.toState==removed;}),m_asset.animationTransitions.end());
            changed=true;
        }
        if (ImGui::Button("Add State")) {
            EditorScene::AnimationStateNode state;
            state.name="State "+std::to_string(m_asset.animationStates.size()+1);
            if (clips && !clips->empty()) state.clipName=clips->front().name;
            m_asset.animationStates.push_back(std::move(state)); changed=true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Create 1D Locomotion")) {
            const bool hasSpeed = std::any_of(m_asset.animationParameters.begin(), m_asset.animationParameters.end(),
                [](const auto& parameter) { return parameter.name == "Speed"; });
            if (!hasSpeed) {
                m_asset.animationParameters.push_back({"Speed", EditorScene::AnimationParameter::Type::Float, 0.0f});
            }
            EditorScene::AnimationStateNode locomotion;
            locomotion.name = "Locomotion";
            locomotion.blendParameter = "Speed";
            const int clipCount = clips ? static_cast<int>(clips->size()) : 0;
            const auto addSample = [&](int index, float value) {
                index = clipCount > 0 ? std::clamp(index, 0, clipCount - 1) : 0;
                const std::string name = clipCount > 0 ? (*clips)[static_cast<std::size_t>(index)].name : std::string{};
                locomotion.blendSamples.push_back({index, name, value});
            };
            addSample(0, 0.0f); addSample(clipCount > 1 ? 1 : 0, 2.0f); addSample(clipCount > 2 ? 2 : 0, 6.0f);
            locomotion.clipIndex = locomotion.blendSamples.front().clipIndex;
            locomotion.clipName = locomotion.blendSamples.front().clipName;
            m_asset.animationStates.push_back(std::move(locomotion)); changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Create 2D Directional Locomotion")) {
            const auto ensureFloat = [&](const char* name) {
                if (std::none_of(m_asset.animationParameters.begin(), m_asset.animationParameters.end(),
                    [&](const auto& parameter) { return parameter.name == name; }))
                    m_asset.animationParameters.push_back({name, EditorScene::AnimationParameter::Type::Float, 0.0f});
            };
            ensureFloat("Speed"); ensureFloat("Direction"); ensureFloat("Acceleration");
            ensureFloat("Deceleration"); ensureFloat("TurnRate"); ensureFloat("VerticalSpeed");
            const auto ensureBool = [&](const char* name, bool defaultValue) {
                if (std::none_of(m_asset.animationParameters.begin(), m_asset.animationParameters.end(),
                    [&](const auto& parameter) { return parameter.name == name; }))
                    m_asset.animationParameters.push_back({name, EditorScene::AnimationParameter::Type::Bool,
                        defaultValue ? 1.0f : 0.0f});
            };
            ensureBool("IsMoving", false); ensureBool("IsStopping", false);
            ensureBool("IsGrounded", true); ensureBool("IsFalling", false);
            EditorScene::AnimationStateNode locomotion;
            locomotion.name="Directional Locomotion"; locomotion.blendParameter="Speed";
            locomotion.blendParameterY="Direction"; locomotion.blendSpace2D=true;
            locomotion.synchronizeBlendSpace=true;
            const int clipCount=clips?static_cast<int>(clips->size()):0;
            const auto sample=[&](int index,float speed,float direction){
                index=clipCount>0?std::clamp(index,0,clipCount-1):0;
                locomotion.blendSamples.push_back({index,clipCount>0?(*clips)[static_cast<std::size_t>(index)].name:std::string{},speed,direction});
            };
            sample(0,0.0f,0.0f); sample(1,2.0f,0.0f); sample(2,6.0f,0.0f);
            sample(3,2.0f,-90.0f); sample(4,2.0f,90.0f); sample(5,2.0f,180.0f);
            locomotion.clipIndex=locomotion.blendSamples.front().clipIndex;
            locomotion.clipName=locomotion.blendSamples.front().clipName;
            m_asset.animationStates.push_back(std::move(locomotion)); changed=true;
        }

        ImGui::SeparatorText("Parameters");
        int removeParameter=-1;
        const char* parameterTypes[]={"Float","Bool","Trigger"};
        for (std::size_t i=0;i<m_asset.animationParameters.size();++i) {
            auto& parameter=m_asset.animationParameters[i];
            ImGui::PushID(2000+static_cast<int>(i));
            std::array<char,64> name{}; Copy(name,parameter.name); const std::string oldName=parameter.name;
            if (ImGui::InputText("Name",name.data(),name.size())) {
                parameter.name=name.data();
                for(auto& t:m_asset.animationTransitions) if(t.parameter==oldName)t.parameter=parameter.name;
                for(auto& s:m_asset.animationStates) if(s.blendParameter==oldName)s.blendParameter=parameter.name;
                for(auto& s:m_asset.animationStates) if(s.blendParameterY==oldName)s.blendParameterY=parameter.name;
                changed=true;
            }
            int type=std::clamp(static_cast<int>(parameter.type),0,2);
            if(ImGui::Combo("Type",&type,parameterTypes,3)){parameter.type=static_cast<EditorScene::AnimationParameter::Type>(type);changed=true;}
            if(parameter.type==EditorScene::AnimationParameter::Type::Float)
                changed|=ImGui::DragFloat("Default",&parameter.defaultValue,.05f);
            else { bool v=parameter.defaultValue!=0; if(ImGui::Checkbox("Default",&v)){parameter.defaultValue=v?1.0f:0.0f;changed=true;} }
            if(ImGui::Button("Remove Parameter"))removeParameter=static_cast<int>(i);
            ImGui::Separator(); ImGui::PopID();
        }
        if(removeParameter>=0){
            const std::string removed=m_asset.animationParameters[static_cast<std::size_t>(removeParameter)].name;
            m_asset.animationParameters.erase(m_asset.animationParameters.begin()+removeParameter);
            m_asset.animationTransitions.erase(std::remove_if(m_asset.animationTransitions.begin(),m_asset.animationTransitions.end(),
                [&](const auto& t){return t.parameter==removed;}),m_asset.animationTransitions.end()); changed=true;
        }
        if(ImGui::Button("Add Parameter")){m_asset.animationParameters.push_back({"Speed",EditorScene::AnimationParameter::Type::Float,0.0f});changed=true;}

        ImGui::SeparatorText("Transitions");
        int removeTransition=-1;
        const char* compares[]={">=","<","==","!="};
        for(std::size_t i=0;i<m_asset.animationTransitions.size();++i){
            auto& transition=m_asset.animationTransitions[i]; ImGui::PushID(3000+static_cast<int>(i));
            auto stateCombo=[&](const char* label,std::string& value,bool any){
                if(ImGui::BeginCombo(label,value.empty()?(any?"Any State":"None"):value.c_str())){
                    if(any&&ImGui::Selectable("Any State",value.empty())){value.clear();changed=true;}
                    for(const auto& s:m_asset.animationStates)if(ImGui::Selectable(s.name.c_str(),value==s.name)){value=s.name;changed=true;}
                    ImGui::EndCombo();
                }
            };
            stateCombo("From",transition.fromState,true); stateCombo("To",transition.toState,false);
            if(ImGui::BeginCombo("Parameter",transition.parameter.empty()?"None":transition.parameter.c_str())){
                for(const auto& p:m_asset.animationParameters)if(ImGui::Selectable(p.name.c_str(),transition.parameter==p.name)){transition.parameter=p.name;changed=true;}
                ImGui::EndCombo();
            }
            int compare=std::clamp(static_cast<int>(transition.compare),0,3);
            if(ImGui::Combo("Compare",&compare,compares,4)){transition.compare=static_cast<EditorScene::AnimationStateTransition::Compare>(compare);changed=true;}
            changed|=ImGui::DragFloat("Threshold",&transition.threshold,.05f);
            changed|=ImGui::DragFloat("Fade",&transition.fade,.01f,0.0f,5.0f);
            changed|=ImGui::SliderFloat("Exit Time",&transition.exitTime,0.0f,1.0f);
            changed|=ImGui::DragInt("Priority",&transition.priority,1.0f,-100,100);
            changed|=ImGui::Checkbox("Interrupt Blend",&transition.canInterrupt);
            if(ImGui::Button("Remove Transition"))removeTransition=static_cast<int>(i);
            ImGui::Separator();ImGui::PopID();
        }
        if(removeTransition>=0){m_asset.animationTransitions.erase(m_asset.animationTransitions.begin()+removeTransition);changed=true;}
        if(ImGui::Button("Add Transition")&&!m_asset.animationStates.empty()){
            EditorScene::AnimationStateTransition t; t.fromState=m_asset.animationStates.front().name;
            t.toState=m_asset.animationStates.size()>1?m_asset.animationStates[1].name:m_asset.animationStates.front().name;
            if(!m_asset.animationParameters.empty())t.parameter=m_asset.animationParameters.front().name;
            m_asset.animationTransitions.push_back(std::move(t));changed=true;
        }

        if(!m_asset.animationStates.empty()){
            ImGui::SeparatorText("Graph View");
            const float canvasWidth=std::max(ImGui::GetContentRegionAvail().x,260.0f);
            const float canvasHeight=std::max(150.0f,85.0f*std::ceil(static_cast<float>(m_asset.animationStates.size())/2.0f));
            const ImVec2 origin=ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("CharacterGraphCanvas",ImVec2(canvasWidth,canvasHeight));
            ImDrawList* draw=ImGui::GetWindowDrawList();
            draw->AddRectFilled(origin,ImVec2(origin.x+canvasWidth,origin.y+canvasHeight),IM_COL32(22,27,35,255),5.0f);
            std::vector<ImVec2> centers;
            for(std::size_t i=0;i<m_asset.animationStates.size();++i)
                centers.emplace_back(origin.x+75.0f+static_cast<float>(i%2)*(canvasWidth-150.0f),origin.y+42.0f+static_cast<float>(i/2)*85.0f);
            auto stateIndex=[&](const std::string& name){for(std::size_t i=0;i<m_asset.animationStates.size();++i)if(m_asset.animationStates[i].name==name)return static_cast<int>(i);return -1;};
            for(const auto& transition:m_asset.animationTransitions){
                const int to=stateIndex(transition.toState); if(to<0)continue;
                const int from=stateIndex(transition.fromState);
                const ImVec2 target=centers[static_cast<std::size_t>(to)];
                const ImVec2 source=from>=0?centers[static_cast<std::size_t>(from)]:ImVec2(origin.x+8.0f,target.y);
                draw->AddLine(source,target,transition.fromState.empty()?IM_COL32(245,175,65,230):IM_COL32(90,155,235,225),2.0f);
            }
            for(std::size_t i=0;i<m_asset.animationStates.size();++i){
                const ImVec2 c=centers[i]; const bool active=m_previewController.CurrentStateName()==m_asset.animationStates[i].name;
                draw->AddRectFilled(ImVec2(c.x-58,c.y-23),ImVec2(c.x+58,c.y+23),active?IM_COL32(42,125,87,255):IM_COL32(55,72,96,255),5.0f);
                draw->AddRect(ImVec2(c.x-58,c.y-23),ImVec2(c.x+58,c.y+23),active?IM_COL32(90,235,155,255):IM_COL32(110,150,205,255),5.0f,0,2.0f);
                draw->AddText(ImVec2(c.x-50,c.y-16),IM_COL32(245,247,250,255),m_asset.animationStates[i].name.c_str());
                const std::string clip=m_asset.animationStates[i].clipName.empty()?"Clip "+std::to_string(m_asset.animationStates[i].clipIndex):m_asset.animationStates[i].clipName;
                draw->AddText(ImVec2(c.x-50,c.y+2),IM_COL32(175,190,210,255),clip.c_str());
            }
        }

        if(changed)m_previewGraphDirty=true;
        if(!m_asset.animationStates.empty()){
            ImGui::SeparatorText("Live Graph Preview");
            ImGui::Text("Active State: %s",m_previewController.CurrentStateName().c_str());
            const auto blendDebug = m_previewController.CurrentBlendSpace();
            if (blendDebug.active && m_previewModel) {
                ImGui::TextDisabled("Active Blend Samples");
                const auto& animations = m_previewModel->Animations();
                for (const auto& sample : blendDebug.samples) {
                    const char* name = sample.clip >= 0 && sample.clip < static_cast<int>(animations.size())
                        ? animations[static_cast<std::size_t>(sample.clip)].name.c_str() : "Invalid clip";
                    ImGui::ProgressBar(sample.weight, ImVec2(-1.0f, 0.0f), name);
                }
            }
            for(const auto& parameter:m_asset.animationParameters){
                ImGui::PushID(("preview_"+parameter.name).c_str()); float& value=m_previewGraphParameters[parameter.name];
                if(parameter.type==EditorScene::AnimationParameter::Type::Float)ImGui::DragFloat(parameter.name.c_str(),&value,.05f);
                else if(parameter.type==EditorScene::AnimationParameter::Type::Bool){bool v=value!=0;if(ImGui::Checkbox(parameter.name.c_str(),&v))value=v?1.0f:0.0f;}
                else if(ImGui::Button(("Trigger "+parameter.name).c_str()))m_previewController.SetTriggerParameter(parameter.name);
                ImGui::PopID();
            }
        }
    } else if (m_component == 5) {
        changed |= ImGui::Checkbox("Health", &m_asset.healthEnabled);
        changed |= ImGui::DragFloat("HP", &m_asset.health.hp,1,0,100000);
        changed |= ImGui::DragFloat("Max HP", &m_asset.health.maxHp,1,1,100000);
    } else if (m_component == 6) {
        changed |= ImGui::Checkbox("AI Agent", &m_asset.navAgentEnabled);
        changed |= ImGui::DragFloat("Speed",&m_asset.navSpeed,.05f,0,100);
        changed |= ImGui::DragFloat("Reach Radius",&m_asset.navReachRadius,.01f,.01f,20);
        changed |= ImGui::DragFloat("Vision Range",&m_asset.navVisionRange,.1f,0,1000);
        changed |= ImGui::InputText("Behavior Tree",m_behaviorBuffer.data(),m_behaviorBuffer.size());
        changed |= ImGui::Checkbox("Auto Target",&m_asset.navAutoTarget);
        if (changed) m_asset.behaviorTreeAsset=m_behaviorBuffer.data();
    } else {
        changed |= ImGui::Checkbox("Script Enabled",&m_asset.scriptEnabled);
        changed |= ImGui::InputText("Class",m_scriptClassBuffer.data(),m_scriptClassBuffer.size());
        changed |= ImGui::InputText("Path",m_scriptPathBuffer.data(),m_scriptPathBuffer.size());
        if (changed) { m_asset.scriptClassName=m_scriptClassBuffer.data(); m_asset.scriptPath=m_scriptPathBuffer.data(); }
    }
    if (changed) {
        m_dirty = true;
        if (m_component == 2) m_colliderGuideDirty = true;
    }
    ImGui::Separator();
    ImGui::TextColored(m_dirty ? ImVec4(1.0f,.75f,.2f,1.0f) : ImVec4(.45f,.85f,.55f,1.0f), m_dirty ? "Unsaved changes" : "Asset saved");
    ImGui::EndChild();
    ImGui::End();
}
