#include "CharacterEditorPanel.h"

#include "AnimationClipAsset.h"
#include "AnimationGraphAsset.h"
#include "AnimationGraphBuilder.h"
#include "EditorPanels.h"

#include <engine/animation/AnimatedModel.h>
#include <engine/animation/Animator.h>
#include <engine/assets/IKRigAsset.h>
#include <engine/graphics/Camera.h>
#include <engine/graphics/Model.h>
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

std::string StoredScriptPath(const std::filesystem::path& absolutePath) {
    std::error_code ec;
    const std::filesystem::path relative = std::filesystem::relative(
        absolutePath, std::filesystem::current_path(ec), ec);
    return ec ? absolutePath.lexically_normal().generic_string()
              : relative.lexically_normal().generic_string();
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

bool CharacterEditorPanel::SaveForShutdown(const std::string& assetRoot, std::string* error) {
    if (m_path.empty()) {
        std::string name = m_asset.name.empty() ? "NewCharacter" : m_asset.name;
        for (char& c : name) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') c = '_';
        }
        m_path = (std::filesystem::path(assetRoot) / "GameAssets" / "Characters"
            / (name + ".3dgcharacter")).string();
    }
    if (std::filesystem::path(m_path).extension() != ".3dgcharacter") m_path += ".3dgcharacter";
    if (!m_asset.Save(m_path, error)) return false;
    m_dirty = false;
    SyncBuffers();
    return true;
}

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

    // Prefer the referenced graph's animation data; fall back to the character's own inline
    // states for legacy (graph-less) characters.
    const std::vector<EditorScene::AnimationStateNode>& states =
        m_previewUsingGraph ? m_previewGraphStates : m_asset.animationStates;
    const std::vector<EditorScene::AnimationParameter>& parameters =
        m_previewUsingGraph ? m_previewGraphParamDefs : m_asset.animationParameters;
    const std::vector<EditorScene::AnimationStateTransition>& transitions =
        m_previewUsingGraph ? m_previewGraphTransitions : m_asset.animationTransitions;
    if (!m_previewModel || states.empty()) return;

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
    const auto baseSpeed = [&](int fallback, const std::string& name) {
        if (m_previewUsingGraph) {
            for (const auto& source : m_previewGraphSources)
                if (source.name == name) return std::max(source.basePlaybackSpeed, 0.0f);
            if (fallback >= 0 && fallback < static_cast<int>(m_previewGraphSources.size()))
                return std::max(m_previewGraphSources[static_cast<std::size_t>(fallback)].basePlaybackSpeed, 0.0f);
        } else {
            for (const auto& source : m_asset.animationSources)
                if (source.clipName == name) return std::max(source.basePlaybackSpeed, 0.0f);
            if (fallback >= 0 && fallback < static_cast<int>(m_asset.animationSources.size()))
                return std::max(m_asset.animationSources[static_cast<std::size_t>(fallback)].basePlaybackSpeed, 0.0f);
        }
        return 1.0f;
    };
    editor::BuildAnimationController(m_previewController,
        states, parameters, transitions, resolveClip, duration, {}, baseSpeed);

    // The preview UI keeps its own editable copy of each parameter's value.
    for (const auto& parameter : parameters) {
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
    if (!m_attachmentShader) {
        static const char* vertex = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
uniform mat4 uModel;
uniform mat4 uViewProj;
uniform mat3 uNormalMat;
out vec3 vWorld; out vec3 vNormal; out vec2 vUV;
void main(){ vWorld=vec3(uModel*vec4(aPos,1.0)); vNormal=uNormalMat*aNormal; vUV=aUV;
             gl_Position=uViewProj*vec4(vWorld,1.0); }
)GLSL";
        static const char* fragment = R"GLSL(
#version 330 core
in vec3 vWorld; in vec3 vNormal; in vec2 vUV;
uniform vec3 uColor; uniform vec3 uSpecular; uniform vec3 uEmissive; uniform float uShininess;
uniform int uHasDiffuse; uniform sampler2D uDiffuseTex;
uniform vec3 uLightPos; uniform vec3 uLightColor; uniform vec3 uViewPos;
out vec4 FragColor;
void main(){
    vec3 base = uColor;
    if (uHasDiffuse == 1) base *= texture(uDiffuseTex, vUV).rgb;
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightPos - vWorld);
    vec3 V = normalize(uViewPos - vWorld);
    vec3 H = normalize(L + V);
    float diff = max(dot(N, L), 0.0);
    float spec = diff > 0.0 ? pow(max(dot(N, H), 0.0), max(uShininess, 1.0)) : 0.0;
    vec3 color = base * (0.28 + diff * uLightColor) + uSpecular * spec + uEmissive;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
)GLSL";
        engine::ShaderCompileReport report;
        m_attachmentShader = engine::Shader::TryCompile(vertex, fragment, report);
    }

    // A graph-driven character keeps its animation in a referenced .3dggraph; load it so the
    // preview shows the graph's clips + idle. Its own inline animationStates stay empty, so
    // without this the preview would fall back to a static bind pose.
    if (m_previewGraphPath != m_asset.animationGraphPath
        || m_previewClipMetadataInvalidated) {
        const bool graphPathChanged = m_previewGraphPath != m_asset.animationGraphPath;
        m_previewClipMetadataInvalidated = false;
        m_previewGraphPath = m_asset.animationGraphPath;
        m_previewUsingGraph = false;
        m_previewGraphSources.clear();
        m_previewGraphClipAssets.clear();
        m_previewGraphStates.clear();
        m_previewGraphParamDefs.clear();
        m_previewGraphTransitions.clear();
        if (!m_asset.animationGraphPath.empty()) {
            AnimationGraphAsset graph;
            std::string graphError;
            if (graph.Load(m_asset.animationGraphPath, &graphError)) {
                m_previewUsingGraph = true;
                for (const AnimationGraphClip& c : graph.clips) {
                    AnimationClipAsset clip;
                    const float base = clip.Load(c.clipAsset, nullptr)
                        ? std::max(clip.speed, 0.0f) : 1.0f;
                    if (!c.sourceFile.empty())
                        m_previewGraphSources.push_back(
                            {c.sourceFile, c.clipName, c.stripRootMotion, c.sourceClipName, base});
                    m_previewGraphClipAssets.push_back(c.clipAsset);
                }
                m_previewGraphStates = graph.states;
                m_previewGraphParamDefs = graph.parameters;
                m_previewGraphTransitions = graph.transitions;
            } else if (!graphError.empty()) {
                m_previewError = graphError;
            }
        }
        if (graphPathChanged)
            m_previewAnimSignature.clear(); // graph source layout changed
    }

    std::string clipMetadataSignature;
    for (const std::string& path : m_previewGraphClipAssets) {
        std::error_code ec;
        const auto stamp = std::filesystem::last_write_time(path, ec);
        if (!ec) clipMetadataSignature += path + '@' + std::to_string(stamp.time_since_epoch().count());
    }
    if (!clipMetadataSignature.empty()
        && clipMetadataSignature != m_previewClipMetadataSignature) {
        const bool first = m_previewClipMetadataSignature.empty();
        m_previewClipMetadataSignature = std::move(clipMetadataSignature);
        if (!first) m_previewClipMetadataInvalidated = true;
        m_previewGraphDirty = true;
    }

    // Reload the preview model when the model OR its merged animation sources change. Use the
    // graph's clips when a graph is referenced, otherwise the character's inline sources.
    std::vector<engine::RuntimeAssetManager::SkinnedAnimationSource> sources;
    if (m_previewUsingGraph) {
        sources = m_previewGraphSources;
    } else {
        for (const auto& source : m_asset.animationSources)
            if (!source.file.empty())
                sources.push_back({source.file, source.clipName, source.stripRootMotion});
    }
    std::string animSignature;
    for (const auto& source : sources) {
        animSignature += source.path + '|' + source.name + '|' +
                         (source.stripRootMotion ? "1" : "0") + '|' + source.sourceName + '\n';
    }
    if (m_previewModelPath != m_asset.modelAssetPath || m_previewAnimSignature != animSignature) {
        ResetPreviewModel();
        m_previewModelPath = m_asset.modelAssetPath;
        m_previewAnimSignature = animSignature;
        if (!m_previewModelPath.empty()) {
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

    m_previewSocketWorld.clear();
    m_previewSocketParentWorld.clear();
    if (m_previewModel) {
        const auto& clips = m_previewModel->Animations();
        const std::vector<EditorScene::AnimationParameter>& previewParams =
            m_previewUsingGraph ? m_previewGraphParamDefs : m_asset.animationParameters;
        const bool haveGraphStates =
            m_previewUsingGraph ? !m_previewGraphStates.empty() : !m_asset.animationStates.empty();
        if (!clips.empty() && haveGraphStates) {
            for (const auto& parameter : previewParams) {
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
                            else if (!space.synchronized)
                                sampleTime *= std::max(weighted.basePlaybackSpeed, 0.0f);
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

        // Render-only model offset (pos/rot/scale about the model centre). For a fresh
        // Z-up rig with no offset set yet, preview an automatic stand-up so it matches
        // what dropping it into the scene will do.
        const float radius = std::max(m_previewModel->BoundingRadius(), 0.001f);
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

        // Character instances are automatically converted from their imported
        // mesh units to a nominal 1.8 m character when they are added to a scene.
        // Apply that same conversion to the preview mesh only. Colliders and the
        // controller are already authored in world metres and must not inherit the
        // FBX/model import scale.
        const float importedHeight = std::max(std::max(boundsSize.y, boundsSize.z), 0.001f);
        const float previewSceneScale = 1.8f / importedHeight;

        // Frame the visible character and its collider as one object. Previously
        // the camera fitted only the mesh, so a tall/wide capsule extended beyond
        // the preview and only tiny pieces of its wireframe remained visible.
        const float modelScale = std::max({
            std::abs(previewScale.x), std::abs(previewScale.y),
            std::abs(previewScale.z)});
        float framingRadius = previewSceneScale * (radius * modelScale
            + glm::length(m_asset.modelOffsetPosition));
        if (m_showColliderGuide && m_asset.colliderEnabled) {
            const auto& c = m_asset.collider;
            float colliderRadius = 0.0f;
            switch (c.shape) {
            case engine::ecs::ColliderShape::Sphere:
                colliderRadius = c.radius; break;
            case engine::ecs::ColliderShape::Capsule:
                colliderRadius = c.halfHeight + c.radius; break;
            case engine::ecs::ColliderShape::Cylinder:
            case engine::ecs::ColliderShape::Cone:
                colliderRadius = std::sqrt(c.radius * c.radius
                    + c.halfHeight * c.halfHeight); break;
            case engine::ecs::ColliderShape::Box:
            case engine::ecs::ColliderShape::Pyramid:
            case engine::ecs::ColliderShape::Staircase:
                colliderRadius = glm::length(c.halfExtents); break;
            case engine::ecs::ColliderShape::Torus:
                colliderRadius = c.majorRadius + c.minorRadius; break;
            case engine::ecs::ColliderShape::Plane:
                break;
            }
            framingRadius = std::max(framingRadius, colliderRadius);
        }
        const float fit = (0.88f * m_previewZoom)
            / std::max(framingRadius, 0.001f);
        // View framing: orbit + fit-scale. The offset axes live in this frame, so the
        // gizmo handles follow the orbit.
        glm::mat4 frame(1.0f);
        frame = glm::rotate(frame, glm::radians(m_previewYaw), glm::vec3(0, 1, 0));
        frame = glm::rotate(frame, glm::radians(m_previewPitch), glm::vec3(1, 0, 0));
        frame = glm::scale(frame, glm::vec3(fit));

        glm::mat4 modelFrame = glm::scale(frame, glm::vec3(previewSceneScale));
        glm::mat4 offset(1.0f);
        offset = glm::translate(offset, m_asset.modelOffsetPosition);
        offset *= glm::mat4_cast(glm::quat(glm::radians(previewEuler)));
        offset = glm::scale(offset, previewScale);
        offset = glm::translate(offset, -m_previewModel->Center());

        const glm::mat4 model = modelFrame * offset;
        engine::Camera camera(glm::vec3(0.0f, 0.0f, 2.5f));
        camera.LookAt(glm::vec3(0.0f));
        const float aspect = static_cast<float>(width) / static_cast<float>(height);

        // Reflect an assigned .3dgmat in the preview: tint the base colour and, if the
        // material has an albedo map, show it. Cached, so this is cheap after first load.
        glm::vec3 materialTint(1.0f);
        const engine::Texture* materialAlbedo = nullptr;
        if (!m_asset.materialAssetPath.empty()
            && std::filesystem::path(m_asset.materialAssetPath).extension() == ".3dgmat") {
            std::string materialError;
            if (const engine::RuntimeMaterialAsset* material =
                    m_previewAssets.LoadMaterial(m_asset.materialAssetPath, &materialError)) {
                materialTint = material->material.albedo;
                if (!material->albedoMapPath.empty()) {
                    materialAlbedo = m_previewAssets.LoadTexture(material->albedoMapPath, &materialError);
                }
            }
        }

        m_previewRenderer->Draw(*m_previewModel, m_previewPose, model, camera,
            aspect,
            glm::normalize(glm::vec3(0.45f, -1.0f, -0.35f)),
            glm::vec3(1.0f, 0.96f, 0.90f), glm::vec3(0.16f),
            materialTint, materialAlbedo);

        // Cache the transforms so Draw() can project the gizmo handles onto the image.
        m_previewViewProj = camera.ProjectionMatrix(aspect) * camera.ViewMatrix();
        m_previewModelMatrix = model;
        m_previewGizmoFrame = modelFrame;
        m_previewModelCenter = m_previewModel->Center();

        // Cache every animated socket transform for preview markers, selection and the
        // socket gizmo. This is the same model * bone * bind * offset chain used by
        // attachments and by SocketPosition() at runtime.
        m_previewSocketWorld.clear();
        m_previewSocketParentWorld.clear();
        m_previewSocketWorld.reserve(m_asset.sockets.size());
        m_previewSocketParentWorld.reserve(m_asset.sockets.size());
        const engine::Skeleton& previewSkeleton = m_previewModel->GetSkeleton();
        for (const CharacterSocket& socket : m_asset.sockets) {
            glm::mat4 parent = model;
            const int bone = previewSkeleton.Find(socket.boneName);
            if (bone >= 0 && bone < static_cast<int>(m_previewPose.size())) {
                const glm::mat4 boneBind =
                    glm::inverse(previewSkeleton.bones[static_cast<std::size_t>(bone)].offset);
                parent *= m_previewPose[static_cast<std::size_t>(bone)] * boneBind;
            }
            m_previewSocketParentWorld.push_back(parent);
            m_previewSocketWorld.push_back(parent * engine::MakeAttachmentOffset(
                socket.position, socket.eulerDegrees, socket.scale));
        }

        // Socketed attachments (weapons/props): draw each static model at its bone.
        if (!m_asset.attachments.empty() && m_attachmentShader) {
            engine::AnimatedModel temp;
            temp.pose = m_previewPose;
            const engine::Skeleton& skel = m_previewModel->GetSkeleton();
            for (const CharacterAttachment& a : m_asset.attachments) {
                if (a.modelPath.empty()) continue;
                const CharacterSocket* sock = nullptr;
                for (const CharacterSocket& s : m_asset.sockets) {
                    if (s.name == a.socketName) { sock = &s; break; }
                }
                if (!sock) continue;
                std::string attachError;
                if (const engine::Model* attachModel =
                        m_previewAssets.LoadModel(a.modelPath, &attachError)) {
                    engine::ModelAttachment r;
                    r.model = attachModel;
                    r.bone = skel.Find(sock->boneName);
                    r.boneBind = r.bone >= 0
                        ? glm::inverse(skel.bones[static_cast<std::size_t>(r.bone)].offset)
                        : glm::mat4(1.0f);
                    r.localOffset = engine::MakeAttachmentOffset(sock->position, sock->eulerDegrees, sock->scale);
                    if (!a.materialPath.empty()) {
                        std::string matError;
                        if (const engine::RuntimeMaterialAsset* mat =
                                m_previewAssets.LoadMaterial(a.materialPath, &matError)) {
                            r.tint = mat->material.albedo;
                            if (!mat->albedoMapPath.empty()) {
                                r.albedoOverride = m_previewAssets.LoadTexture(mat->albedoMapPath, &matError);
                            }
                        }
                    }
                    temp.attachments.push_back(r);
                }
            }
            if (!temp.attachments.empty()) {
                glEnable(GL_DEPTH_TEST);
                m_attachmentShader->Bind();
                m_attachmentShader->SetMat4("uViewProj", m_previewViewProj);
                m_attachmentShader->SetVec3("uLightPos", camera.Position() + glm::vec3(2.0f, 4.0f, 3.0f));
                m_attachmentShader->SetVec3("uLightColor", glm::vec3(1.0f));
                m_attachmentShader->SetVec3("uViewPos", camera.Position());
                engine::DrawAnimatedModelAttachments(temp, model, *m_attachmentShader);
            }
        }

        if (m_showColliderGuide && m_asset.colliderEnabled && m_colliderGuideShader) {
            if (m_colliderGuideDirty || !m_colliderGuideMesh) RebuildColliderGuide();
            if (m_colliderGuideMesh) {
                // Draw the collider in the same fitted preview frame as the
                // character.  Scaling it by its own height kept its visible
                // height constant, so changing Height appeared to alter Radius.
                // The mesh offset deliberately is not included: it is render-only
                // while the controller capsule remains at the object origin.
                const auto& guideCollider = m_asset.collider;
                constexpr float units = 1.0f;
                glm::mat4 colliderModel = frame;
                const auto& c = guideCollider;
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
    Copy(m_scriptClassBuffer, m_asset.scriptClassName);
    Copy(m_scriptPathBuffer, m_asset.scriptPath);
}

void CharacterEditorPanel::RefreshAssetChoices(const std::string& assetRoot) {
    m_modelChoices.clear();
    m_staticMeshChoices.clear();
    m_materialChoices.clear();
    m_clipChoices.clear();
    m_graphChoices.clear();
    m_ikRigChoices.clear();
    m_behaviorChoices.clear();
    m_scriptChoices.clear();
    m_scannedAssetRoot = assetRoot;
    std::error_code ec;
    const std::filesystem::path root(assetRoot);
    if (!std::filesystem::exists(root, ec)) return;
    // Skip unreadable folders instead of aborting the whole scan, and use a separate
    // error_code for the per-entry file test so one bad entry can't truncate the list
    // (which would silently drop saved .3dgmat materials from the picker).
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, ec), end;
    while (!ec && it != end) {
        std::error_code fileEc;
        if (it->is_regular_file(fileEc)) {
            const std::filesystem::path& file = it->path();
            const std::string extension = Lower(file.extension().string());
            AssetChoice choice{file.generic_string(), file.filename().string()};
            // Native engine-imported skeletal mesh (.3dgskmesh) as the character's base model.
            if (extension == ".3dgskmesh") {
                // Base animated character mesh.
                m_modelChoices.push_back(std::move(choice));
            } else if (extension == ".3dgmesh") {
                // Static weapon / shield / prop used by socket attachments.
                m_staticMeshChoices.push_back(std::move(choice));
            } else if (extension == ".3dgmat") {
                m_materialChoices.push_back(std::move(choice));
            } else if (extension == ".3dgclip") {
                m_clipChoices.push_back(std::move(choice));
            } else if (extension == ".3dggraph") {
                m_graphChoices.push_back(std::move(choice));
            } else if (extension == ".3dgikrig") {
                m_ikRigChoices.push_back(std::move(choice));
            } else if (extension == ".btgraph") {
                m_behaviorChoices.push_back(std::move(choice));
            } else if ((extension == ".h" || extension == ".lua")
                       && Lower(file.generic_string()).find("/scripts/")
                              != std::string::npos) {
                choice.displayName = file.stem().string();
                choice.path = StoredScriptPath(file);
                m_scriptChoices.push_back(std::move(choice));
            }
        }
        it.increment(ec);
    }
    const auto byName = [](const AssetChoice& a, const AssetChoice& b) {
        return Lower(a.displayName) < Lower(b.displayName);
    };
    std::sort(m_modelChoices.begin(), m_modelChoices.end(), byName);
    std::sort(m_staticMeshChoices.begin(), m_staticMeshChoices.end(), byName);
    std::sort(m_materialChoices.begin(), m_materialChoices.end(), byName);
    std::sort(m_clipChoices.begin(), m_clipChoices.end(), byName);
    std::sort(m_graphChoices.begin(), m_graphChoices.end(), byName);
    std::sort(m_ikRigChoices.begin(), m_ikRigChoices.end(), byName);
    std::sort(m_behaviorChoices.begin(), m_behaviorChoices.end(), byName);
    std::sort(m_scriptChoices.begin(), m_scriptChoices.end(), byName);
}

void CharacterEditorPanel::Draw(EditorScene& scene, const std::string& assetRoot, bool* open,
                                bool* assetSaved, std::string* message,
                                float deltaTime) {
    if (m_scannedAssetRoot != assetRoot) RefreshAssetChoices(assetRoot);
    if (!m_pendingOpen.empty()) {
        std::string error;
        if (m_asset.Load(m_pendingOpen, &error)) { m_path = m_pendingOpen; m_dirty = false; if (message) *message = "Opened character: " + m_path; }
        else if (message) *message = error;
        m_selectedSocket = -1;
        m_selectedAttachment = -1;
        m_pendingOpen.clear(); SyncBuffers(); ResetPreviewModel();
    }
    if (m_path.empty()) {
        m_path = (std::filesystem::path(assetRoot) / "Assets" / "Characters" / "Character.3dgcharacter").string();
        SyncBuffers();
    }
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::CharacterEditor), open, ImGuiWindowFlags_MenuBar)) { ImGui::End(); return; }
    if (ImGui::BeginMenuBar()) {
        if (ImGui::MenuItem("New")) {
            m_asset = {};
            m_path.clear();
            m_dirty = false;
            m_selectedSocket = -1;
            m_selectedAttachment = -1;
            SyncBuffers();
            ResetPreviewModel();
        }
        if (ImGui::MenuItem("Capture Selected", nullptr, false, scene.SelectedObject() != nullptr)) {
            m_asset.Capture(*scene.SelectedObject());
            m_applyTarget.Set(scene.SelectedObject()->entity);
            m_dirty = true;
            m_selectedSocket = -1;
            m_selectedAttachment = -1;
            SyncBuffers();
            ResetPreviewModel();
        }
        const EditorScene::Object* applyObject = m_applyTarget.Resolve(scene);
        const bool validApplyTarget = applyObject
            && (applyObject->skeletalModel || !applyObject->characterAssetPath.empty());
        const std::string applyMenuLabel = validApplyTarget
            ? "Apply Changes to \"" + applyObject->name + "\""
            : "Apply Changes (No Valid Target)";
        if (ImGui::MenuItem(applyMenuLabel.c_str(), nullptr, false, validApplyTarget)) {
            const int oldIndex = scene.SelectedIndex();
            const auto oldSelection = scene.HierarchySelection();
            const auto oldGroup = scene.SelectedGroupId();
            scene.SelectEntity(m_applyTarget.Entity());
            const bool applied = m_asset.Apply(scene);
            if (oldSelection == EditorScene::HierarchySelectionType::Group) scene.SelectGroup(oldGroup);
            else if (oldIndex >= 0) scene.SelectIndex(oldIndex);
            else scene.Deselect();
            if (message) *message = applied
                ? "Applied character setup to " + applyObject->name
                : "Could not apply character setup to " + applyObject->name;
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
    {
        const EditorScene::Object* target = m_applyTarget.Resolve(scene);
        const EditorScene::Object* current = scene.SelectedObject();
        ImGui::SeparatorText("Scene Apply Target");
        if (target) {
            ImGui::Text("Target: %s", target->name.c_str());
            ImGui::TextDisabled("Entity ID: %u", static_cast<unsigned>(target->entity));
        } else if (m_applyTarget.IsSet()) {
            ImGui::TextColored(ImVec4(1, .45f, .3f, 1), "Target object no longer exists.");
            m_applyTarget.Clear();
        } else ImGui::TextDisabled("Target: None");
        if (current && current->entity != m_applyTarget.Entity()) {
            ImGui::SameLine();
            const bool compatible = current->skeletalModel || !current->characterAssetPath.empty();
            ImGui::BeginDisabled(!compatible);
            if (ImGui::Button(("Use \"" + current->name + "\" As Target").c_str()))
                m_applyTarget.Set(current->entity);
            ImGui::EndDisabled();
            if (!compatible && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("The selected object is not character/skeletal-mesh compatible.");
        }
        if (target && current && current->entity != target->entity)
            ImGui::TextColored(ImVec4(1, .7f, .2f, 1),
                "Current selection differs from the stable apply target.");
    }
    ImGui::Separator();
    const float leftWidth = 175.0f, rightWidth = 330.0f;
    ImGui::BeginChild("CharacterComponents", ImVec2(leftWidth, 0), true);
    ImGui::TextDisabled("COMPONENTS");
    const char* components[] = {
        "Character", "Mesh", "Collision", "Movement + Camera",
        "Animation", "Gameplay", "AI", "Script"
    };
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
        if (m_selectedSocket >= static_cast<int>(m_asset.sockets.size())) {
            m_selectedSocket = -1;
            m_selectedAttachment = -1;
        }
        const bool socketSelected = m_selectedSocket >= 0
            && m_selectedSocket < static_cast<int>(m_previewSocketWorld.size());
        const glm::vec3 pivotWorld = socketSelected
            ? glm::vec3(m_previewSocketWorld[static_cast<std::size_t>(m_selectedSocket)][3])
            : glm::vec3(m_previewModelMatrix * glm::vec4(m_previewModelCenter, 1.0f));
        ImVec2 pivotPx;
        if (project(pivotWorld, pivotPx)) {
            glm::mat3 frameAxes(m_previewGizmoFrame);
            if (socketSelected) {
                const glm::mat4& axisFrame = m_gizmoMode == 0
                    ? m_previewSocketParentWorld[static_cast<std::size_t>(m_selectedSocket)]
                    : m_previewSocketWorld[static_cast<std::size_t>(m_selectedSocket)];
                frameAxes = glm::mat3(axisFrame);
                if (m_gizmoMode != 0) {
                    for (int axis = 0; axis < 3; ++axis) {
                        const float length = glm::length(frameAxes[axis]);
                        if (length > 1e-5f) frameAxes[axis] /= length;
                    }
                }
            }
            const ImU32 axisColors[3] = {
                IM_COL32(232, 68, 68, 255), IM_COL32(72, 210, 72, 255), IM_COL32(70, 120, 240, 255) };
            const float handleLen = 46.0f;
            ImDrawList* dl = ImGui::GetWindowDrawList();

            // Socket markers are always visible. Clicking a marker selects the socket;
            // attachments share their socket, so this is also a lightweight prop picker.
            int clickedSocket = -1;
            float closestMarker = 11.0f;
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            for (std::size_t i = 0; i < m_previewSocketWorld.size(); ++i) {
                ImVec2 marker;
                if (!project(glm::vec3(m_previewSocketWorld[i][3]), marker)) continue;
                const bool selected = static_cast<int>(i) == m_selectedSocket;
                const ImU32 markerColor = selected
                    ? IM_COL32(255, 190, 45, 255) : IM_COL32(75, 225, 235, 220);
                dl->AddCircle(marker, selected ? 7.0f : 5.0f, markerColor, 16,
                              selected ? 2.5f : 2.0f);
                dl->AddLine(ImVec2(marker.x - 4.0f, marker.y),
                            ImVec2(marker.x + 4.0f, marker.y), markerColor, 1.5f);
                dl->AddLine(ImVec2(marker.x, marker.y - 4.0f),
                            ImVec2(marker.x, marker.y + 4.0f), markerColor, 1.5f);
                if (!m_gizmoDragging && imageHovered
                    && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    const float dx = mouse.x - marker.x;
                    const float dy = mouse.y - marker.y;
                    const float distance = std::sqrt(dx * dx + dy * dy);
                    if (distance < closestMarker) {
                        closestMarker = distance;
                        clickedSocket = static_cast<int>(i);
                    }
                }
            }
            if (clickedSocket >= 0) {
                m_selectedSocket = clickedSocket;
                m_selectedAttachment = -1;
                for (std::size_t i = 0; i < m_asset.attachments.size(); ++i) {
                    if (m_asset.attachments[i].socketName
                        == m_asset.sockets[static_cast<std::size_t>(clickedSocket)].name) {
                        m_selectedAttachment = static_cast<int>(i);
                        break;
                    }
                }
                m_component = 1;
                gizmoConsumedMouse = true;
            }

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
            if (!socketSelected) {
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
            if (!gizmoConsumedMouse && !m_gizmoDragging && imageHovered
                && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
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
                if (socketSelected) {
                    CharacterSocket& socket =
                        m_asset.sockets[static_cast<std::size_t>(m_selectedSocket)];
                    if (m_gizmoMode == 0)
                        socket.position[a] += along / pixelsPerUnit[a];
                    else if (m_gizmoMode == 1)
                        socket.eulerDegrees[a] += along * 0.5f;
                    else
                        socket.scale[a] = std::max(
                            0.001f, socket.scale[a] + along * 0.01f);
                } else if (m_gizmoMode == 0) {
                    m_asset.modelOffsetPosition[a] += along / pixelsPerUnit[a];
                } else if (m_gizmoMode == 1) {
                    m_asset.modelOrientationEuler[a] += along * 0.5f;
                } else {
                    m_asset.modelOffsetScale[a] = std::max(
                        0.001f, m_asset.modelOffsetScale[a] + along * 0.01f);
                }
                m_dirty = true;
            }
        }
    }
    if (m_gizmoDragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_gizmoDragging = false; m_activeGizmoAxis = -1;
    }

    if (imageHovered && !gizmoConsumedMouse && !m_gizmoDragging) {
        ImGui::SetTooltip(
            "Click a socket marker to select it. Left-drag empty space to orbit. "
            "Wheel to zoom. Drag the coloured handles to transform.");
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
        ImGui::Text("%s  |  %zu bones",
                    std::filesystem::path(m_asset.modelAssetPath).filename().string().c_str(),
                    m_previewModel->BoneCount());
        ImGui::Text("%zu vertices  |  %zu triangles  |  %zu submeshes",
                    m_previewModel->VertexCount(),
                    m_previewModel->TriangleCount(),
                    m_previewModel->SubMeshCount());
        const bool previewHasStates =
            m_previewUsingGraph ? !m_previewGraphStates.empty() : !m_asset.animationStates.empty();
        if (previewHasStates) {
            ImGui::Text("Graph state: %s", m_previewController.CurrentStateName().c_str());
            if (m_previewUsingGraph) {
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)",
                    std::filesystem::path(m_asset.animationGraphPath).filename().string().c_str());
            }
        } else if (!m_asset.animationGraphPath.empty()) {
            ImGui::TextColored(ImVec4(1.0f, .72f, .25f, 1.0f),
                "Animation Graph set, but it has no states / clips did not load.");
        } else {
            ImGui::TextColored(ImVec4(1.0f, .72f, .25f, 1.0f),
                "Bind pose - reference an Animation Graph below.");
        }
    }
    if (m_selectedSocket >= 0
        && m_selectedSocket < static_cast<int>(m_asset.sockets.size())) {
        const CharacterSocket& socket =
            m_asset.sockets[static_cast<std::size_t>(m_selectedSocket)];
        ImGui::TextColored(ImVec4(1.0f, .72f, .20f, 1.0f),
                           "Selected socket: %s", socket.name.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Select Character")) {
            m_selectedSocket = -1;
            m_selectedAttachment = -1;
            m_gizmoDragging = false;
            m_activeGizmoAxis = -1;
        }
    } else {
        ImGui::TextDisabled("Selected: Character");
    }
    if (ImGui::Button(m_previewPlaying ? "Pause" : "Play")) m_previewPlaying = !m_previewPlaying;
    ImGui::SameLine();
    if (ImGui::Button("Reset View")) { m_previewYaw = 0.0f; m_previewPitch = 0.0f; m_previewZoom = 1.0f; m_previewTime = 0.0f; }
    ImGui::Checkbox("Show Collider", &m_showColliderGuide);
    // Live parameter scrubbers so the graph's transitions/blend spaces can be driven in the
    // editor (e.g. raise Speed to watch idle -> walk -> run) without entering Play.
    const std::vector<EditorScene::AnimationParameter>& driveParams =
        m_previewUsingGraph ? m_previewGraphParamDefs : m_asset.animationParameters;
    if (!driveParams.empty()) {
        ImGui::SeparatorText("Graph Parameters");
        for (const auto& parameter : driveParams) {
            float& value = m_previewGraphParameters[parameter.name];
            ImGui::PushID(parameter.name.c_str());
            if (parameter.type == EditorScene::AnimationParameter::Type::Bool) {
                bool on = value != 0.0f;
                if (ImGui::Checkbox(parameter.name.c_str(), &on)) value = on ? 1.0f : 0.0f;
            } else if (parameter.type == EditorScene::AnimationParameter::Type::Trigger) {
                if (ImGui::Button(("Trigger " + parameter.name).c_str()))
                    m_previewController.SetTriggerParameter(parameter.name);
            } else {
                ImGui::SetNextItemWidth(160);
                ImGui::DragFloat(parameter.name.c_str(), &value, 0.05f);
            }
            ImGui::PopID();
        }
    }
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
        const bool pickedModel = drawPicker(
            "Skeletal Model", "##ModelSearch", m_modelSearch,
            m_modelChoices, m_asset.modelAssetPath);
        changed |= pickedModel;
        if (pickedModel) m_asset.modelAssetId = {};
        const bool pickedMaterial = drawPicker(
            "Material", "##MaterialSearch", m_materialSearch,
            m_materialChoices, m_asset.materialAssetPath);
        changed |= pickedMaterial;
        if (pickedMaterial) m_asset.materialAssetId = {};
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

        // Sockets: named mount points (bone + offset). You tune the socket transform,
        // never the bone, so the skeleton is never disturbed.
        ImGui::SeparatorText("Sockets (mount points)");
        ImGui::TextDisabled("A socket = a bone + an offset. Move/rotate/scale the socket freely.");
        int removeSocket = -1;
        for (std::size_t i = 0; i < m_asset.sockets.size(); ++i) {
            auto& sock = m_asset.sockets[i];
            ImGui::PushID(12000 + static_cast<int>(i));
            const std::string socketLabel = "Socket: "
                + (sock.name.empty() ? std::string("(unnamed)") : sock.name);
            if (ImGui::Selectable(socketLabel.c_str(),
                                  m_selectedSocket == static_cast<int>(i))) {
                m_selectedSocket = static_cast<int>(i);
                m_selectedAttachment = -1;
                m_gizmoDragging = false;
                m_activeGizmoAxis = -1;
            }
            std::array<char, 96> nameBuf{}; Copy(nameBuf, sock.name);
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::InputText("Name", nameBuf.data(), nameBuf.size())) { sock.name = nameBuf.data(); changed = true; }
            const char* bonePreview = sock.boneName.empty() ? "(model origin)" : sock.boneName.c_str();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("Bone", bonePreview)) {
                if (ImGui::Selectable("(model origin)", sock.boneName.empty())) { sock.boneName.clear(); changed = true; }
                if (m_previewModel) {
                    for (std::size_t boneIndex = 0;
                         boneIndex < m_previewModel->GetSkeleton().bones.size(); ++boneIndex) {
                        const engine::Bone& b = m_previewModel->GetSkeleton().bones[boneIndex];
                        ImGui::PushID(static_cast<int>(boneIndex));
                        if (ImGui::Selectable(b.name.c_str(), sock.boneName == b.name)) {
                            sock.boneName = b.name; changed = true;
                        }
                        ImGui::PopID();
                    }
                } else {
                    ImGui::TextDisabled("Load a skeletal model to list its bones.");
                }
                ImGui::EndCombo();
            }
            changed |= ImGui::DragFloat3("Pos##sock", &sock.position.x, 0.01f, -100.0f, 100.0f);
            changed |= ImGui::DragFloat3("Rot##sock", &sock.eulerDegrees.x, 0.5f, -180.0f, 180.0f);
            changed |= ImGui::DragFloat3("Scale##sock", &sock.scale.x, 0.01f, 0.001f, 100.0f);
            if (ImGui::SmallButton("Remove Socket")) removeSocket = static_cast<int>(i);
            ImGui::Separator();
            ImGui::PopID();
        }
        if (removeSocket >= 0) {
            m_asset.sockets.erase(m_asset.sockets.begin() + removeSocket);
            if (m_selectedSocket == removeSocket) {
                m_selectedSocket = -1;
                m_selectedAttachment = -1;
            } else if (m_selectedSocket > removeSocket) {
                --m_selectedSocket;
            }
            changed = true;
        }
        if (ImGui::Button("Add Socket")) {
            CharacterSocket s;
            s.name = "Socket" + std::to_string(m_asset.sockets.size() + 1);
            m_asset.sockets.push_back(std::move(s));
            m_selectedSocket = static_cast<int>(m_asset.sockets.size() - 1);
            m_selectedAttachment = -1;
            changed = true;
        }

        ImGui::SeparatorText("Attachments (weapon / prop)");
        ImGui::TextDisabled("Mount a static model to a socket; it follows the animation.");
        int removeAttachment = -1;
        for (std::size_t i = 0; i < m_asset.attachments.size(); ++i) {
            auto& att = m_asset.attachments[i];
            ImGui::PushID(11000 + static_cast<int>(i));
            const std::string attachmentName = att.modelPath.empty()
                ? std::string("(choose model)")
                : std::filesystem::path(att.modelPath).filename().string();
            if (ImGui::Selectable(("Attachment: " + attachmentName).c_str(),
                                  m_selectedAttachment == static_cast<int>(i))) {
                m_selectedAttachment = static_cast<int>(i);
                m_selectedSocket = -1;
                for (std::size_t socketIndex = 0;
                     socketIndex < m_asset.sockets.size(); ++socketIndex) {
                    if (m_asset.sockets[socketIndex].name == att.socketName) {
                        m_selectedSocket = static_cast<int>(socketIndex);
                        break;
                    }
                }
                m_gizmoDragging = false;
                m_activeGizmoAxis = -1;
            }
            const bool pickedAttachment = drawPicker(
                "Model", "##AttachModelSearch", m_animSearch,
                m_staticMeshChoices, att.modelPath);
            changed |= pickedAttachment;
            if (pickedAttachment) att.modelAssetId = {};
            const bool pickedAttachmentMaterial = drawPicker(
                "Material", "##AttachMatSearch", m_materialSearch,
                m_materialChoices, att.materialPath);
            changed |= pickedAttachmentMaterial;
            if (pickedAttachmentMaterial) att.materialAssetId = {};
            const char* socketPreview = att.socketName.empty() ? "(choose a socket)" : att.socketName.c_str();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("Socket", socketPreview)) {
                if (m_asset.sockets.empty()) {
                    ImGui::TextDisabled("Add a socket above first.");
                }
                for (std::size_t socketIndex = 0;
                     socketIndex < m_asset.sockets.size(); ++socketIndex) {
                    const CharacterSocket& s = m_asset.sockets[socketIndex];
                    ImGui::PushID(static_cast<int>(socketIndex));
                    if (ImGui::Selectable(s.name.c_str(), att.socketName == s.name)) {
                        att.socketName = s.name;
                        if (m_selectedAttachment == static_cast<int>(i))
                            m_selectedSocket = static_cast<int>(socketIndex);
                        changed = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            if (ImGui::SmallButton("Remove")) removeAttachment = static_cast<int>(i);
            ImGui::Separator();
            ImGui::PopID();
        }
        if (removeAttachment >= 0) {
            m_asset.attachments.erase(m_asset.attachments.begin() + removeAttachment);
            if (m_selectedAttachment == removeAttachment) {
                m_selectedAttachment = -1;
            } else if (m_selectedAttachment > removeAttachment) {
                --m_selectedAttachment;
            }
            changed = true;
        }
        if (ImGui::Button("Add Attachment")) {
            CharacterAttachment a;
            if (!m_asset.sockets.empty()) a.socketName = m_asset.sockets.front().name;
            m_asset.attachments.push_back(std::move(a));
            m_selectedAttachment = static_cast<int>(m_asset.attachments.size() - 1);
            m_selectedSocket = m_asset.sockets.empty() ? -1 : 0;
            changed = true;
        }
        if (changed) m_dirty = true;
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
            // Keep the dimension controls in their own ID scope.  The character
            // panel can coexist with the scene inspector, which also exposes a
            // Radius field; explicit IDs prevent ImGui from routing a drag to the
            // wrong value.  Capsules expose total tip-to-tip Height because that
            // is the value artists expect to edit.  Internally halfHeight remains
            // the central segment half-length (the end caps are radius-sized).
            ImGui::PushID("CharacterColliderDimensions");
            if (collider.shape == engine::ecs::ColliderShape::Capsule) {
                float totalHeight = 2.0f * (std::max(collider.halfHeight, 0.0f)
                    + std::max(collider.radius, 0.001f));
                float radius = collider.radius;
                if (ImGui::DragFloat("Radius##radius", &radius,
                                     .01f, .001f, 1000.0f)) {
                    // Radius and height are independent authoring values. Keep
                    // the current total height and resize only the rounded caps.
                    collider.radius = std::clamp(
                        radius, 0.001f, std::max(totalHeight * 0.5f, 0.001f));
                    collider.halfHeight = std::max(
                        0.0f, totalHeight * 0.5f - collider.radius);
                    changed = true;
                }
                if (ImGui::DragFloat("Height##height", &totalHeight,
                                     .01f, 0.002f, 2000.0f)) {
                    totalHeight = std::max(totalHeight, 2.0f * collider.radius);
                    collider.halfHeight = std::max(
                        0.0f, totalHeight * 0.5f - collider.radius);
                    changed = true;
                }
                ImGui::TextDisabled("Total capsule height includes both rounded caps");
            } else {
                changed |= ImGui::DragFloat("Radius##radius", &collider.radius,
                                            .01f, .001f, 1000.0f);
                changed |= ImGui::DragFloat("Half Height##halfHeight",
                                            &collider.halfHeight, .01f, 0.0f, 1000.0f);
                collider.halfExtents = glm::vec3(collider.radius, collider.halfHeight, collider.radius);
            }
            ImGui::PopID();
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
        ImGui::SeparatorText("Movement");
        changed |= ImGui::DragFloat("Walk Speed", &v.walkSpeed,.05f,0.0f,100.0f);
        changed |= ImGui::DragFloat("Run Speed", &v.runSpeed,.05f,0.0f,100.0f);
        changed |= ImGui::DragFloat("Jump Speed", &v.jumpSpeed,.05f,0.0f,100.0f);
        if (ImGui::TreeNodeEx("Crouching", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= ImGui::DragFloat("Crouch Speed", &v.crouchSpeed,.05f,0.0f,100.0f);
            changed |= ImGui::DragFloat("Crouched Height", &v.crouchedHeight,.01f,
                                        v.capsuleRadius * 2.0f, v.capsuleHeight);
            ImGui::TextDisabled("Hold Ctrl or C. Standing is blocked under a low ceiling.");
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Swimming", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= ImGui::DragFloat("Swim Speed", &v.swimSpeed,.05f,0.0f,100.0f);
            changed |= ImGui::DragFloat("Vertical Swim Speed", &v.swimVerticalSpeed,.05f,0.0f,100.0f);
            ImGui::TextDisabled("WASD swims; Space rises; Ctrl or C descends.");
            ImGui::TreePop();
        }
        changed |= ImGui::DragFloat("Step Height", &v.stepHeight,.01f,0.0f,5.0f);
        changed |= ImGui::DragFloat("Max Slope", &v.maxSlopeDegrees,.5f,0.0f,89.0f);
        ImGui::SeparatorText("Camera");
        const char* cameraModes[] = { "Third Person", "First Person", "Isometric", "Platformer" };
        int cameraMode = std::clamp(v.cameraMode, 0, 3);
        if (v.firstPerson && cameraMode == 0) cameraMode = 1;
        if (ImGui::Combo("Camera Mode", &cameraMode, cameraModes, IM_ARRAYSIZE(cameraModes))) {
            v.cameraMode = cameraMode;
            v.firstPerson = cameraMode == 1;
            changed = true;
        }
        if (cameraMode == 3) {
            ImGui::SeparatorText("Platformer Camera");
            changed |= ImGui::DragFloat("Side Distance", &v.isometricDistance,
                                        0.1f, 0.0f, 500.0f);
            changed |= ImGui::SliderFloat("Camera Yaw", &v.platformerYaw,
                                          -180.0f, 180.0f, "%.1f deg");
            changed |= ImGui::DragFloat("Target Height", &v.cameraTargetHeight,
                                        0.01f, -100.0f, 100.0f);
            ImGui::TextDisabled("Side-on 2.5D: yaw picks the run axis (-90 = along X, "
                                "0 = along Z); the character faces its travel direction.");
        } else if (cameraMode == 2) {
            ImGui::SeparatorText("Isometric Camera");
            changed |= ImGui::DragFloat("Isometric Distance", &v.isometricDistance,
                                        0.1f, 0.0f, 500.0f);
            changed |= ImGui::SliderFloat("Isometric Yaw", &v.isometricYaw,
                                          -180.0f, 180.0f, "%.1f deg");
            changed |= ImGui::SliderFloat("Isometric Pitch", &v.isometricPitch,
                                          -89.0f, -5.0f, "%.1f deg");
            changed |= ImGui::DragFloat("Target Height", &v.cameraTargetHeight,
                                        0.01f, -100.0f, 100.0f);
            ImGui::TextDisabled("Fixed angle in Play; movement follows the camera axes.");
        } else if (cameraMode == 1) {
            changed |= ImGui::DragFloat("Eye Height", &v.eyeHeight,
                                        0.01f, 0.0f, 100.0f);
        } else {
            changed |= ImGui::DragFloat("Camera Distance", &v.cameraDistance,
                                        0.05f, 0.0f, 100.0f);
            changed |= ImGui::DragFloat("Target Height", &v.cameraTargetHeight,
                                        0.01f, -100.0f, 100.0f);
        }
        if (cameraMode != 1 && ImGui::TreeNodeEx(
                "Camera Collision", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= ImGui::Checkbox("Collision Enabled", &v.cameraCollision);
            ImGui::BeginDisabled(!v.cameraCollision);
            changed |= ImGui::DragFloat("Probe Radius", &v.cameraProbeRadius,
                                        0.01f, 0.0f, 5.0f);
            changed |= ImGui::DragFloat("Wall Padding", &v.cameraCollisionPadding,
                                        0.005f, 0.0f, 2.0f);
            changed |= ImGui::DragFloat("Return Speed", &v.cameraReturnSpeed,
                                        0.1f, 0.0f, 100.0f);
            ImGui::EndDisabled();
            ImGui::TreePop();
        }

        ImGui::SeparatorText("Rotation");
        const char* facingModes[] = { "Face Camera (strafe)", "Face Movement (free camera)" };
        int facing = std::clamp(v.facingMode, 0, 1);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("Orientation", &facing, facingModes, IM_ARRAYSIZE(facingModes))) {
            v.facingMode = facing; changed = true;
        }
        if (v.facingMode == 1) {
            changed |= ImGui::DragFloat("Turn Speed", &v.turnSpeed, 0.25f, 0.0f, 40.0f);
            ImGui::TextDisabled("Body turns toward its travel direction; the camera orbits freely.");
        } else {
            ImGui::TextDisabled("Body always faces the camera; rotating the camera turns the character.");
        }
    } else if (m_component == 4) {
        // Animation is authored entirely in a .3dggraph asset (Animation Graph Editor) and baked
        // onto placed characters on Apply. The character just references one.
        ImGui::SeparatorText("Animation Graph##AnimationGraphSection");
        ImGui::TextWrapped("Author animation (clips, states, transitions, blend spaces, movement) "
                           "in the Animation Graph Editor, save a .3dggraph, and reference it here.");
        const std::string graphPreview = m_asset.animationGraphPath.empty()
            ? std::string("None") : std::filesystem::path(m_asset.animationGraphPath).filename().string();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("Animation Graph", graphPreview.c_str())) {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##GraphSearch", "Search graphs...", m_graphSearch.data(), m_graphSearch.size());
            ImGui::Separator();
            if (ImGui::Selectable("None", m_asset.animationGraphPath.empty())) { m_asset.animationGraphPath.clear(); changed = true; }
            const std::string graphFilter = Lower(m_graphSearch.data());
            for (const AssetChoice& choice : m_graphChoices) {
                if (!graphFilter.empty() && Lower(choice.displayName).find(graphFilter) == std::string::npos) continue;
                ImGui::PushID(choice.path.c_str());
                if (ImGui::Selectable(choice.displayName.c_str(), m_asset.animationGraphPath == choice.path)) {
                    m_asset.animationGraphPath = choice.path;
                    m_asset.animationGraphAssetId = {};
                    changed = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", choice.path.c_str());
                ImGui::PopID();
            }
            if (m_graphChoices.empty()) ImGui::TextDisabled("No .3dggraph assets - make one in the Animation Graph Editor.");
            ImGui::EndCombo();
        }
        if (ImGui::Button("Refresh")) RefreshAssetChoices(assetRoot);
        if (!m_asset.animationGraphPath.empty()) {
            AnimationGraphAsset graphSummary;
            std::string graphSummaryError;
            if (graphSummary.Load(m_asset.animationGraphPath, &graphSummaryError)) {
                ImGui::TextDisabled("%zu clips, %zu states, %zu transitions.",
                    graphSummary.clips.size(), graphSummary.states.size(), graphSummary.transitions.size());
            } else {
                ImGui::TextColored(ImVec4(1.0f, .4f, .3f, 1.0f), "Graph failed to load.");
            }
        }

        ImGui::SeparatorText("IK Rig");
        const std::string ikRigPreview = m_asset.ikRigPath.empty()
            ? std::string("None")
            : std::filesystem::path(m_asset.ikRigPath).filename().string();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("Authored IK Rig", ikRigPreview.c_str())) {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##IKRigSearch", "Search IK rigs...",
                m_ikRigSearch.data(), m_ikRigSearch.size());
            ImGui::Separator();
            if (ImGui::Selectable("None", m_asset.ikRigPath.empty())) {
                m_asset.ikRigPath.clear();
                m_asset.ikRigAssetId = {};
                changed = true;
            }
            const std::string filter = Lower(m_ikRigSearch.data());
            for (const AssetChoice& choice : m_ikRigChoices) {
                if (!filter.empty()
                    && Lower(choice.displayName).find(filter) == std::string::npos
                    && Lower(choice.path).find(filter) == std::string::npos) {
                    continue;
                }
                ImGui::PushID(choice.path.c_str());
                if (ImGui::Selectable(choice.displayName.c_str(),
                                      m_asset.ikRigPath == choice.path)) {
                    engine::IKRigAssetData rig;
                    std::string rigError;
                    if (engine::LoadIKRigAsset(choice.path, &rig, &rigError)) {
                        m_asset.ikRigPath = choice.path;
                        m_asset.ikRigAssetId = rig.header.id;
                        changed = true;
                    } else if (message) {
                        *message = rigError;
                    }
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", choice.path.c_str());
                ImGui::PopID();
            }
            if (m_ikRigChoices.empty()) {
                ImGui::TextDisabled("No .3dgikrig assets. Create one in the IK Rig Editor.");
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Refresh IK Rigs")) RefreshAssetChoices(assetRoot);
        ImGui::TextDisabled("Foot, hand, look-at, aim and weapon goals are authored in the IK Rig Editor.");

        ImGui::SeparatorText("Legacy Foot IK Fallback");
        ImGui::BeginDisabled(!m_asset.ikRigPath.empty());
        changed |= ImGui::Checkbox("Enable Foot IK", &m_asset.footIK.enabled);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Grounds the feet on the surface at runtime (raycasts the scene). "
                              "Leg + pelvis bones auto-detect from the rig's bone names.");
        }
        if (m_asset.footIK.enabled) {
            changed |= ImGui::SliderFloat("IK Weight", &m_asset.footIK.weight, 0.0f, 1.0f);
            changed |= ImGui::DragFloat("Trace Up", &m_asset.footIK.traceUp, 0.01f, 0.0f, 3.0f, "%.2f m");
            changed |= ImGui::DragFloat("Trace Down", &m_asset.footIK.traceDown, 0.01f, 0.0f, 3.0f, "%.2f m");
            changed |= ImGui::DragFloat("Foot Height", &m_asset.footIK.footHeight, 0.005f, 0.0f, 0.5f, "%.3f m");
            changed |= ImGui::SliderFloat("Pelvis Adjust", &m_asset.footIK.pelvisWeight, 0.0f, 1.0f);
            changed |= ImGui::DragFloat("Max Pelvis Drop", &m_asset.footIK.maxPelvisDrop, 0.01f, 0.0f, 2.0f, "%.2f m");
            ImGui::TextDisabled("Preview shows this in Play or with the global View toggle.");
        }
        ImGui::EndDisabled();
        if (!m_asset.ikRigPath.empty()) {
            ImGui::TextDisabled("The assigned IK rig owns foot placement; the legacy fallback is ignored.");
        }

        ImGui::SeparatorText("Standalone Action Clips");
        ImGui::TextWrapped(
            "Attach one-shot clips created in the Clip Editor. They remain outside "
            "the locomotion graph and are called by their Action Name from scripts.");
        if (ImGui::BeginCombo("Add Action Clip", "Choose saved action...")) {
            const std::string clipFilter = Lower(m_clipSearch.data());
            ImGui::InputTextWithHint(
                "##ActionClipSearch", "Search action clips...",
                m_clipSearch.data(), m_clipSearch.size());
            ImGui::Separator();
            for (const AssetChoice& choice : m_clipChoices) {
                if (!clipFilter.empty()
                    && Lower(choice.displayName).find(clipFilter) == std::string::npos) {
                    continue;
                }
                AnimationClipAsset candidate;
                if (!candidate.Load(choice.path, nullptr) || !candidate.action) continue;
                const bool attached = std::find(
                    m_asset.actionClipAssets.begin(),
                    m_asset.actionClipAssets.end(), choice.path)
                    != m_asset.actionClipAssets.end();
                ImGui::PushID(choice.path.c_str());
                if (ImGui::Selectable(choice.displayName.c_str(), attached)) {
                    if (!attached) {
                        m_asset.actionClipAssets.push_back(choice.path);
                        changed = true;
                    }
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Action: %s\n%s",
                        candidate.name.c_str(), choice.path.c_str());
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("Refresh Action Clips")) {
            RefreshAssetChoices(assetRoot);
            if (message) {
                *message = "Refreshed standalone Action Clips.";
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Rescan Content for newly saved .3dgclip Action Clip assets.");
        }
        int removeAction = -1;
        for (std::size_t i = 0; i < m_asset.actionClipAssets.size(); ++i) {
            ImGui::PushID(4100 + static_cast<int>(i));
            AnimationClipAsset action;
            const std::string& actionPath = m_asset.actionClipAssets[i];
            if (action.Load(actionPath, nullptr) && action.action) {
                ImGui::Text("%s  [%s]", action.name.c_str(),
                    action.maskRootBone.empty() ? "Full Body" : action.maskRootBone.c_str());
                ImGui::TextDisabled("Fade %.2f / %.2f  Speed %.2f",
                    action.fadeIn, action.fadeOut, action.speed);
            } else {
                ImGui::TextColored(ImVec4(1.0f, .4f, .3f, 1.0f),
                    "%s (missing or not an Action Clip)",
                    std::filesystem::path(actionPath).filename().string().c_str());
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove")) removeAction = static_cast<int>(i);
            ImGui::PopID();
        }
        if (removeAction >= 0) {
            m_asset.actionClipAssets.erase(
                m_asset.actionClipAssets.begin() + removeAction);
            changed = true;
        }
        if (m_asset.actionClipAssets.empty()) {
            ImGui::TextDisabled("No standalone actions attached.");
        }
    } else if (false) {   // legacy inline animation authoring (superseded by graph assets)
        ImGui::SeparatorText("Animation Sources (separate files)");
        ImGui::TextDisabled("Merge clips from external FBX files onto this model by bone name.");
        ImGui::TextDisabled("The animation file must share the model's skeleton (e.g. Mixamo rigs).");
        // Quick-add from a saved clip asset (authored in the Clip Editor): one pick fills
        // file + clip name + strip-root-motion, so you don't re-enter them each time.
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##AddFromClip", "Add from Clip Asset...")) {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##ClipSearch", "Search clips...", m_clipSearch.data(), m_clipSearch.size());
            ImGui::Separator();
            const std::string clipFilter = Lower(m_clipSearch.data());
            for (const AssetChoice& choice : m_clipChoices) {
                if (!clipFilter.empty() && Lower(choice.displayName).find(clipFilter) == std::string::npos) continue;
                if (ImGui::Selectable(choice.displayName.c_str())) {
                    AnimationClipAsset clip;
                    std::string clipError;
                    if (clip.Load(choice.path, &clipError)) {
                        CharacterAnimationSource s;
                        s.file = clip.sourceFile;
                        s.assetId = clip.sourceAssetId;
                        s.clipName = clip.clipName;
                        s.stripRootMotion = clip.stripRootMotion;
                        m_asset.animationSources.push_back(std::move(s));
                        changed = true;
                    }
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", choice.path.c_str());
            }
            if (m_clipChoices.empty()) ImGui::TextDisabled("No .3dgclip assets yet - make them in the Clip Editor.");
            ImGui::EndCombo();
        }
        int removeSource = -1;
        for (std::size_t i = 0; i < m_asset.animationSources.size(); ++i) {
            auto& source = m_asset.animationSources[i];
            ImGui::PushID(9000 + static_cast<int>(i));
            const bool pickedAnimation = drawPicker(
                "File", "##AnimFileSearch", m_animSearch,
                m_modelChoices, source.file);
            changed |= pickedAnimation;
            if (pickedAnimation) source.assetId = {};
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
                        for (std::size_t ci=0;ci<clips->size();++ci) {
                            ImGui::PushID(static_cast<int>(ci));
                            if (ImGui::Selectable((*clips)[ci].name.c_str(),state.clipIndex==static_cast<int>(ci))) {
                                state.clipIndex=static_cast<int>(ci); state.clipName=(*clips)[ci].name; changed=true;
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndCombo();
                    }
                } else ImGui::TextDisabled("Load a skeletal model to choose clips.");
                changed |= ImGui::Checkbox("Loop",&state.loop);
                changed |= ImGui::DragFloat("Speed Multiplier",&state.speed,.01f,0.0f,5.0f);
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
                        for (std::size_t parameterIndex = 0;
                             parameterIndex < m_asset.animationParameters.size(); ++parameterIndex) {
                            const auto& parameter = m_asset.animationParameters[parameterIndex];
                            if (parameter.type != EditorScene::AnimationParameter::Type::Float) continue;
                            ImGui::PushID(static_cast<int>(parameterIndex));
                            if (ImGui::Selectable(parameter.name.c_str(), state.blendParameter == parameter.name)) {
                                state.blendParameter = parameter.name; changed = true;
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndCombo();
                    }
                    if (state.blendSpace2D) {
                        if (state.blendParameterY.empty()) state.blendParameterY = "Direction";
                        if (ImGui::BeginCombo("Direction Parameter", state.blendParameterY.c_str())) {
                            for (std::size_t parameterIndex = 0;
                                 parameterIndex < m_asset.animationParameters.size(); ++parameterIndex) {
                                const auto& parameter = m_asset.animationParameters[parameterIndex];
                                if (parameter.type != EditorScene::AnimationParameter::Type::Float) continue;
                                ImGui::PushID(static_cast<int>(parameterIndex));
                                if (ImGui::Selectable(parameter.name.c_str(), state.blendParameterY == parameter.name)) {
                                    state.blendParameterY = parameter.name; changed = true;
                                }
                                ImGui::PopID();
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
                                    ImGui::PushID(static_cast<int>(clipIndex));
                                    if (ImGui::Selectable((*clips)[clipIndex].name.c_str(),
                                            sample.clipIndex == static_cast<int>(clipIndex))) {
                                        sample.clipIndex = static_cast<int>(clipIndex);
                                        sample.clipName = (*clips)[clipIndex].name;
                                        changed = true;
                                    }
                                    ImGui::PopID();
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
            ensureBool("IsCrouching", false); ensureBool("IsSwimming", false);
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
        const char* compares[]={">=","<","==","!=","<=",">"};
        for(std::size_t i=0;i<m_asset.animationTransitions.size();++i){
            auto& transition=m_asset.animationTransitions[i]; ImGui::PushID(3000+static_cast<int>(i));
            auto stateCombo=[&](const char* label,std::string& value,bool any){
                if(ImGui::BeginCombo(label,value.empty()?(any?"Any State":"None"):value.c_str())){
                    if(any&&ImGui::Selectable("Any State",value.empty())){value.clear();changed=true;}
                    for(std::size_t stateIndex=0;stateIndex<m_asset.animationStates.size();++stateIndex){const auto& s=m_asset.animationStates[stateIndex];ImGui::PushID(static_cast<int>(stateIndex));if(ImGui::Selectable(s.name.c_str(),value==s.name)){value=s.name;changed=true;}ImGui::PopID();}
                    ImGui::EndCombo();
                }
            };
            stateCombo("From",transition.fromState,true); stateCombo("To",transition.toState,false);
            if(ImGui::BeginCombo("Parameter",transition.parameter.empty()?"None":transition.parameter.c_str())){
                for(std::size_t parameterIndex=0;parameterIndex<m_asset.animationParameters.size();++parameterIndex){const auto& p=m_asset.animationParameters[parameterIndex];ImGui::PushID(static_cast<int>(parameterIndex));if(ImGui::Selectable(p.name.c_str(),transition.parameter==p.name)){transition.parameter=p.name;changed=true;}ImGui::PopID();}
                ImGui::EndCombo();
            }
            // Determine the selected parameter's type so a bool/trigger gets a proper
            // "is true / is false" condition (a >= 0 default always passes and never
            // actually compares a boolean).
            EditorScene::AnimationParameter::Type paramType = EditorScene::AnimationParameter::Type::Float;
            for(const auto& p:m_asset.animationParameters)
                if(p.name==transition.parameter){paramType=p.type;break;}
            using Comp = EditorScene::AnimationStateTransition::Compare;
            if(paramType==EditorScene::AnimationParameter::Type::Bool
               || paramType==EditorScene::AnimationParameter::Type::Trigger){
                const char* boolConds[]={"is false","is true (or triggered)"};
                int sel=(transition.threshold>=0.5f)?1:0;
                ImGui::Combo("Condition",&sel,boolConds,2);
                const float wantThreshold=sel?1.0f:0.0f;
                // Normalise to an equality test so the boolean is actually compared;
                // also fixes legacy transitions saved with the >= 0 default.
                if(transition.compare!=Comp::Equal||transition.threshold!=wantThreshold){
                    transition.compare=Comp::Equal;
                    transition.threshold=wantThreshold;
                    changed=true;
                }
            } else {
                int compare=std::clamp(static_cast<int>(transition.compare),0,5);
                if(ImGui::Combo("Compare",&compare,compares,6)){transition.compare=static_cast<Comp>(compare);changed=true;}
                changed|=ImGui::DragFloat("Threshold",&transition.threshold,.05f);
            }
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
            if(!m_asset.animationParameters.empty()){
                const auto& param=m_asset.animationParameters.front(); t.parameter=param.name;
                using PT=EditorScene::AnimationParameter::Type;
                using TC=EditorScene::AnimationStateTransition::Compare;
                if(param.type==PT::Bool||param.type==PT::Trigger){t.compare=TC::Equal;t.threshold=1.0f;}
                else{t.compare=TC::Greater;t.threshold=0.1f;}
            }
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
        ImGui::SeparatorText("Death Physics");
        changed |= ImGui::Checkbox("Ragdoll On Death", &m_asset.ragdollEnabled);
        if (m_asset.ragdollEnabled) {
            changed |= ImGui::DragFloat(
                "Ragdoll Mass", &m_asset.ragdoll.totalMass, 1.0f, 1.0f, 500.0f, "%.1f kg");
            changed |= ImGui::SliderInt(
                "Physics Bodies", &m_asset.ragdoll.maxBodies, 4, 32);
            changed |= ImGui::SliderFloat(
                "Body Thickness", &m_asset.ragdoll.bodyRadiusScale, 0.04f, 0.4f);
            changed |= ImGui::DragFloat(
                "Death Impulse", &m_asset.ragdoll.deathImpulse, 0.05f, 0.0f, 20.0f);
            changed |= ImGui::DragFloat(
                "Linear Damping", &m_asset.ragdoll.linearDamping, 0.02f, 0.0f, 10.0f);
            changed |= ImGui::DragFloat(
                "Angular Damping", &m_asset.ragdoll.angularDamping, 0.02f, 0.0f, 20.0f);
            ImGui::TextDisabled("Activates automatically when Health reaches zero.");
        }
    } else if (m_component == 6) {
        changed |= ImGui::Checkbox("AI Agent", &m_asset.navAgentEnabled);
        if (!m_asset.navAgentEnabled) ImGui::BeginDisabled();
        changed |= ImGui::DragFloat("Speed",&m_asset.navSpeed,.05f,0,100);
        changed |= ImGui::DragFloat("Max Force",&m_asset.navMaxForce,.1f,0,200);
        changed |= ImGui::DragFloat("Reach Radius",&m_asset.navReachRadius,.01f,.01f,20);
        changed |= ImGui::DragFloat("Repath (s)",&m_asset.navRepathInterval,.01f,.01f,5);
        changed |= ImGui::DragFloat("Vision Range",&m_asset.navVisionRange,.1f,0,1000);
        changed |= ImGui::DragFloat(
            "Vision Half-Angle", &m_asset.navVisionHalfAngle, .5f, 1, 180, "%.0f deg");
        changed |= ImGui::DragFloat("Hearing Range",&m_asset.navHearingRange,.1f,0,1000);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Omnidirectional radius for hearing noises (footsteps, combat). 0 = deaf.");

        ImGui::SeparatorText("Behavior");
        changed |= drawPicker(
            "Behavior Tree", "##CharacterBehaviorSearch",
            m_behaviorSearch, m_behaviorChoices, m_asset.behaviorTreeAsset);
        if (ImGui::SmallButton("Refresh Trees")) {
            RefreshAssetChoices(assetRoot);
        }
        if (m_behaviorChoices.empty()) {
            ImGui::TextDisabled("No saved .btgraph assets found under Content.");
        } else {
            ImGui::TextDisabled("Searches all saved Behavior Trees in Content.");
        }

        ImGui::SeparatorText("Target and Team");
        const char* targetPreview = m_asset.navTargetName.empty()
            ? "None (patrol / auto-target)" : m_asset.navTargetName.c_str();
        if (ImGui::BeginCombo("Chase Target", targetPreview)) {
            if (ImGui::Selectable(
                    "None (patrol / auto-target)", m_asset.navTargetName.empty())) {
                m_asset.navTargetName.clear();
                changed = true;
            }
            for (std::size_t objectIndex = 0; objectIndex < scene.Objects().size(); ++objectIndex) {
                const EditorScene::Object& object = scene.Objects()[objectIndex];
                const bool selected = object.name == m_asset.navTargetName;
                ImGui::PushID(static_cast<int>(objectIndex));
                if (ImGui::Selectable(object.name.c_str(), selected)) {
                    m_asset.navTargetName = object.name;
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        changed |= ImGui::DragInt(
            "Team ID (0 = neutral)", &m_asset.navTeam, .1f, 0, 32);
        if (ImGui::SmallButton("Neutral##CharacterTeam")) {
            m_asset.navTeam = 0;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Player##CharacterTeam")) {
            m_asset.navTeam = 1;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Enemy##CharacterTeam")) {
            m_asset.navTeam = 2;
            changed = true;
        }
        changed |= ImGui::Checkbox(
            "Auto-target nearest hostile", &m_asset.navAutoTarget);
        if (m_asset.navAutoTarget) {
            ImGui::TextDisabled(
                "Targets the nearest living agent on a different non-zero team.");
        } else if (m_asset.navTeam == 0) {
            ImGui::TextDisabled(
                "Neutral agents are not included in faction auto-targeting.");
        }
        if (!m_asset.navAgentEnabled) ImGui::EndDisabled();
    } else {
        ImGui::TextDisabled(
            "Scripts run from top to bottom. Each has an independent enabled state.");
        for (std::size_t index = 0; index < m_asset.scripts.size(); ++index) {
            CharacterScript& script = m_asset.scripts[index];
            ImGui::PushID(static_cast<int>(index));
            changed |= ImGui::Checkbox("##Enabled", &script.enabled);
            ImGui::SameLine();

            const auto selectedScript = std::find_if(
                m_scriptChoices.begin(), m_scriptChoices.end(),
                [&script](const AssetChoice& choice) {
                    return choice.path == script.path
                        || choice.displayName == script.className;
                });
            const char* preview = selectedScript != m_scriptChoices.end()
                ? selectedScript->displayName.c_str()
                : (script.className.empty() ? "Choose saved script..."
                                            : script.className.c_str());
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::BeginCombo("##Script", preview)) {
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint(
                    "##Search", "Search saved scripts...",
                    m_scriptSearch.data(), m_scriptSearch.size());
                ImGui::Separator();
                const std::string filter = Lower(m_scriptSearch.data());
                for (const AssetChoice& choice : m_scriptChoices) {
                    if (!filter.empty()
                        && Lower(choice.displayName).find(filter) == std::string::npos) {
                        continue;
                    }
                    const bool selected = choice.path == script.path;
                    ImGui::PushID(choice.path.c_str());
                    if (ImGui::Selectable(choice.displayName.c_str(), selected)) {
                        script.className = choice.displayName;
                        script.path = choice.path;
                        script.enabled = true;
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", choice.path.c_str());
                    }
                    ImGui::PopID();
                }
                if (m_scriptChoices.empty()) {
                    ImGui::TextDisabled("No scripts found in Content/Scripts");
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (index > 0 && ImGui::SmallButton("Up")) {
                std::swap(m_asset.scripts[index], m_asset.scripts[index - 1]);
                changed = true;
            }
            if (index > 0) ImGui::SameLine();
            const bool remove = ImGui::SmallButton("Remove");
            if (!script.path.empty() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", script.path.c_str());
            }
            if (ImGui::TreeNode("Scheduling")) {
                changed |= ImGui::DragInt(
                    "Execution Order", &script.executionOrder, 1.0f, -10000, 10000);
                const std::string dependencyPreview = script.dependencies.empty()
                    ? "None" : std::to_string(script.dependencies.size()) + " selected";
                if (ImGui::BeginCombo("Required Scripts", dependencyPreview.c_str())) {
                    for (const AssetChoice& choice : m_scriptChoices) {
                        if (choice.displayName == script.className) continue;
                        const auto found = std::find(script.dependencies.begin(),
                            script.dependencies.end(), choice.displayName);
                        const bool required = found != script.dependencies.end();
                        ImGui::PushID(choice.path.c_str());
                        if (ImGui::Selectable(choice.displayName.c_str(), required,
                                              ImGuiSelectableFlags_DontClosePopups)) {
                            if (required) script.dependencies.erase(found);
                            else script.dependencies.push_back(choice.displayName);
                            changed = true;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                ImGui::TextDisabled(
                    "Required scripts initialize and update before this script.");
                ImGui::TreePop();
            }
            ImGui::PopID();
            if (remove) {
                m_asset.scripts.erase(m_asset.scripts.begin()
                    + static_cast<std::ptrdiff_t>(index));
                changed = true;
                break;
            }
        }

        if (ImGui::Button("+ Add Script")) {
            CharacterScript script;
            if (!m_scriptChoices.empty()) {
                script.className = m_scriptChoices.front().displayName;
                script.path = m_scriptChoices.front().path;
            }
            m_asset.scripts.push_back(std::move(script));
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh Scripts")) {
            RefreshAssetChoices(assetRoot);
            if (message) *message = "Refreshed saved gameplay scripts.";
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Rescan Content/Scripts for saved C++ and Lua scripts.");
        }

        if (ImGui::Button("Apply Scripts to Selected")) {
            if (!scene.SelectedObject()) {
                if (message) *message = "Select a scene character first.";
            } else {
                if (m_asset.scripts.empty()) {
                    scene.SetSelectedScript({}, {}, false);
                    scene.SetSelectedAdditionalScripts({});
                } else {
                    const CharacterScript& primary = m_asset.scripts.front();
                    scene.SetSelectedScript(
                        primary.className, primary.path, primary.enabled);
                    scene.SetSelectedScriptScheduling(
                        primary.executionOrder, primary.dependencies);
                    std::vector<EditorScene::ScriptBinding> additional;
                    for (std::size_t i = 1; i < m_asset.scripts.size(); ++i) {
                        const CharacterScript& script = m_asset.scripts[i];
                        additional.push_back({script.enabled, script.className,
                            script.path, {}, script.executionOrder,
                            script.dependencies});
                    }
                    scene.SetSelectedAdditionalScripts(additional);
                }
                if (message) {
                    *message = "Applied " + std::to_string(m_asset.scripts.size())
                        + " script(s) to the selected character.";
                }
            }
        }

        if (!m_asset.scripts.empty()) {
            m_asset.scriptEnabled = m_asset.scripts.front().enabled;
            m_asset.scriptClassName = m_asset.scripts.front().className;
            m_asset.scriptPath = m_asset.scripts.front().path;
        } else {
            m_asset.scriptEnabled = false;
            m_asset.scriptClassName.clear();
            m_asset.scriptPath.clear();
        }
    }
    if (changed) {
        m_dirty = true;
        if (m_component == 2) m_colliderGuideDirty = true;
        // Live-sync: if the character placed in the scene (linked by asset path) is
        // selected, re-stamp the edits onto it so sockets / transforms / materials update
        // in real time without re-dropping. Undo is suppressed so a drag isn't logged
        // as hundreds of steps.
        const EditorScene::Object* selected = scene.SelectedObject();
        if (!m_path.empty() && selected && selected->characterAssetPath == m_path) {
            // If the character was auto-stood-up on drop (the asset itself carries no
            // model offset), keep that orientation across the re-apply so it doesn't flip.
            const glm::vec3 scaleDelta = m_asset.modelOffsetScale - glm::vec3(1.0f);
            const bool assetHasOffset =
                glm::dot(m_asset.modelOffsetPosition, m_asset.modelOffsetPosition) > 1e-8f
                || glm::dot(m_asset.modelOrientationEuler, m_asset.modelOrientationEuler) > 1e-4f
                || glm::dot(scaleDelta, scaleDelta) > 1e-8f;
            const glm::vec3 keepPos = selected->modelOffsetPosition;
            const glm::vec3 keepEuler = selected->modelOrientationEuler;
            const glm::vec3 keepScale = selected->modelOffsetScale;
            scene.SuppressUndo(true);
            m_asset.Apply(scene);
            if (!assetHasOffset) {
                scene.SetSelectedModelOffset(keepPos, keepEuler, keepScale);
            }
            scene.SuppressUndo(false);
        }
    }
    ImGui::Separator();
    ImGui::TextColored(m_dirty ? ImVec4(1.0f,.75f,.2f,1.0f) : ImVec4(.45f,.85f,.55f,1.0f), m_dirty ? "Unsaved changes" : "Asset saved");
    ImGui::EndChild();
    ImGui::End();
}
