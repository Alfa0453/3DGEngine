#include "engine/assets/RuntimeAssetManager.h"

#include "engine/animation/AnimatedModel.h"
#include "engine/ecs/Components.h"
#include "engine/assets/ShaderGraphCompiler.h"

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

} // namespace

const Model* RuntimeAssetManager::LoadModel(const std::string &path, std::string *error)
{
    if (path.empty()) {
        SetError(error, "RuntimeAssetManager: model path is empty");
        return nullptr;
    }

    const auto existing = m_models.find(path);
    if (existing != m_models.end()) {
        SetError(error, std::string{});
        return existing->second.get();
    }

    try {
        auto model = std::make_unique<Model>(Model::FromFile(path));
        const Model* result = model.get();
        m_models.emplace(path, std::move(model));
        SetError(error, std::string{});
        return result;
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

    const auto existing = m_skinnedModels.find(path);
    if (existing != m_skinnedModels.end()) {
        SetError(error, std::string{});
        return existing->second.get();
    }

    try {
        auto model = std::make_unique<SkinnedModel>(SkinnedModel::FromFile(path));
        const SkinnedModel* result = model.get();
        m_skinnedModels.emplace(path, std::move(model));
        SetError(error, std::string{});
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

    const auto existing = m_textures.find(path);
    if (existing != m_textures.end()) {
        SetError(error, std::string{});
        return existing->second.get();
    }

    try {
        auto texture = std::make_unique<Texture>(path);
        const Texture* result = texture.get();
        m_textures.emplace(path, std::move(texture));
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

    const auto existing = m_materials.find(path);
    if (existing != m_materials.end()) {
        SetError(error, std::string{});
        return existing->second.get();
    }

    auto material = std::make_unique<RuntimeMaterialAsset>();
    if (!LoadMaterialAssetFile(path, material.get(), error)) {
        return nullptr;
    }

    const RuntimeMaterialAsset* result = material.get();
    m_materials.emplace(path, std::move(material));
    SetError(error, std::string{});
    return result;
}

const Shader* RuntimeAssetManager::LoadShader(
    const std::string& path, bool skinned, std::string* error)
{
    if (path.empty()) {
        SetError(error, "RuntimeAssetManager: shader path is empty");
        return nullptr;
    }
    auto asset = m_shaderAssets.find(path);
    if (asset == m_shaderAssets.end()) {
        ShaderAsset loaded;
        std::string loadError;
        if (!LoadShaderAsset(path, &loaded, &loadError)) {
            SetError(error, loadError);
            return nullptr;
        }
        asset = m_shaderAssets.emplace(path, std::move(loaded)).first;
    }
    const std::string variant =
        asset->second.domain == ShaderDomain::Surface
            ? (skinned ? "surface_skinned" : "surface_static")
            : asset->second.domain == ShaderDomain::PostProcess
                ? "post_process"
                : asset->second.domain == ShaderDomain::Particle
                    ? "particle"
                    : "unlit";
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
        path, variant, asset->second, generated.vertex, generated.fragment, {path});
    const Shader* shader = m_shaderPrograms.Find(path, variant);
    const ShaderCompileReport* report = m_shaderPrograms.LastReport(path, variant);
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
    const auto found = m_models.find(path);
    return found == m_models.end() ? nullptr : found->second.get();
}

const SkinnedModel* RuntimeAssetManager::FindSkinnedModel(const std::string& path) const {
    const auto found = m_skinnedModels.find(path);
    return found == m_skinnedModels.end() ? nullptr : found->second.get();
}
const Texture *RuntimeAssetManager::FindTexture(const std::string &path) const
{
    const auto found = m_textures.find(path);
    return found == m_textures.end() ? nullptr : found->second.get();
}

const RuntimeMaterialAsset* RuntimeAssetManager::FindMaterial(const std::string& path) const
{
    const auto found = m_materials.find(path);
    return found == m_materials.end() ? nullptr : found->second.get();
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
        const SkinnedModel* model = LoadSkinnedModel(asset.path, &error);
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
        for (const ecs::SkinnedModelAsset::Notify& notify : asset.notifies) {
            if (notify.name.empty()) {
                continue;
            }
            animated.events.push_back(AnimEvent{
                std::max(notify.clipIndex, 0),
                std::max(notify.time, 0.0f),
                notify.name
            });
        }
        if (model->AnimationCount() > 0 && asset.autoplay) {
            if (!asset.states.empty()) {
                for (const ecs::SkinnedModelAsset::AnimationState& state : asset.states) {
                    const int clip = resolveClip(state.clipIndex, state.clipName);
                    animated.controller.AddState(engine::AnimationController::State{
                        state.name.empty() ? std::string("State") : state.name,
                        clip,
                        state.loop,
                        std::max(state.speed, 0.0f),
                        -std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::infinity(),
                        clipSeconds(clip),
                        state.blendClipIndex >= 0 ? resolveClip(state.blendClipIndex, state.blendClipName) : -1,
                        state.blendParameter,
                        state.blendMin,
                        state.blendMax,
                        state.rootMotion
                    });
                }
                for (const ecs::SkinnedModelAsset::AnimationParameter& parameter : asset.parameters) {
                    animated.controller.DeclareParameter({
                        parameter.name,
                        static_cast<engine::AnimationController::ParameterType>(std::clamp(parameter.type, 0, 2)),
                        parameter.defaultValue
                    });
                }
                for (const ecs::SkinnedModelAsset::AnimationTransition& transition : asset.transitions) {
                    animated.controller.AddTransition(engine::AnimationController::Transition{
                        transition.from,
                        transition.to,
                        transition.parameter,
                        static_cast<engine::AnimationController::Transition::Compare>(
                            std::clamp(transition.compare, 0, 3)),
                        transition.threshold,
                        std::max(transition.fade, 0.0f),
                        std::clamp(transition.exitTime, 0.0f, 1.0f),
                        transition.priority,
                        transition.canInterrupt
                    });
                }
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

        loaded.material.albedoMap = loaded.albedoMap;
        loaded.material.normalMap = loaded.normalMap;
        loaded.material.metalRoughMap = loaded.metalRoughMap;
        loaded.material.heightMap = loaded.heightMap;
        registry.Add<ecs::LoadedMaterialAsset>(entity, loaded);
    });

    return report;
}
void RuntimeAssetManager::Clear()
{
    m_models.clear();
    m_skinnedModels.clear();
    m_textures.clear();
    m_materials.clear();
    m_shaderPrograms.Clear();
    m_shaderAssets.clear();
}

} // namespace engine
