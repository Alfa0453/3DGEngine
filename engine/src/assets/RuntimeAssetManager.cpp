#include "engine/assets/RuntimeAssetManager.h"

#include "engine/animation/AnimatedModel.h"
#include "engine/animation/AnimationGraphDesc.h"
#include "engine/ecs/Components.h"
#include "engine/physics/PhysicsComponents.h"
#include "engine/assets/ShaderGraphCompiler.h"
#include "engine/assets/TextureAsset.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <utility>

namespace engine {
namespace {

void SetError(std::string* out, const std::string& message) {
    if (out) {
        *out = message;
    }
}

std::string CacheKey(const std::string& path) {
    std::filesystem::path normalized(path);
    normalized = normalized.lexically_normal();
    return normalized.generic_string();
}

} // namespace

const Model* RuntimeAssetManager::LoadModel(const std::string &path, std::string *error)
{
    if (path.empty()) {
        SetError(error, "RuntimeAssetManager: model path is empty");
        return nullptr;
    }

    const std::string key = CacheKey(path);
    const auto existing = m_models.find(key);
    if (existing != m_models.end()) {
        SetError(error, std::string{});
        return existing->second.get();
    }

    try {
        auto model = std::make_unique<Model>(Model::FromFile(path));
        const Model* result = model.get();
        m_models.emplace(key, std::move(model));
        SetError(error, std::string{});
        return result;
    } catch (const std::exception& ex) {
        SetError(error, ex.what());
        return nullptr;
    }
}

const Model* RuntimeAssetManager::ReloadModel(
    const std::string& path, std::string* error) {
    if (path.empty()) {
        SetError(error, "RuntimeAssetManager: model path is empty");
        return nullptr;
    }
    const std::string key = CacheKey(path);
    const auto existing = m_models.find(key);
    if (existing == m_models.end()) return LoadModel(path, error);
    try {
        Model replacement = Model::FromFile(path);
        *existing->second = std::move(replacement);
        SetError(error, {});
        return existing->second.get();
    } catch (const std::exception& ex) {
        SetError(error, ex.what());
        return nullptr;
    }
}

const SkinnedModel* RuntimeAssetManager::LoadSkinnedModel(const std::string& path, std::string* error) {
    if (path.empty()) {
        SetError(error, "RuntimeAssetManager: skinned model path is empty");
        return nullptr;
    }

    const std::string key = CacheKey(path);
    const auto existing = m_skinnedModels.find(key);
    if (existing != m_skinnedModels.end()) {
        SetError(error, std::string{});
        return existing->second.get();
    }

    try {
        auto model = std::make_unique<SkinnedModel>(SkinnedModel::FromFile(path));
        const SkinnedModel* result = model.get();
        m_skinnedModels.emplace(key, std::move(model));
        SetError(error, std::string{});
        return result;
    } catch (const std::exception& ex) {
        SetError(error, ex.what());
        return nullptr;
    }
}

const SkinnedModel* RuntimeAssetManager::ReloadSkinnedModel(
    const std::string& path, std::string* error) {
    if (path.empty()) {
        SetError(error, "RuntimeAssetManager: skinned model path is empty");
        return nullptr;
    }
    const auto existing = m_skinnedModels.find(CacheKey(path));
    if (existing == m_skinnedModels.end()) return LoadSkinnedModel(path, error);
    try {
        SkinnedModel replacement = SkinnedModel::FromFile(path);
        *existing->second = std::move(replacement);
        SetError(error, {});
        return existing->second.get();
    } catch (const std::exception& ex) {
        SetError(error, ex.what());
        return nullptr;
    }
}

const SkinnedModel* RuntimeAssetManager::LoadSkinnedModel(
    const std::string& path, const std::vector<SkinnedAnimationSource>& extraAnimations,
    std::string* error) {
    if (extraAnimations.empty()) {
        return LoadSkinnedModel(path, error);
    }
    if (path.empty()) {
        SetError(error, "RuntimeAssetManager: skinned model path is empty");
        return nullptr;
    }

    // Cache key = model path + each source (name/strip/path), so a model with a
    // given set of merged clips is loaded once and shared.
    std::string key = CacheKey(path);
    for (const SkinnedAnimationSource& source : extraAnimations) {
        key += '\n';
        key += source.name;
        key += source.stripRootMotion ? "|1|" : "|0|";
        key += source.sourceName;
        key += '|';
        key += CacheKey(source.path);
    }

    const auto existing = m_skinnedModels.find(key);
    if (existing != m_skinnedModels.end()) {
        SetError(error, std::string{});
        return existing->second.get();
    }

    try {
        auto model = std::make_unique<SkinnedModel>(SkinnedModel::FromFile(path));
        std::string mergeError;
        for (const SkinnedAnimationSource& source : extraAnimations) {
            if (source.path.empty()) continue;
            try {
                model->AddAnimationsFromFile(
                    source.path, source.stripRootMotion, source.name, source.sourceName);
            } catch (const std::exception& ex) {
                // Keep whatever clips loaded; report the first failure but don't abort.
                if (mergeError.empty()) mergeError = ex.what();
            }
        }
        const SkinnedModel* result = model.get();
        m_skinnedModels.emplace(key, std::move(model));
        SetError(error, mergeError);
        return result;
    } catch (const std::exception& ex) {
        SetError(error, ex.what());
        return nullptr;
    }
}

const Texture* RuntimeAssetManager::LoadTexture(const std::string &path, std::string *error)
{
    if (path.empty()) {
        SetError(error, "RuntimeAssetManager: texture path is empty");
        return nullptr;
    }

    const std::string key = CacheKey(path);
    const auto existing = m_textures.find(key);
    if (existing != m_textures.end()) {
        SetError(error, std::string{});
        return existing->second.get();
    }

    try {
        std::unique_ptr<Texture> texture;
        if (std::filesystem::path(path).extension() == ".3dgtex") {
            TextureAssetData asset;
            std::string loadError;
            if (!LoadTextureAsset(path, &asset, &loadError)) {
                SetError(error, loadError);
                return nullptr;
            }
            texture = std::make_unique<Texture>(asset);
        } else {
            texture = std::make_unique<Texture>(path);
        }
        const Texture* result = texture.get();
        m_textures.emplace(key, std::move(texture));
        SetError(error, std::string{});
        return result;
    } catch (const std::exception& ex) {
        SetError(error, ex.what());
        return nullptr;
    }
}

const RuntimeMaterialAsset* RuntimeAssetManager::LoadMaterial(const std::string& path, std::string* error)
{
    if (path.empty()) {
        SetError(error, "RuntimeAssetManager: material path is empty");
        return nullptr;
    }

    const std::string key = CacheKey(path);
    const auto existing = m_materials.find(key);
    if (existing != m_materials.end()) {
        SetError(error, std::string{});
        return existing->second.get();
    }

    auto material = std::make_unique<RuntimeMaterialAsset>();
    if (!LoadMaterialAssetFile(path, material.get(), error)) {
        return nullptr;
    }

    const RuntimeMaterialAsset* result = material.get();
    m_materials.emplace(key, std::move(material));
    SetError(error, std::string{});
    return result;
}

const FoliageAssetData* RuntimeAssetManager::LoadFoliage(
    const std::string& path, std::string* error) {
    if (path.empty()) {
        SetError(error, "RuntimeAssetManager: foliage path is empty");
        return nullptr;
    }
    const std::string key = CacheKey(path);
    const auto existing = m_foliage.find(key);
    if (existing != m_foliage.end()) {
        SetError(error, {});
        return existing->second.get();
    }
    auto foliage = std::make_unique<FoliageAssetData>();
    if (!LoadFoliageAsset(path, foliage.get(), error)) return nullptr;
    const FoliageAssetData* result = foliage.get();
    m_foliage.emplace(key, std::move(foliage));
    SetError(error, {});
    return result;
}

const FoliageAssetData* RuntimeAssetManager::ReloadFoliage(
    const std::string& path, std::string* error) {
    m_foliage.erase(CacheKey(path));
    return LoadFoliage(path, error);
}

const Shader* RuntimeAssetManager::LoadShader(
    const std::string& path, bool skinned, std::string* error)
{
    if (path.empty()) {
        SetError(error, "RuntimeAssetManager: shader path is empty");
        return nullptr;
    }
    const std::string key = CacheKey(path);
    auto asset = m_shaderAssets.find(key);
    if (asset == m_shaderAssets.end()) {
        ShaderAsset loaded;
        std::string loadError;
        if (!LoadShaderAsset(path, &loaded, &loadError)) {
            SetError(error, loadError);
            return nullptr;
        }
        asset = m_shaderAssets.emplace(key, std::move(loaded)).first;
    }
    const std::string variant =
        asset->second.domain == ShaderDomain::Surface
            ? (skinned ? "surface_skinned" : "surface_static")
            : asset->second.domain == ShaderDomain::PostProcess
                ? "post_process"
                : asset->second.domain == ShaderDomain::Particle
                    ? "particle"
                    : "unlit";
    // LoadShader is called from the render submission path for every object
    // using a graph material. Once a variant has compiled, returning it here
    // avoids regenerating graph source, validating nodes, and hashing the full
    // program every frame. Explicit asset reload/manager Clear still invalidates
    // this cache in the normal editor refresh workflow.
    if (const Shader* cached = m_shaderPrograms.Find(key, variant)) {
        SetError(error, {});
        return cached;
    }
    const GeneratedShaderSource generated =
        GenerateShaderSource(
            asset->second,
            asset->second.domain == ShaderDomain::Surface && skinned);
    if (!generated.success) {
        SetError(error, generated.issues.empty()
            ? "Shader graph generation failed."
            : generated.issues.front().message);
        return nullptr;
    }
    m_shaderPrograms.CompileOrReload(
        key, variant, asset->second, generated.vertex, generated.fragment, {path});
    const Shader* shader = m_shaderPrograms.Find(key, variant);
    const ShaderCompileReport* report = m_shaderPrograms.LastReport(key, variant);
    if ((!report || !report->success) && !shader) {
        SetError(error, report && !report->diagnostics.empty()
            ? report->diagnostics.front().message : "Shader compilation failed.");
        return nullptr;
    }
    SetError(error, {});
    return shader;
}
const Model *RuntimeAssetManager::FindModel(const std::string &path) const
{
    const auto found = m_models.find(CacheKey(path));
    return found == m_models.end() ? nullptr : found->second.get();
}

const SkinnedModel* RuntimeAssetManager::FindSkinnedModel(const std::string& path) const {
    const auto found = m_skinnedModels.find(CacheKey(path));
    return found == m_skinnedModels.end() ? nullptr : found->second.get();
}
const Texture *RuntimeAssetManager::FindTexture(const std::string &path) const
{
    const auto found = m_textures.find(CacheKey(path));
    return found == m_textures.end() ? nullptr : found->second.get();
}

const RuntimeMaterialAsset* RuntimeAssetManager::FindMaterial(const std::string& path) const
{
    const auto found = m_materials.find(CacheKey(path));
    return found == m_materials.end() ? nullptr : found->second.get();
}

const FoliageAssetData* RuntimeAssetManager::FindFoliage(const std::string& path) const {
    const auto found = m_foliage.find(CacheKey(path));
    return found == m_foliage.end() ? nullptr : found->second.get();
}
RuntimeAssetManager::ResolveReport RuntimeAssetManager::ResolveRegistryAssets(ecs::Registry &registry)
{
    ResolveReport report;

    registry.view<ecs::ModelAsset>().each([&](ecs::Entity entity, ecs::ModelAsset& asset) {
        const bool wasCached = FindModel(asset.path) != nullptr;
        std::string error;
        const Model* model = LoadModel(asset.path, &error);
        if (!model) {
            report.errors.push_back(error);
            return;
        }

        if (!wasCached) {
            ++report.modelsLoaded;
        }
        registry.Add<ecs::LoadedModelAsset>(entity, ecs::LoadedModelAsset{model});
        ++report.modelsAssigned;
    });

    registry.view<ecs::SkinnedModelAsset>().each([&](ecs::Entity entity, ecs::SkinnedModelAsset& asset) {
        const bool wasCached = FindSkinnedModel(asset.path) != nullptr;
        std::string error;
        // Merge separate FBX clips (idle/walk/run) onto the model, or the character has
        // no clips at runtime and stays in the bind (T-)pose.
        const SkinnedModel* model = nullptr;
        if (asset.animationSources.empty()) {
            model = LoadSkinnedModel(asset.path, &error);
        } else {
            std::vector<SkinnedAnimationSource> sources;
            sources.reserve(asset.animationSources.size());
            for (const ecs::SkinnedModelAsset::AnimationSourceFile& s : asset.animationSources) {
                sources.push_back(SkinnedAnimationSource{
                    s.path, s.clipName, s.stripRootMotion, s.sourceClipName,
                    s.basePlaybackSpeed});
            }
            model = LoadSkinnedModel(asset.path, sources, &error);
        }
        if (!model) {
            report.errors.push_back(error);
            return;
        }

        if (!wasCached) {
            ++report.modelsLoaded;
        }

        auto resolveClip = [&](int fallback, const std::string& name) {
            int clip = fallback;
            if (!name.empty()) {
                const auto& animations = model->Animations();
                for (std::size_t i = 0; i < animations.size(); ++i) {
                    if (animations[i].name == name) {
                        clip = static_cast<int>(i);
                        break;
                    }
                }
            }
            return std::clamp(clip, 0, static_cast<int>(model->AnimationCount() - 1));
        };
        auto clipSeconds = [&](int clipIndex) {
            const auto& animations = model->Animations();
            if (clipIndex < 0 || clipIndex >= static_cast<int>(animations.size())) {
                return 0.0f;
            }
            const Animation& clip = animations[static_cast<std::size_t>(clipIndex)];
            const float ticksPerSecond = clip.ticksPerSecond > 0.0f ? clip.ticksPerSecond : 25.0f;
            return clip.duration > 0.0f ? clip.duration / ticksPerSecond : 0.0f;
        };

        AnimatedModel animated;
        animated.SetModel(model);
        // Render-only orientation offset: stand up + re-centre the mesh on the object
        // origin (where the capsule is), without rotating the entity transform.
        animated.renderOffset = MakeModelRenderOffset(
            asset.modelOffsetPosition, asset.modelOrientationEuler,
            asset.modelOffsetScale, model->Center());
        // Grounded foot placement (opt-in). The host supplies the ground raycast callback;
        // here we just copy the authored enable + tuning across. Leg bones auto-detect on
        // first use from the model's bone names.
        animated.footIK.enabled       = asset.footIK.enabled;
        animated.footIK.traceUp       = asset.footIK.traceUp;
        animated.footIK.traceDown     = asset.footIK.traceDown;
        animated.footIK.footHeight    = asset.footIK.footHeight;
        animated.footIK.pelvisWeight  = asset.footIK.pelvisWeight;
        animated.footIK.maxPelvisDrop = asset.footIK.maxPelvisDrop;
        animated.footIK.weight        = asset.footIK.weight;
        for (const ecs::SkinnedModelAsset::Notify& notify : asset.notifies) {
            if (notify.name.empty()) {
                continue;
            }
            animated.events.push_back(AnimEvent{
                resolveClip(std::max(notify.clipIndex, 0), notify.clipName),
                std::max(notify.time, 0.0f),
                notify.name
            });
        }
        if (model->AnimationCount() > 0 && asset.autoplay) {
            if (!asset.states.empty()) {
                // Convert the serialized component into the engine's canonical
                // graph descriptor and build the controller through the single
                // shared mapping (shared with the editor's authoring path).
                AnimationGraphDesc desc;
                desc.states.reserve(asset.states.size());
                for (const ecs::SkinnedModelAsset::AnimationState& state : asset.states) {
                    AnimationGraphDesc::StateDesc stateDesc;
                    stateDesc.name                  = state.name;
                    stateDesc.clipIndex             = state.clipIndex;
                    stateDesc.clipName              = state.clipName;
                    stateDesc.loop                  = state.loop;
                    stateDesc.speed                 = state.speed;
                    const auto baseSpeedFor = [&](int fallback, const std::string& alias) {
                        for (const auto& source : asset.animationSources)
                            if (source.clipName == alias)
                                return std::max(source.basePlaybackSpeed, 0.0f);
                        if (fallback >= 0 && fallback < static_cast<int>(asset.animationSources.size()))
                            return std::max(asset.animationSources[static_cast<std::size_t>(fallback)].basePlaybackSpeed, 0.0f);
                        return 1.0f;
                    };
                    stateDesc.clipBaseSpeed = baseSpeedFor(state.clipIndex, state.clipName);
                    stateDesc.blendClipIndex        = state.blendClipIndex;
                    stateDesc.blendClipName         = state.blendClipName;
                    stateDesc.blendParameter        = state.blendParameter;
                    stateDesc.blendMin              = state.blendMin;
                    stateDesc.blendMax              = state.blendMax;
                    stateDesc.rootMotion            = state.rootMotion;
                    stateDesc.blendParameterY       = state.blendParameterY;
                    stateDesc.blendSpace2D          = state.blendSpace2D;
                    stateDesc.synchronizeBlendSpace = state.synchronizeBlendSpace;
                    stateDesc.blendSamples.reserve(state.blendSamples.size());
                    for (const auto& sample : state.blendSamples) {
                        const int resolved = resolveClip(sample.clipIndex, sample.clipName);
                        stateDesc.blendSamples.push_back(
                            {sample.clipIndex, sample.clipName, sample.value, sample.valueY,
                             baseSpeedFor(sample.clipIndex, sample.clipName),
                             clipSeconds(resolved)});
                    }
                    desc.states.push_back(std::move(stateDesc));
                }
                desc.parameters.reserve(asset.parameters.size());
                for (const ecs::SkinnedModelAsset::AnimationParameter& parameter : asset.parameters) {
                    desc.parameters.push_back({
                        parameter.name,
                        static_cast<AnimationGraphDesc::ParamDesc::Type>(std::clamp(parameter.type, 0, 2)),
                        parameter.defaultValue
                    });
                }
                desc.transitions.reserve(asset.transitions.size());
                for (const ecs::SkinnedModelAsset::AnimationTransition& transition : asset.transitions) {
                    AnimationGraphDesc::TransitionDesc transitionDesc;
                    // The component stores integer state indices; the canonical
                    // builder resolves transitions by state name, so translate.
                    // A negative 'from' means "any state" (left empty here).
                    if (transition.from >= 0 && transition.from < static_cast<int>(asset.states.size())) {
                        transitionDesc.fromState = asset.states[static_cast<std::size_t>(transition.from)].name;
                    }
                    if (transition.to >= 0 && transition.to < static_cast<int>(asset.states.size())) {
                        transitionDesc.toState = asset.states[static_cast<std::size_t>(transition.to)].name;
                    }
                    transitionDesc.parameter    = transition.parameter;
                    transitionDesc.compare      = static_cast<AnimationGraphDesc::TransitionDesc::Compare>(
                        std::clamp(transition.compare, 0, 5));
                    transitionDesc.threshold    = transition.threshold;
                    transitionDesc.fade         = transition.fade;
                    transitionDesc.exitTime     = transition.exitTime;
                    transitionDesc.priority     = transition.priority;
                    transitionDesc.canInterrupt = transition.canInterrupt;
                    transitionDesc.useConditions = transition.useConditions;
                    transitionDesc.requireAllConditions = transition.requireAllConditions;
                    transitionDesc.additionalConditions.reserve(
                        transition.additionalConditions.size());
                    for (const auto& condition : transition.additionalConditions) {
                        transitionDesc.additionalConditions.push_back({
                            condition.parameter,
                            static_cast<AnimationGraphDesc::TransitionDesc::Compare>(
                                std::clamp(condition.compare, 0, 5)),
                            condition.threshold
                        });
                    }
                    desc.transitions.push_back(std::move(transitionDesc));
                }
                BuildAnimationController(animated.controller, desc, resolveClip, clipSeconds);
            } else if (asset.locomotionEnabled) {
                const int idle = resolveClip(asset.idleClipIndex, asset.idleClipName);
                const int walk = resolveClip(asset.walkClipIndex, asset.walkClipName);
                const int run = resolveClip(asset.runClipIndex, asset.runClipName);
                animated.controller = AnimationController::Locomotion(
                    idle,
                    walk,
                    run,
                    std::max(asset.walkAt, 0.0f),
                    std::max(asset.runAt, asset.walkAt),
                    0.2f);
            } else {
                const int clip = resolveClip(asset.clipIndex, asset.clipName);
                animated.controller.AddState(engine::AnimationController::State{
                    asset.clipName.empty() ? std::string("Default") : asset.clipName,
                    clip,
                    asset.loop,
                    std::max(asset.speed, 0.0f)
                });
            }
        }
        // Resolve socketed attachments (weapons/shields): load each static model and
        // find its bone, so DrawAnimatedModelAttachments can ride the animated bone.
        const Skeleton& skeleton = model->GetSkeleton();
        for (const ecs::SkinnedModelAsset::Attachment& a : asset.attachments) {
            const int socketBone = skeleton.Find(a.boneName);
            const glm::mat4 socketBind = (socketBone >= 0)
                ? glm::inverse(skeleton.bones[static_cast<std::size_t>(socketBone)].offset)
                : glm::mat4(1.0f);
            const glm::mat4 socketOffset =
                MakeAttachmentOffset(a.position, a.eulerDegrees, a.scale);
            if (!a.socketName.empty()) {
                const auto existing = std::find_if(
                    animated.sockets.begin(), animated.sockets.end(),
                    [&a](const NamedModelSocket& socket) {
                        return socket.name == a.socketName;
                    });
                NamedModelSocket resolvedSocket{
                    a.socketName, socketBone, socketBind, socketOffset};
                if (existing == animated.sockets.end())
                    animated.sockets.push_back(std::move(resolvedSocket));
                else
                    *existing = std::move(resolvedSocket);
            }
            if (a.path.empty()) continue;
            std::string attachError;
            const Model* attachModel = LoadModel(a.path, &attachError);
            if (!attachModel) {
                report.errors.push_back(attachError);
                continue;
            }
            ModelAttachment resolved;
            resolved.model = attachModel;
            resolved.bone = socketBone;
            resolved.boneBind = socketBind;
            resolved.localOffset = socketOffset;
            if (!a.materialPath.empty()) {
                std::string matError;
                if (const RuntimeMaterialAsset* mat = LoadMaterial(a.materialPath, &matError)) {
                    resolved.tint = mat->material.albedo;
                    if (!mat->albedoMapPath.empty()) {
                        resolved.albedoOverride = LoadTexture(mat->albedoMapPath, &matError);
                    }
                }
            }
            animated.attachments.push_back(resolved);
        }

        registry.Add<AnimatedModel>(entity, std::move(animated));
        ++report.modelsAssigned;
    });

    registry.view<ecs::MaterialAsset>().each([&](ecs::Entity entity, ecs::MaterialAsset& asset) {
        const std::string materialPath = asset.path.empty() ? asset.albedoPath : asset.path;
        if (materialPath.empty()) {
            return;
        }

        std::string error;
        ecs::LoadedMaterialAsset loaded;

        if (std::filesystem::path(materialPath).extension() == ".3dgmat") {
            const bool wasCached = FindMaterial(materialPath) != nullptr;
            const RuntimeMaterialAsset* material = LoadMaterial(materialPath, &error);
            if (!material) {
                report.errors.push_back(error);
                return;
            }
            if (!wasCached) {
                ++report.materialsLoaded;
            }
            loaded.material = material->material;
            if (!material->albedoMapPath.empty()) {
                const bool textureCached = FindTexture(material->albedoMapPath) != nullptr;
                loaded.albedoMap = LoadTexture(material->albedoMapPath, &error);
                if (!loaded.albedoMap) {
                    report.errors.push_back(error);
                    return;
                }
                if (!textureCached) {
                    ++report.texturesLoaded;
                }
            }
            if (!material->normalMapPath.empty()) {
                loaded.normalMap = LoadTexture(material->normalMapPath, &error);
            }
            if (!material->metalRoughMapPath.empty()) {
                loaded.metalRoughMap = LoadTexture(material->metalRoughMapPath, &error);
            }
            if (!material->heightMapPath.empty()) {
                loaded.heightMap = LoadTexture(material->heightMapPath, &error);
            }
            if (!material->shaderPath.empty()) {
                loaded.shader = LoadShader(material->shaderPath, false, &error);
                loaded.skinnedShader = LoadShader(material->shaderPath, true, &error);
                if (!loaded.shader)
                    report.errors.push_back(error);
                for (const RuntimeShaderParameter& parameter : material->shaderParameters)
                    loaded.shaderParameters[parameter.name] = parameter.value;
                for (const RuntimeShaderParameter& parameter : material->shaderParameters)
                    loaded.shaderParameterTypes[parameter.name] = parameter.type;
                for (const RuntimeShaderParameter& parameter : material->shaderParameters) {
                    if (parameter.type != static_cast<int>(ShaderValueType::Texture2D)
                        || parameter.value.empty()) continue;
                    const Texture* texture = LoadTexture(parameter.value, &error);
                    if (texture) loaded.shaderTextures[parameter.name] = texture;
                    else report.errors.push_back(error);
                }
                for (const auto& overrideValue : asset.parameterOverrides)
                    loaded.shaderParameters[overrideValue.first] =
                        overrideValue.second;
            }
            ++report.materialsAssigned;
        } else {
            const bool wasCached = FindTexture(materialPath) != nullptr;
            const Texture* texture = LoadTexture(materialPath, &error);
            if (!texture) {
                report.errors.push_back(error);
                return;
            }
            if (!wasCached) {
                ++report.texturesLoaded;
            }
            loaded.albedoMap = texture;
            loaded.material.albedoMap = texture;
            ++report.texturesAssigned;
        }

        // Standard surface overrides are useful even without a custom shader.
        // Decal actors use this path so their authored opacity survives runtime
        // scene export instead of being limited to the editor preview.
        if (const auto opacity = asset.parameterOverrides.find("Opacity");
            opacity != asset.parameterOverrides.end()) {
            try {
                loaded.material.opacity =
                    std::clamp(std::stof(opacity->second), 0.0f, 1.0f);
                if (loaded.material.opacity < 0.999f)
                    loaded.material.blendMode =
                        ecs::PbrMaterial::BlendMode::Transparent;
            } catch (const std::exception&) {
                report.errors.push_back(
                    "Invalid material Opacity override: " + opacity->second);
            }
        }

        loaded.material.albedoMap = loaded.albedoMap;
        loaded.material.normalMap = loaded.normalMap;
        loaded.material.metalRoughMap = loaded.metalRoughMap;
        loaded.material.heightMap = loaded.heightMap;

        // Skinned characters: the default skinned draw path reads the AnimatedModel's
        // own surface fields, not LoadedMaterialAsset (only a custom skinned shader is
        // read from the material). So a plain .3dgmat would do nothing to a character.
        // Copy the material's look onto the AnimatedModel so assigning a material
        // actually changes it.
        if (AnimatedModel* animated = registry.TryGet<AnimatedModel>(entity)) {
            animated->tint = loaded.material.albedo;
            animated->metallic = loaded.material.metallic;
            animated->roughness = loaded.material.roughness;
            animated->emissive = loaded.material.emissive;
            if (loaded.albedoMap) animated->albedoOverride = loaded.albedoMap;
        }
        registry.Add<ecs::LoadedMaterialAsset>(entity, loaded);
    });

    registry.view<ecs::FoliageComponent>().each(
        [&](ecs::Entity, ecs::FoliageComponent& component) {
            if (component.assetPath.empty()) return;
            const bool wasCached = FindFoliage(component.assetPath) != nullptr;
            std::string error;
            const FoliageAssetData* foliage = LoadFoliage(component.assetPath, &error);
            if (!foliage) {
                report.errors.push_back(error);
                return;
            }
            if (!wasCached) ++report.foliageAssetsLoaded;
            component.assetId = foliage->header.id;
            component.types.clear();
            component.types.reserve(foliage->types.size());
            for (const FoliageTypeAsset& source : foliage->types) {
                ecs::FoliageTypeRuntime type;
                type.name = source.name;
                type.meshPath = source.meshPath;
                type.meshId = source.meshId;
                type.materialPath = source.materialPath;
                type.materialId = source.materialId;
                type.lod1MeshPath = source.lod1MeshPath;
                type.lod1MeshId = source.lod1MeshId;
                type.lod2MeshPath = source.lod2MeshPath;
                type.lod2MeshId = source.lod2MeshId;
                type.cullStartDistance = source.cullStartDistance;
                type.cullEndDistance = source.cullEndDistance;
                type.lod1Distance = source.lod1Distance;
                type.lod2Distance = source.lod2Distance;
                type.windStrength = source.windStrength;
                type.castShadows = source.castShadows;
                type.collisionEnabled = source.collisionEnabled;
                type.model = LoadModel(source.meshPath, &error);
                if (!type.model) {
                    report.errors.push_back(error);
                }
                if (!source.lod1MeshPath.empty()) {
                    type.lod1Model = LoadModel(source.lod1MeshPath, &error);
                    if (!type.lod1Model) report.errors.push_back(error);
                }
                if (!source.lod2MeshPath.empty()) {
                    type.lod2Model = LoadModel(source.lod2MeshPath, &error);
                    if (!type.lod2Model) report.errors.push_back(error);
                }
                if (!source.materialPath.empty()) {
                    const RuntimeMaterialAsset* material =
                        LoadMaterial(source.materialPath, &error);
                    if (!material) {
                        report.errors.push_back(error);
                    } else {
                        type.material = material->material;
                        if (!material->albedoMapPath.empty())
                            type.material.albedoMap = LoadTexture(material->albedoMapPath, &error);
                        if (!material->normalMapPath.empty())
                            type.material.normalMap = LoadTexture(material->normalMapPath, &error);
                        if (!material->metalRoughMapPath.empty())
                            type.material.metalRoughMap = LoadTexture(material->metalRoughMapPath, &error);
                    }
                }
                component.types.push_back(std::move(type));
            }
            ++component.revision;
            ++report.foliageActorsAssigned;
        });

    return report;
}

int RuntimeAssetManager::RebuildFoliageCollisionProxies(ecs::Registry& registry) const {
    std::vector<ecs::Entity> oldProxies;
    registry.view<ecs::FoliageCollisionProxy>().each(
        [&](ecs::Entity entity, ecs::FoliageCollisionProxy&) { oldProxies.push_back(entity); });
    for (ecs::Entity entity : oldProxies) registry.Destroy(entity);

    int created = 0;
    registry.view<ecs::Transform, ecs::FoliageComponent>().each(
        [&](ecs::Entity ownerEntity, ecs::Transform& owner, ecs::FoliageComponent& foliage) {
            for (const ecs::FoliageInstance& instance : foliage.instances) {
                if (!instance.enabled || instance.typeIndex >= foliage.types.size()) continue;
                const ecs::FoliageTypeRuntime& type = foliage.types[instance.typeIndex];
                if (!type.collisionEnabled || !type.model) continue;

                const glm::vec3 localCenter = type.model->Center();
                const glm::vec3 localHalf = glm::max(
                    (type.model->Max() - type.model->Min()) * 0.5f, glm::vec3(0.02f));
                const glm::quat localRotation = glm::quat(glm::radians(instance.rotationDegrees));
                const glm::mat4 localModel = glm::translate(glm::mat4(1.0f), instance.position)
                    * glm::mat4_cast(localRotation) * glm::scale(glm::mat4(1.0f), instance.scale);
                const glm::mat4 worldModel = owner.Model() * localModel;

                ecs::Transform proxyTransform;
                proxyTransform.position = glm::vec3(worldModel * glm::vec4(localCenter, 1.0f));
                proxyTransform.rotation = owner.rotation * localRotation;
                proxyTransform.scale = glm::abs(owner.scale * instance.scale);
                const ecs::Entity proxy = registry.Create();
                registry.Add<ecs::Transform>(proxy, proxyTransform);
                ecs::Collider collider = ecs::Collider::MakeBox(localHalf);
                collider.layer = ecs::CollisionLayer::WorldStatic;
                collider.mask = ecs::CollisionLayer::All;
                collider.friction = 0.8f;
                registry.Add<ecs::Collider>(proxy, collider);
                registry.Add<ecs::FoliageCollisionProxy>(proxy,
                    {ownerEntity, instance.id});
                ++created;
            }
        });
    return created;
}
void RuntimeAssetManager::Clear()
{
    m_models.clear();
    m_skinnedModels.clear();
    m_textures.clear();
    m_materials.clear();
    m_foliage.clear();
    m_shaderPrograms.Clear();
    m_shaderAssets.clear();
}

} // namespace engine
