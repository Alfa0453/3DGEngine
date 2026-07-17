#include "ParticleEditorPanel.h"

#include "EditorScene.h"
#include "EditorAssets.h"
#include "ParticleAsset.h"
#include <engine/assets/ShaderAsset.h>
#include "ParticlePresets.h"

#include <engine/graphics/Framebuffer.h>
#include <engine/graphics/GpuParticleSystem.h>
#include <engine/graphics/ParticleRenderer.h>

#include <glad/glad.h>
#include <imgui.h>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>

namespace {

void ResolveParticleShader(engine::EmitterConfig& config,
                           engine::RuntimeAssetManager& assets) {
    config.customShader = nullptr;
    config.shaderTextures.clear();
    if (config.shaderPath.empty()) return;
    std::string error;
    config.customShader = assets.LoadShader(config.shaderPath, false, &error);
    for (const engine::ParticleShaderParameter& parameter :
         config.shaderParameters) {
        if (parameter.type != static_cast<int>(
                engine::ShaderValueType::Texture2D)
            || parameter.value.empty()) continue;
        const engine::Texture* texture =
            assets.LoadTexture(parameter.value, &error);
        if (texture) config.shaderTextures[parameter.name] = texture;
    }
}

engine::ParticleSystemComponent FromObject(const EditorScene::Object& object) {
    engine::ParticleSystemComponent result;
    result.config = object.particleConfig;
    result.enabled = object.particleSystemEnabled;
    result.autoplay = object.particleAutoplay;
    result.loop = object.particleLoop;
    result.prewarm = object.particlePrewarm;
    result.duration = object.particleDuration;
    result.startDelay = object.particleStartDelay;
    result.simulationSpeed = object.particleSimulationSpeed;
    result.localSpace = object.particleLocalSpace;
    result.burstCount = object.particleBurstCount;
    result.burstInterval = object.particleBurstInterval;
    return result;
}

bool Vec3Control(const char* label, glm::vec3& value, float speed = 0.05f) {
    return ImGui::DragFloat3(label, &value.x, speed);
}

bool CurveKeysControl(const char* label, bool& enabled, std::array<float, 4>& keys) {
    bool changed = ImGui::Checkbox(label, &enabled);
    if (!enabled) return changed;
    ImGui::PushID(label);
    ImGui::PlotLines("##Preview", keys.data(), static_cast<int>(keys.size()), 0, nullptr,
                     0.0f, 1.0f, ImVec2(-1.0f, 54.0f));
    changed |= ImGui::SliderFloat4("Keys", keys.data(), 0.0f, 1.0f, "%.2f");
    ImGui::PopID();
    return changed;
}

} // namespace

void ParticleEditorPanel::SyncSelection(EditorScene& scene) {
    const int selected = scene.SelectedIndex();
    if (selected == m_selectedIndex) return;
    m_selectedIndex = selected;
    const EditorScene::Object* object = scene.SelectedObject();
    m_hasSystem = object && object->particleSystemEnabled;
    if (m_hasSystem) {
        m_settings = FromObject(*object);
        m_assetPath = object->particleAssetPath;
        m_assetDirty = object->particleAssetOverride;
        m_effectLayers = object->particleEffectLayers;
        for (engine::ParticleEffectLayer& layer : m_effectLayers) {
            std::string ignored;
            if (!layer.assetPath.empty()) particle_asset::Load(layer.assetPath, &layer.system, &ignored);
        }
        if (!m_assetPath.empty()) m_assetName = std::filesystem::path(m_assetPath).stem().string();
        RestartPreview();
        RestartEffectPreview();
    } else {
        m_emitter.Clear();
        m_playing = false;
    }
}

void ParticleEditorPanel::RestartEffectPreview() {
    for (engine::ParticleEffectLayer& layer : m_effectLayers) {
        engine::ParticleSystemComponent& system = layer.system;
        system.emitter.Clear();
        system.emitter.cfg = system.config;
        system.emitter.position = layer.offset;
        system.emitter.emitting = system.enabled && system.startDelay <= 0.0f;
        system.elapsed = 0.0f;
        system.delayElapsed = 0.0f;
        system.burstElapsed = 0.0f;
        system.initialBurstFired = false;
        system.playing = system.enabled && system.autoplay;
        if (layer.enabled && system.playing && system.burstCount > 0 && system.startDelay <= 0.0f) {
            system.emitter.Burst(system.burstCount);
            system.initialBurstFired = true;
        }
    }
}

void ParticleEditorPanel::UpdateEffectPreview(float dt) {
    for (engine::ParticleEffectLayer& layer : m_effectLayers) {
        engine::ParticleSystemComponent& system = layer.system;
        const float step = std::clamp(dt, 0.0f, 0.1f) * std::max(system.simulationSpeed, 0.0f);
        system.emitter.cfg = system.config;
        system.emitter.position = layer.offset;
        if (!layer.enabled || !system.enabled || !system.playing) {
            system.emitter.emitting = false;
            system.emitter.Update(step);
            continue;
        }
        if (system.delayElapsed < system.startDelay) {
            system.delayElapsed += step;
            system.emitter.emitting = false;
            continue;
        }
        if (!system.initialBurstFired && system.burstCount > 0) {
            system.emitter.Burst(system.burstCount);
            system.initialBurstFired = true;
        }
        system.emitter.emitting = system.config.rate > 0.0f;
        system.emitter.Update(step);
        system.elapsed += step;
        if (system.duration > 0.0f && system.elapsed >= system.duration) {
            if (system.loop) {
                system.elapsed = 0.0f;
                system.initialBurstFired = false;
            } else {
                system.playing = false;
                system.emitter.emitting = false;
            }
        }
    }
}

void ParticleEditorPanel::RestartPreview() {
    m_emitter.Clear();
    m_emitter.cfg = m_settings.config;
    m_emitter.position = glm::vec3(0.0f);
    m_emitter.emitting = m_settings.startDelay <= 0.0f;
    m_elapsed = 0.0f;
    m_playing = true;
    m_settings.gpuSpawnAccumulator = 0.0f;
    m_settings.gpuPendingBurst = 0;
    m_settings.gpuBackendActive = m_settings.config.simulationBackend
            != engine::ParticleSimulationBackend::CPU
        && engine::IsGpuParticleConfigurationSupported(m_settings.config);
    if (m_settings.gpuBackendActive) {
        if (!m_settings.gpuEmitter)
            m_settings.gpuEmitter = std::make_shared<engine::GpuParticleEmitter>();
        m_settings.gpuBackendActive = m_settings.gpuEmitter->Prepare(m_settings.config);
        if (m_settings.gpuBackendActive) {
            m_settings.gpuEmitter->Reset(m_settings.config, glm::vec3(0.0f));
            if (m_settings.prewarm) {
                std::vector<engine::ParticleCollisionShape> gpuColliders;
                if (m_settings.config.collisionEnabled && m_showGround) {
                    engine::ParticleCollisionShape ground;
                    ground.type = engine::ParticleCollisionShape::Type::Plane;
                    ground.normal = glm::vec3(0, 1, 0);
                    ground.offset = 0.0f;
                    gpuColliders.push_back(ground);
                }
                const float warmTime = std::clamp(
                    m_settings.duration > 0.0f ? m_settings.duration
                                               : std::max(m_settings.config.lifeMax, 1.0f),
                    0.0f, 30.0f);
                m_settings.gpuEmitter->Prewarm(m_settings.config, glm::vec3(0.0f), warmTime,
                    m_settings.burstCount, m_settings.burstInterval, gpuColliders);
            }
        }
        if (!m_settings.prewarm && m_settings.burstCount > 0 && m_settings.startDelay <= 0.0f)
            m_settings.gpuPendingBurst = m_settings.burstCount;
    } else if (m_settings.gpuEmitter) {
        m_settings.gpuEmitter->Clear();
    }
    if (m_settings.burstCount > 0 && m_settings.startDelay <= 0.0f)
        m_emitter.Burst(m_settings.burstCount);
    if (m_settings.prewarm) {
        const float warmTime = std::clamp(
            m_settings.duration > 0.0f ? m_settings.duration
                                       : std::max(m_settings.config.lifeMax, 1.0f),
            0.0f, 30.0f);
        m_emitter.emitting = m_settings.config.rate > 0.0f;
        float burstClock = 0.0f;
        for (float t = 0.0f; t < warmTime; ) {
            const float step = std::min(1.0f / 60.0f, warmTime - t);
            if (m_settings.burstCount > 0 && m_settings.burstInterval > 0.0f) {
                burstClock += step;
                while (burstClock >= m_settings.burstInterval) {
                    m_emitter.Burst(m_settings.burstCount);
                    burstClock -= m_settings.burstInterval;
                }
            }
            if (m_settings.config.collisionEnabled && m_showGround) {
                engine::ParticleCollisionShape ground;
                ground.type = engine::ParticleCollisionShape::Type::Plane;
                ground.normal = glm::vec3(0, 1, 0);
                ground.offset = 0.0f;
                m_emitter.Update(step, std::vector<engine::ParticleCollisionShape>{ground});
            } else {
                m_emitter.Update(step);
            }
            t += step;
        }
        m_emitter.emitting = true;
    }
}

void ParticleEditorPanel::UpdatePreview(float dt) {
    if (!m_hasSystem || !m_playing) return;
    UpdateEffectPreview(dt);
    const float step = std::clamp(dt, 0.0f, 0.1f) * std::max(0.0f, m_settings.simulationSpeed);
    const float previous = m_elapsed;
    m_elapsed += step;
    m_emitter.cfg = m_settings.config;
    m_emitter.emitting = m_settings.enabled && m_elapsed >= m_settings.startDelay;

    const float activeTime = m_elapsed - m_settings.startDelay;
    if (m_settings.duration > 0.0f && activeTime >= m_settings.duration) {
        if (m_settings.loop) {
            RestartPreview();
            return;
        }
        m_emitter.emitting = false;
    }
    if (m_settings.burstCount > 0 && m_settings.burstInterval > 0.0f && activeTime >= 0.0f) {
        const int before = static_cast<int>(std::max(0.0f, previous - m_settings.startDelay) /
                                            m_settings.burstInterval);
        const int now = static_cast<int>(activeTime / m_settings.burstInterval);
        if (now > before) {
            m_emitter.Burst(m_settings.burstCount);
            m_settings.gpuPendingBurst += (now - before) * m_settings.burstCount;
        }
    } else if (m_settings.burstCount > 0 && previous < m_settings.startDelay &&
               m_elapsed >= m_settings.startDelay) {
        m_emitter.Burst(m_settings.burstCount);
        m_settings.gpuPendingBurst += m_settings.burstCount;
    }
    if (m_settings.gpuBackendActive && m_settings.gpuEmitter) {
        int spawnCount = m_settings.gpuPendingBurst;
        m_settings.gpuPendingBurst = 0;
        if (m_settings.enabled && m_elapsed >= m_settings.startDelay && m_emitter.emitting) {
            m_settings.gpuSpawnAccumulator += std::max(m_settings.config.rate, 0.0f) * step;
            const int continuous = static_cast<int>(m_settings.gpuSpawnAccumulator);
            spawnCount += continuous;
            m_settings.gpuSpawnAccumulator -= static_cast<float>(continuous);
        }
        std::vector<engine::ParticleCollisionShape> gpuColliders;
        if (m_settings.config.collisionEnabled && m_showGround) {
            engine::ParticleCollisionShape ground;
            ground.type = engine::ParticleCollisionShape::Type::Plane;
            ground.normal = glm::vec3(0, 1, 0);
            ground.offset = 0.0f;
            gpuColliders.push_back(ground);
        }
        m_settings.gpuEmitter->Update(m_settings.config, glm::vec3(0.0f), glm::vec3(0.0f),
                                      m_settings.localSpace, step, spawnCount, gpuColliders);
    }
    if (m_settings.config.collisionEnabled && m_showGround) {
        engine::ParticleCollisionShape ground;
        ground.type = engine::ParticleCollisionShape::Type::Plane;
        ground.normal = glm::vec3(0, 1, 0);
        ground.offset = 0.0f;
        m_emitter.Update(step, std::vector<engine::ParticleCollisionShape>{ground});
    } else {
        m_emitter.Update(step);
    }
}

void ParticleEditorPanel::SimulateTo(float seconds) {
    const bool loop = m_settings.loop;
    const float simulationSpeed = m_settings.simulationSpeed;
    m_settings.loop = false;
    m_settings.simulationSpeed = 1.0f;
    RestartPreview();
    while (m_elapsed + 0.0001f < seconds) {
        const float step = std::min(1.0f / 60.0f, seconds - m_elapsed);
        UpdatePreview(step);
    }
    m_playing = false;
    m_settings.loop = loop;
    m_settings.simulationSpeed = simulationSpeed;
}

unsigned int ParticleEditorPanel::RenderPreview(int width, int height, float dt) {
    try {
        width = std::max(width, 64);
        height = std::max(height, 64);
        if (!m_renderer) m_renderer = std::make_unique<engine::ParticleRenderer>();

        GLint oldFramebuffer = 0;
        GLint oldViewport[4]{};
        GLfloat oldClear[4]{};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFramebuffer);
        glGetIntegerv(GL_VIEWPORT, oldViewport);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, oldClear);

        if (m_bloom) {
            if (!m_postProcess) m_postProcess.emplace(width, height);
            else m_postProcess->Resize(width, height);
            if (!m_bloomOutput) m_bloomOutput.emplace(width, height, GL_RGBA8, false);
            else if (m_bloomOutput->Width() != width || m_bloomOutput->Height() != height)
                m_bloomOutput->Resize(width, height);
            m_postProcess->settings.bloom = true;
            m_postProcess->settings.bloomStrength = m_bloomStrength;
            m_postProcess->settings.autoExposure = false;
            m_postProcess->settings.exposure = 1.0f;
            m_postProcess->BeginScene();
        } else {
            if (!m_framebuffer) m_framebuffer.emplace(width, height, GL_RGBA8, true);
            else if (m_framebuffer->Width() != width || m_framebuffer->Height() != height)
                m_framebuffer->Resize(width, height);
            m_framebuffer->Bind();
        }
        glClearColor(m_background.r, m_background.g, m_background.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        const float yaw = glm::radians(m_yaw);
        const float pitch = glm::radians(m_pitch);
        const glm::vec3 target(0.0f, 1.0f, 0.0f);
        const glm::vec3 offset(m_distance * std::cos(pitch) * std::sin(yaw),
                               m_distance * std::sin(pitch),
                               m_distance * std::cos(pitch) * std::cos(yaw));
        m_camera.SetPosition(target + offset);
        m_camera.LookAt(target);
        if (m_settings.gpuBackendActive) {
            ResolveParticleShader(m_settings.config, m_shaderAssets);
            m_renderer->Draw(m_settings, m_camera, static_cast<float>(width) / height);
        } else {
            ResolveParticleShader(m_emitter.cfg, m_shaderAssets);
            m_renderer->Draw(m_emitter, m_camera, static_cast<float>(width) / height);
        }
        for (engine::ParticleEffectLayer& layer : m_effectLayers) {
            if (layer.enabled) {
                ResolveParticleShader(layer.system.config, m_shaderAssets);
                m_renderer->Draw(layer.system.emitter, m_camera,
                                 static_cast<float>(width) / height);
            }
        }

        unsigned int result = 0;
        if (m_bloom) {
            m_postProcess->RenderToFramebuffer(*m_bloomOutput, dt);
            result = m_bloomOutput->ColorTexture();
        } else {
            result = m_framebuffer->ColorTexture();
        }

        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(oldFramebuffer));
        glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
        glClearColor(oldClear[0], oldClear[1], oldClear[2], oldClear[3]);
        m_error.clear();
        return result;
    } catch (const std::exception& e) {
        m_error = e.what();
    } catch (...) {
        m_error = "Particle preview initialization failed";
    }
    return 0;
}

bool ParticleEditorPanel::DrawModuleStack(engine::ParticleSystemComponent& s) {
    bool changed = false;
    engine::NormalizeParticleModuleStack(s.config, false);
    ImGui::SeparatorText("Module Stack");
    ImGui::TextDisabled("Ordered authoring modules compile into the CPU/GPU emitter pipeline.");
    auto& modules = s.config.modules;
    for (std::size_t i = 0; i < modules.size(); ++i) {
        engine::ParticleModule& module = modules[i];
        if (i == 0 || modules[i - 1].stage != module.stage) {
            ImGui::Spacing();
            ImGui::SeparatorText(engine::ParticleModuleStageName(module.stage));
        }
        ImGui::PushID(static_cast<int>(i));
        const bool optional = engine::IsOptionalParticleModule(module.type);
        const std::size_t sameTypeCount = static_cast<std::size_t>(std::count_if(
            modules.begin(), modules.end(), [&module](const engine::ParticleModule& candidate) {
                return candidate.type == module.type;
            }));
        const bool removable = optional
            || (engine::SupportsDuplicateParticleModules(module.type) && sameTypeCount > 1);
        const bool toggleable = optional
            || (engine::SupportsDuplicateParticleModules(module.type) && sameTypeCount > 1);
        if (toggleable) {
            if (ImGui::Checkbox("##Enabled", &module.enabled)) {
                engine::CompileParticleModuleStack(s.config);
                changed = true;
            }
        } else {
            bool requiredEnabled = true;
            ImGui::BeginDisabled(); ImGui::Checkbox("##Enabled", &requiredEnabled); ImGui::EndDisabled();
        }
        ImGui::SameLine();
        const float labelWidth = std::max(70.0f, ImGui::GetContentRegionAvail().x - (removable ? 94.0f : 70.0f));
        const char* displayName = module.name.empty() ? engine::ParticleModuleName(module.type)
                                                       : module.name.c_str();
        if (ImGui::Selectable(displayName,
                              m_selectedModuleId == module.instanceId,
                              ImGuiSelectableFlags_None, ImVec2(labelWidth, 0.0f)))
        {
            m_selectedModule = module.type;
            m_selectedModuleId = module.instanceId;
        }
        ImGui::SameLine();
        const bool canMoveUp = i > 0 && modules[i - 1].stage == module.stage;
        if (!canMoveUp) ImGui::BeginDisabled();
        if (ImGui::SmallButton("Up")) { std::swap(modules[i], modules[i - 1]); changed = true; }
        if (!canMoveUp) ImGui::EndDisabled();
        ImGui::SameLine();
        const bool canMoveDown = i + 1 < modules.size() && modules[i + 1].stage == module.stage;
        if (!canMoveDown) ImGui::BeginDisabled();
        if (ImGui::SmallButton("Dn")) { std::swap(modules[i], modules[i + 1]); changed = true; }
        if (!canMoveDown) ImGui::EndDisabled();
        bool removed = false;
        if (removable) {
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) {
                const std::uint32_t removedId = module.instanceId;
                module.enabled = false;
                engine::CompileParticleModuleStack(s.config);
                modules.erase(modules.begin() + i);
                if (m_selectedModuleId == removedId) {
                    m_selectedModule = engine::ParticleModuleType::Spawn;
                    m_selectedModuleId = 0;
                }
                changed = true;
                removed = true;
            }
        }
        ImGui::PopID();
        if (removed) break;
    }
    if (ImGui::Button("Add Module")) {
        m_moduleSearch.fill('\0');
        ImGui::OpenPopup("Add Particle Module");
    }
    if (ImGui::BeginPopup("Add Particle Module")) {
        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputTextWithHint("##ModuleSearch", "Search modules...",
                                 m_moduleSearch.data(), m_moduleSearch.size());
        ImGui::Separator();
        std::string search = m_moduleSearch.data();
        std::transform(search.begin(), search.end(), search.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        bool found = false;
        constexpr engine::ParticleModuleType addableModules[] = {
            engine::ParticleModuleType::Forces,
            engine::ParticleModuleType::InitialVelocity,
            engine::ParticleModuleType::Rotation,
            engine::ParticleModuleType::ColorOverLife,
            engine::ParticleModuleType::SizeOverLife,
            engine::ParticleModuleType::Collision,
            engine::ParticleModuleType::Trails
        };
        for (const engine::ParticleModuleType type : addableModules) {
            const bool alreadyAdded = std::any_of(modules.begin(), modules.end(),
                [type](const engine::ParticleModule& module) { return module.type == type; });
            if (alreadyAdded && !engine::SupportsDuplicateParticleModules(type)) continue;
            std::string name = engine::ParticleModuleName(type);
            std::string lowerName = name;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (!search.empty() && lowerName.find(search) == std::string::npos) continue;
            found = true;
            if (ImGui::Selectable(name.c_str())) {
                std::uint32_t nextId = 1;
                for (const engine::ParticleModule& existing : modules)
                    nextId = std::max(nextId, existing.instanceId + 1);
                engine::ParticleModule added{type, true};
                added.instanceId = nextId;
                added.name = engine::SupportsDuplicateParticleModules(type)
                    ? name + " " + std::to_string(static_cast<int>(std::count_if(
                        modules.begin(), modules.end(), [type](const engine::ParticleModule& module) {
                            return module.type == type;
                        })) + 1)
                    : engine::ParticleModuleName(type);
                if (engine::SupportsDuplicateParticleModules(type))
                    added.parametersInitialized = true;
                if (type == engine::ParticleModuleType::SizeOverLife) {
                    added.valueA = 1.0f;
                    added.valueB = 1.0f;
                }
                modules.push_back(std::move(added));
                engine::NormalizeParticleModuleStack(s.config, true);
                m_selectedModule = type;
                m_selectedModuleId = nextId;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
        }
        if (!found) ImGui::TextDisabled("No available modules match the search.");
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset Module Order")) {
        engine::EmitterConfig defaults;
        s.config.modules = std::move(defaults.modules);
        engine::NormalizeParticleModuleStack(s.config, false);
        m_selectedModule = engine::ParticleModuleType::Spawn;
        m_selectedModuleId = s.config.modules.empty() ? 0 : s.config.modules.front().instanceId;
        changed = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Required modules are always enabled.");
    if (ImGui::CollapsingHeader("Compiled Pipeline Graph")) {
        const auto connections = engine::BuildParticleModuleConnections(s.config);
        const auto errors = engine::ValidateParticleModulePipeline(s.config);
        const std::size_t enabledCount = static_cast<std::size_t>(std::count_if(
            modules.begin(), modules.end(), [](const engine::ParticleModule& module) {
                return module.enabled;
            }));
        if (errors.empty())
            ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.45f, 1.0f),
                "Compiled successfully: %zu modules, %zu typed connections",
                enabledCount, connections.size());
        else
            for (const std::string& error : errors)
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1.0f), "! %s", error.c_str());
        auto moduleName = [&modules](std::uint32_t id) -> const char* {
            const auto it = std::find_if(modules.begin(), modules.end(),
                [id](const engine::ParticleModule& module) { return module.instanceId == id; });
            return it == modules.end() ? "Missing Module" : it->name.c_str();
        };
        for (const engine::ParticleModuleConnection& connection : connections) {
            ImGui::BulletText("%s  ->  %s", moduleName(connection.fromInstanceId),
                              moduleName(connection.toInstanceId));
            ImGui::SameLine();
            ImGui::TextDisabled("[%s]", engine::ParticleModuleDataChannelName(connection.channel));
        }
        ImGui::TextDisabled("The compiled graph is shared by CPU and GPU simulation backends.");
    }
    return changed;
}

bool ParticleEditorPanel::DrawSettings(engine::ParticleSystemComponent& s) {
    bool changed = false;
    if (ImGui::CollapsingHeader("System Playback", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::Checkbox("Enabled", &s.enabled);
        ImGui::SameLine(); changed |= ImGui::Checkbox("Autoplay", &s.autoplay);
        ImGui::SameLine(); changed |= ImGui::Checkbox("Loop", &s.loop);
        ImGui::SameLine(); changed |= ImGui::Checkbox("Prewarm", &s.prewarm);
        changed |= ImGui::DragFloat("Duration", &s.duration, 0.05f, 0.0f, 120.0f, "%.2f s");
        changed |= ImGui::DragFloat("Start Delay", &s.startDelay, 0.02f, 0.0f, 30.0f, "%.2f s");
        changed |= ImGui::DragFloat("Simulation Speed", &s.simulationSpeed, 0.01f, 0.0f, 10.0f, "%.2fx");
        changed |= ImGui::Checkbox("Local Space", &s.localSpace);
        int backend = static_cast<int>(s.config.simulationBackend);
        const char* backends[] = {"Auto", "CPU", "GPU Compute"};
        if (ImGui::Combo("Simulation Backend", &backend, backends, 3)) {
            s.config.simulationBackend = static_cast<engine::ParticleSimulationBackend>(backend);
            changed = true;
        }
        ImGui::TextDisabled("Auto uses GPU compute when supported, otherwise CPU.");
    }

    ImGui::SeparatorText(engine::ParticleModuleName(m_selectedModule));
    const auto selected = m_selectedModule;
    auto moduleIt = std::find_if(s.config.modules.begin(), s.config.modules.end(),
        [this, selected](const engine::ParticleModule& module) {
            return m_selectedModuleId != 0 ? module.instanceId == m_selectedModuleId
                                           : module.type == selected;
        });
    if (moduleIt != s.config.modules.end()) {
        m_selectedModuleId = moduleIt->instanceId;
        std::array<char, 96> moduleName{};
        std::snprintf(moduleName.data(), moduleName.size(), "%s", moduleIt->name.c_str());
        if (ImGui::InputText("Instance Name", moduleName.data(), moduleName.size())) {
            moduleIt->name = moduleName.data();
            changed = true;
        }
    }
    if (moduleIt != s.config.modules.end() && !moduleIt->enabled)
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
            "This optional module is disabled. Enable it in the stack to run it.");

    if (selected == engine::ParticleModuleType::Spawn) {
        changed |= ImGui::DragFloat("Rate", &s.config.rate, 1.0f, 0.0f, 10000.0f, "%.0f /s");
        changed |= ImGui::DragInt("Maximum Particles", &s.config.maxParticles, 10.0f, 1, 100000);
        changed |= ImGui::DragInt("Burst Count", &s.burstCount, 1.0f, 0, 100000);
        changed |= ImGui::DragFloat("Burst Interval", &s.burstInterval, 0.02f, 0.0f, 60.0f, "%.2f s");
        changed |= ImGui::DragFloatRange2("Lifetime", &s.config.lifeMin, &s.config.lifeMax,
            0.02f, 0.01f, 120.0f, "Min %.2f", "Max %.2f");
    }
    if (selected == engine::ParticleModuleType::Shape) {
        int shape = static_cast<int>(s.config.shape);
        const char* shapes[] = {"Point", "Sphere", "Cone"};
        if (ImGui::Combo("Emitter Shape", &shape, shapes, 3)) {
            s.config.shape = static_cast<engine::EmitShape>(shape); changed = true;
        }
        changed |= ImGui::DragFloat("Radius", &s.config.shapeRadius, 0.01f, 0.0f, 100.0f);
        changed |= Vec3Control("Direction", s.config.direction);
        if (s.config.shape == engine::EmitShape::Cone)
            changed |= ImGui::SliderFloat("Cone Angle", &s.config.coneAngleDeg, 0.0f, 89.0f, "%.1f deg");
    }
    if (selected == engine::ParticleModuleType::InitialVelocity) {
        if (moduleIt != s.config.modules.end()) {
            bool velocityChanged = ImGui::DragFloatRange2("Speed", &moduleIt->valueA,
                &moduleIt->valueB, 0.05f, -1000.0f, 1000.0f);
            if (velocityChanged) {
                moduleIt->parametersInitialized = true;
                engine::CompileParticleModuleStack(s.config);
                changed = true;
            }
            ImGui::TextDisabled("Enabled Initial Velocity ranges are added together.");
        }
        ImGui::TextDisabled("Direction and cone spread are configured by the Shape module.");
    }
    if (selected == engine::ParticleModuleType::Forces) {
        if (moduleIt != s.config.modules.end()) {
            bool forceChanged = Vec3Control("Gravity", moduleIt->vectorValue);
            forceChanged |= ImGui::DragFloat("Drag", &moduleIt->valueA, 0.01f, 0.0f, 100.0f);
            if (forceChanged) {
                moduleIt->parametersInitialized = true;
                engine::CompileParticleModuleStack(s.config);
                changed = true;
            }
            ImGui::TextDisabled("Enabled Force instances are added together.");
        }
    }
    if (selected == engine::ParticleModuleType::Collision) {
        changed |= ImGui::Checkbox("Enable Collision", &s.config.collisionEnabled);
        if (s.config.collisionEnabled) {
            int response = static_cast<int>(s.config.collisionResponse);
            const char* responses[] = {"Bounce", "Kill"};
            if (ImGui::Combo("Collision Response", &response, responses, 2)) {
                s.config.collisionResponse = static_cast<engine::ParticleCollisionResponse>(response);
                changed = true;
            }
            changed |= ImGui::DragFloat("Particle Collision Radius", &s.config.collisionRadius,
                0.005f, 0.0f, 10.0f);
            if (s.config.collisionResponse == engine::ParticleCollisionResponse::Bounce) {
                changed |= ImGui::SliderFloat("Bounciness", &s.config.collisionBounce, 0.0f, 2.0f);
                changed |= ImGui::SliderFloat("Surface Friction", &s.config.collisionFriction, 0.0f, 1.0f);
                changed |= ImGui::SliderFloat("Life Lost Per Hit", &s.config.collisionLifetimeLoss,
                    0.0f, 1.0f, "%.0f%%");
            }
            ImGui::TextDisabled("Enable Ground in Preview Display to preview collisions.");
        }
    }
    if (selected == engine::ParticleModuleType::Trails) {
        changed |= ImGui::Checkbox("Enable Trails", &s.config.trailsEnabled);
        if (s.config.trailsEnabled) {
            changed |= ImGui::SliderInt("Ribbon Segments", &s.config.trailSegments, 2, 16);
            changed |= ImGui::DragFloat("Ribbon Length", &s.config.trailLength, 0.05f, 0.001f, 100.0f);
            changed |= ImGui::DragFloat("Ribbon Width", &s.config.trailWidth, 0.01f, 0.0f, 10.0f);
            changed |= ImGui::SliderFloat("Ribbon Opacity", &s.config.trailOpacity, 0.0f, 1.0f);
            ImGui::TextDisabled("Motion history is rendered as a tapered camera-facing ribbon.");
        }
    }
    if (selected == engine::ParticleModuleType::Rotation) {
        if (moduleIt != s.config.modules.end()) {
            bool rotationChanged = ImGui::DragFloatRange2("Start Rotation", &moduleIt->valueA,
                &moduleIt->valueB, 1.0f, -3600.0f, 3600.0f, "Min %.0f deg", "Max %.0f deg");
            rotationChanged |= ImGui::DragFloatRange2("Rotation Speed", &moduleIt->valueC,
                &moduleIt->valueD, 1.0f, -10000.0f, 10000.0f,
                "Min %.0f deg/s", "Max %.0f deg/s");
            if (rotationChanged) {
                moduleIt->parametersInitialized = true;
                engine::CompileParticleModuleStack(s.config);
                changed = true;
            }
            ImGui::TextDisabled("Enabled Rotation ranges are added together.");
        }
    }
    if (selected == engine::ParticleModuleType::ColorOverLife) {
        if (moduleIt != s.config.modules.end()) {
            bool colorChanged = ImGui::ColorEdit4("Start Color", &moduleIt->colorValueA.x,
                ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
            colorChanged |= ImGui::ColorEdit4("End Color", &moduleIt->colorValueB.x,
                ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
            colorChanged |= CurveKeysControl("Composite Curve", moduleIt->curveEnabled,
                                              moduleIt->curveValues);
            if (colorChanged) {
                moduleIt->parametersInitialized = true;
                engine::CompileParticleModuleStack(s.config);
                changed = true;
            }
            ImGui::TextDisabled("Enabled Color instances multiply their colors and curve keys.");
        }
    }
    if (selected == engine::ParticleModuleType::SizeOverLife) {
        if (moduleIt != s.config.modules.end()) {
            bool sizeChanged = ImGui::DragFloat("Start Size Multiplier", &moduleIt->valueA,
                0.01f, 0.0f, 100.0f);
            sizeChanged |= ImGui::DragFloat("End Size Multiplier", &moduleIt->valueB,
                0.01f, 0.0f, 100.0f);
            sizeChanged |= CurveKeysControl("Composite Curve", moduleIt->curveEnabled,
                                             moduleIt->curveValues);
            if (sizeChanged) {
                moduleIt->parametersInitialized = true;
                engine::CompileParticleModuleStack(s.config);
                changed = true;
            }
            ImGui::TextDisabled("Enabled Size instances multiply their values and curve keys.");
        }
    }
    if (selected == engine::ParticleModuleType::Renderer) {
        int renderMode = static_cast<int>(s.config.renderMode);
        const char* renderModes[] = {"Billboard", "Mesh"};
        if (ImGui::Combo("Render Mode", &renderMode, renderModes, 2)) {
            s.config.renderMode = static_cast<engine::ParticleRenderMode>(renderMode);
            changed = true;
        }
        if (s.config.renderMode == engine::ParticleRenderMode::Mesh) {
            int meshShape = static_cast<int>(s.config.meshShape);
            const char* meshShapes[] = {"Cube", "Sphere", "Cone", "Cylinder", "Model Asset"};
            if (ImGui::Combo("Particle Mesh", &meshShape, meshShapes, 5)) {
                s.config.meshShape = static_cast<engine::ParticleMeshShape>(meshShape);
                changed = true;
            }
            changed |= ImGui::DragFloat("Mesh Scale", &s.config.meshScale, 0.01f, 0.001f, 1000.0f);
            changed |= ImGui::Checkbox("Align To Velocity", &s.config.meshAlignToVelocity);
            if (s.config.meshShape == engine::ParticleMeshShape::Model) {
                std::array<char, 512> meshPath{};
                std::snprintf(meshPath.data(), meshPath.size(), "%s", s.config.meshPath.c_str());
                if (ImGui::InputText("Model Asset", meshPath.data(), meshPath.size())) {
                    s.config.meshPath = meshPath.data(); changed = true;
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("3DGEDITOR_ASSET")) {
                        const char* path = static_cast<const char*>(payload->Data);
                        if (path && *path) { s.config.meshPath = path; changed = true; }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear Model")) { s.config.meshPath.clear(); changed = true; }
                ImGui::TextDisabled("Drag a model from Assets. A cube is used if loading fails.");
            }
        }
        int blend = static_cast<int>(s.config.blend);
        const char* blends[] = {"Additive", "Alpha"};
        if (ImGui::Combo("Blend Mode", &blend, blends, 2)) {
            s.config.blend = static_cast<engine::ParticleBlend>(blend); changed = true;
        }
        ImGui::SeparatorText("Texture / Flipbook");
        std::array<char, 512> texturePath{};
        std::snprintf(texturePath.data(), texturePath.size(), "%s", s.config.texturePath.c_str());
        if (ImGui::InputText("Texture", texturePath.data(), texturePath.size())) {
            s.config.texturePath = texturePath.data(); changed = true;
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("3DGEDITOR_ASSET")) {
                const char* path = static_cast<const char*>(payload->Data);
                if (path && *path) { s.config.texturePath = path; changed = true; }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear Texture")) { s.config.texturePath.clear(); changed = true; }
        changed |= ImGui::DragInt("Columns", &s.config.textureColumns, 1.0f, 1, 64);
        changed |= ImGui::DragInt("Rows", &s.config.textureRows, 1.0f, 1, 64);
        changed |= ImGui::DragFloat("Frame Rate", &s.config.textureFps, 0.5f, 0.0f, 240.0f, "%.1f fps");
        changed |= ImGui::Checkbox("Loop Flipbook", &s.config.textureLoop);
        ImGui::SeparatorText("Particle Shader");
        const EditorAssets::Asset* selectedAsset =
            m_assetsContext ? m_assetsContext->SelectedAsset() : nullptr;
        const bool selectedShader =
            selectedAsset && selectedAsset->type == EditorAssets::Type::Shader;
        if (!selectedShader) ImGui::BeginDisabled();
        if (ImGui::Button("Use Selected Particle Shader")
            && m_assetsContext) {
            const std::string path =
                m_assetsContext->SelectedAssetFullPath();
            engine::ShaderAsset shader;
            std::string error;
            if (!engine::LoadShaderAsset(path, &shader, &error))
                m_error = error;
            else if (shader.domain != engine::ShaderDomain::Particle)
                m_error = "Selected shader is not a Particle-domain shader.";
            else {
                s.config.shaderPath = path;
                s.config.shaderParameters.clear();
                for (const engine::ShaderParameter& parameter :
                     shader.parameters) {
                    s.config.shaderParameters.push_back({
                        parameter.name, static_cast<int>(parameter.type),
                        parameter.defaultValue
                    });
                }
                changed = true;
                m_error.clear();
            }
        }
        if (!selectedShader) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear Particle Shader")) {
            s.config.shaderPath.clear();
            s.config.shaderParameters.clear();
            s.config.customShader = nullptr;
            changed = true;
        }
        if (!s.config.shaderPath.empty()) {
            ImGui::TextDisabled("%s", s.config.shaderPath.c_str());
            ImGui::TextDisabled(
                "Graph shaders use the CPU billboard path for deterministic compatibility.");
            for (auto& parameter : s.config.shaderParameters) {
                std::array<char, 384> value{};
                std::snprintf(value.data(), value.size(), "%s",
                              parameter.value.c_str());
                if (ImGui::InputText(
                        parameter.name.c_str(), value.data(), value.size())) {
                    parameter.value = value.data();
                    changed = true;
                }
            }
        }
        ImGui::SeparatorText("Bounds / Culling");
        changed |= ImGui::Checkbox("Frustum Culling", &s.config.cullingEnabled);
        changed |= ImGui::DragFloat("Bounds Radius", &s.config.boundsRadius, 0.05f, 0.01f, 10000.0f);
    }
    return changed;
}

void ParticleEditorPanel::Draw(EditorScene& scene, EditorAssets& assets, bool* open, float dt) {
    m_assetsContext = &assets;
    SyncSelection(scene);
    if (!ImGui::Begin("Particle Editor", open)) { ImGui::End(); return; }

    const EditorScene::Object* object = scene.SelectedObject();
    if (!object) {
        ImGui::TextDisabled("Select an object with a Particle System component.");
        ImGui::End(); return;
    }
    if (!object->particleSystemEnabled) {
        ImGui::TextDisabled("%s has no Particle System component.", object->name.c_str());
        if (ImGui::Button("Add Particle System")) {
            engine::ParticleSystemComponent defaults;
            scene.SetSelectedParticleSystem(true, defaults);
            m_selectedIndex = -2;
        }
        ImGui::End(); return;
    }

    auto openAsset = [&](const std::string& path) {
        engine::ParticleSystemComponent loaded;
        std::string error;
        if (!particle_asset::Load(path, &loaded, &error)) { m_error = error; return; }
        m_settings = std::move(loaded);
        m_assetPath = path;
        m_assetName = std::filesystem::path(path).stem().string();
        m_assetDirty = false;
        scene.SetSelectedParticleAsset(path, m_settings, false);
        RestartPreview();
        m_error.clear();
    };
    auto newAsset = [&] {
        m_settings = engine::ParticleSystemComponent{};
        m_assetPath.clear();
        m_assetName = "NewParticle";
        m_assetDirty = true;
        scene.SetSelectedParticleAsset({}, m_settings, false);
        RestartPreview();
    };
    auto saveAsset = [&](bool saveAs) {
        std::filesystem::path path = m_assetPath;
        if (saveAs || path.empty()) {
            std::string name = m_assetName.empty() ? "NewParticle" : m_assetName;
            if (std::filesystem::path(name).extension() != ".particle") name += ".particle";
            path = std::filesystem::path(assets.RootPath()) / assets.CurrentFolder() / name;
        }
        std::string error;
        if (!particle_asset::Save(path.string(), m_settings, &error)) { m_error = error; return false; }
        m_assetPath = path.string();
        m_assetName = path.stem().string();
        m_assetDirty = false;
        scene.SetSelectedParticleAsset(m_assetPath, m_settings, false);
        scene.RefreshParticleAssetInstances(m_assetPath, m_settings);
        assets.Refresh(assets.RootPath(), &error);
        m_error = error;
        return true;
    };

    ImGui::SeparatorText("Particle Asset");
    ImGui::Text("%s%s", m_assetPath.empty() ? "Unsaved asset" : m_assetPath.c_str(),
                m_assetDirty ? " *" : "");
    std::array<char, 160> assetName{};
    std::snprintf(assetName.data(), assetName.size(), "%s", m_assetName.c_str());
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::InputText("Name", assetName.data(), assetName.size())) m_assetName = assetName.data();
    if (ImGui::Button("New")) {
        if (m_assetDirty) { m_pendingNew = true; ImGui::OpenPopup("Unsaved Particle Asset"); }
        else newAsset();
    }
    ImGui::SameLine();
    const EditorAssets::Asset* selectedAsset = assets.SelectedAsset();
    const bool canOpen = selectedAsset && selectedAsset->type == EditorAssets::Type::Particle;
    if (!canOpen) ImGui::BeginDisabled();
    if (ImGui::Button("Open Selected")) {
        const std::string path = assets.SelectedAssetFullPath();
        if (m_assetDirty) { m_pendingOpenPath = path; ImGui::OpenPopup("Unsaved Particle Asset"); }
        else openAsset(path);
    }
    if (!canOpen) ImGui::EndDisabled();
    ImGui::SameLine(); if (ImGui::Button("Save")) saveAsset(false);
    ImGui::SameLine(); if (ImGui::Button("Save As")) saveAsset(true);
    ImGui::SameLine();
    if (m_assetPath.empty()) ImGui::BeginDisabled();
    if (ImGui::Button("Revert")) openAsset(m_assetPath);
    if (m_assetPath.empty()) ImGui::EndDisabled();
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("3DGEDITOR_ASSET")) {
            const char* path = static_cast<const char*>(payload->Data);
            if (path && std::filesystem::path(path).extension() == ".particle") {
                if (m_assetDirty) { m_pendingOpenPath = path; ImGui::OpenPopup("Unsaved Particle Asset"); }
                else openAsset(path);
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupModal("Unsaved Particle Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("This particle asset has unsaved changes.");
        ImGui::TextUnformatted("Save it first, or discard the changes to continue.");
        if (ImGui::Button("Save")) {
            if (saveAsset(false)) {
                if (m_pendingNew) newAsset(); else if (!m_pendingOpenPath.empty()) openAsset(m_pendingOpenPath);
                m_pendingNew = false; m_pendingOpenPath.clear(); ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard")) {
            if (m_pendingNew) newAsset(); else if (!m_pendingOpenPath.empty()) openAsset(m_pendingOpenPath);
            m_pendingNew = false; m_pendingOpenPath.clear(); ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            m_pendingNew = false; m_pendingOpenPath.clear(); ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SeparatorText("Multi-Emitter Layers");
    std::vector<engine::ParticleEffectLayer> layers = object->particleEffectLayers;
    bool layersChanged = false;
    std::array<char, 160> effectName{};
    std::snprintf(effectName.data(), effectName.size(), "%s", m_effectAssetName.c_str());
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::InputText("Effect Name", effectName.data(), effectName.size()))
        m_effectAssetName = effectName.data();
    if (ImGui::Button("Save Effect")) {
        std::string name = m_effectAssetName.empty() ? "NewParticleEffect" : m_effectAssetName;
        if (std::filesystem::path(name).extension() != ".particlefx") name += ".particlefx";
        const std::filesystem::path path = std::filesystem::path(assets.RootPath()) /
            assets.CurrentFolder() / name;
        std::string error;
        if (particle_asset::SaveEffect(path.string(), layers, &error)) {
            m_effectAssetPath = path.string();
            m_effectAssetName = path.stem().string();
            assets.Refresh(assets.RootPath(), &error);
        }
        m_error = error;
    }
    ImGui::SameLine();
    const bool selectedEffect = selectedAsset && selectedAsset->type == EditorAssets::Type::ParticleEffect;
    if (!selectedEffect) ImGui::BeginDisabled();
    if (ImGui::Button("Open Selected Effect")) {
        std::vector<engine::ParticleEffectLayer> loadedLayers;
        std::string error;
        const std::string path = assets.SelectedAssetFullPath();
        if (particle_asset::LoadEffect(path, &loadedLayers, &error)) {
            layers = std::move(loadedLayers);
            m_effectAssetPath = path;
            m_effectAssetName = std::filesystem::path(path).stem().string();
            layersChanged = true;
        }
        m_error = error;
    }
    if (!selectedEffect) ImGui::EndDisabled();
    if (!m_effectAssetPath.empty()) ImGui::TextDisabled("Effect asset: %s", m_effectAssetPath.c_str());
    auto addLayer = [&](const std::string& path) {
        if (path.empty() || layers.size() >= 64) return;
        engine::ParticleEffectLayer layer;
        layer.assetPath = path;
        layer.name = std::filesystem::path(path).stem().string();
        std::string error;
        if (!particle_asset::Load(path, &layer.system, &error)) { m_error = error; return; }
        layers.push_back(std::move(layer));
        layersChanged = true;
    };
    if (m_assetPath.empty()) ImGui::BeginDisabled();
    if (ImGui::Button("Add Current Asset as Layer")) addLayer(m_assetPath);
    if (m_assetPath.empty()) ImGui::EndDisabled();
    ImGui::SameLine();
    const bool selectedParticleLayer = selectedAsset && selectedAsset->type == EditorAssets::Type::Particle;
    if (!selectedParticleLayer) ImGui::BeginDisabled();
    if (ImGui::Button("Add Selected Particle")) addLayer(assets.SelectedAssetFullPath());
    if (!selectedParticleLayer) ImGui::EndDisabled();
    ImGui::SameLine();
    if (layers.empty()) ImGui::BeginDisabled();
    if (ImGui::Button("Clear Layers")) { layers.clear(); layersChanged = true; }
    if (layers.empty()) ImGui::EndDisabled();
    ImGui::TextDisabled("Layers reference reusable .particle assets and inherit their latest settings.");
    for (std::size_t i = 0; i < layers.size(); ++i) {
        engine::ParticleEffectLayer& layer = layers[i];
        ImGui::PushID(static_cast<int>(i));
        const std::string label = layer.name.empty() ? "Layer" : layer.name;
        if (ImGui::TreeNodeEx("Layer", ImGuiTreeNodeFlags_DefaultOpen, "%zu. %s", i + 1, label.c_str())) {
            layersChanged |= ImGui::Checkbox("Enabled", &layer.enabled);
            std::array<char, 96> layerName{};
            std::snprintf(layerName.data(), layerName.size(), "%s", layer.name.c_str());
            if (ImGui::InputText("Name", layerName.data(), layerName.size())) {
                layer.name = layerName.data(); layersChanged = true;
            }
            layersChanged |= ImGui::DragFloat3("Local Offset", &layer.offset.x, 0.05f);
            ImGui::TextDisabled("%s", layer.assetPath.c_str());
            bool removed = false;
            if (i > 0 && ImGui::SmallButton("Up")) {
                std::swap(layers[i], layers[i - 1]); layersChanged = true;
            }
            ImGui::SameLine();
            if (i + 1 < layers.size() && ImGui::SmallButton("Down")) {
                std::swap(layers[i], layers[i + 1]); layersChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) { layers.erase(layers.begin() + i); layersChanged = true; removed = true; }
            ImGui::TreePop();
            ImGui::PopID();
            if (removed) break;
            continue;
        }
        ImGui::PopID();
    }
    if (layersChanged) {
        scene.SetSelectedParticleEffectLayers(layers);
        m_effectLayers = layers;
        for (engine::ParticleEffectLayer& layer : m_effectLayers) {
            std::string error;
            if (!particle_asset::Load(layer.assetPath, &layer.system, &error) && m_error.empty()) m_error = error;
        }
        RestartEffectPreview();
    }

    UpdatePreview(dt);
    if (dt > 0.0f) {
        const float instantFps = 1.0f / dt;
        m_previewFps += (instantFps - m_previewFps) * std::min(dt * 5.0f, 1.0f);
    }
    ImGui::Text("Editing: %s", object->name.c_str());
    ImGui::SameLine();
    const std::size_t previewAlive = m_settings.gpuBackendActive && m_settings.gpuEmitter
        ? m_settings.gpuEmitter->Alive() : m_emitter.Alive();
    ImGui::TextDisabled("| Alive %zu / %d | %s | %.2f s | %.0f FPS | %.2fx overdraw",
        previewAlive, m_settings.config.maxParticles,
        m_settings.gpuBackendActive ? "GPU Compute" : "CPU",
        m_elapsed, m_previewFps, m_overdraw);
    if (m_settings.gpuEmitter && m_settings.gpuEmitter->TextureLoadFailed())
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
            "GPU texture load failed; preview is using the CPU fallback.");
    if (m_settings.gpuBackendActive && m_settings.gpuEmitter
        && m_settings.gpuEmitter->MeshLoadFailed())
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
            "GPU model load failed; mesh particles are using the cube fallback.");
    if (m_settings.gpuBackendActive && m_settings.config.collisionEnabled
        && m_settings.gpuEmitter)
        ImGui::TextDisabled("GPU collision contacts this frame: %d",
                            m_settings.gpuEmitter->LastCollisionCount());

    if (ImGui::Button(m_playing ? "Pause" : "Play")) m_playing = !m_playing;
    ImGui::SameLine(); if (ImGui::Button("Restart")) { RestartPreview(); RestartEffectPreview(); }
    ImGui::SameLine(); if (ImGui::Button("Stop")) {
        m_playing = false; m_emitter.Clear(); m_elapsed = 0.0f;
        for (engine::ParticleEffectLayer& layer : m_effectLayers) layer.system.emitter.Clear();
    }
    ImGui::SameLine(); if (ImGui::Button("Burst")) {
        m_emitter.Burst(std::max(1, m_settings.burstCount));
        for (engine::ParticleEffectLayer& layer : m_effectLayers)
            if (layer.enabled) layer.system.emitter.Burst(std::max(1, layer.system.burstCount));
    }
    ImGui::SameLine(); if (ImGui::Button("Clear")) {
        m_emitter.Clear();
        for (engine::ParticleEffectLayer& layer : m_effectLayers) layer.system.emitter.Clear();
    }

    const float timelineMax = std::max(m_settings.duration > 0.0f ? m_settings.duration
                                                                  : m_settings.config.lifeMax, 0.1f);
    float scrubTime = std::clamp(m_elapsed, 0.0f, timelineMax);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##ParticleTimeline", &scrubTime, 0.0f, timelineMax, "Time %.2f s"))
        SimulateTo(scrubTime);

    if (ImGui::CollapsingHeader("Preview Display")) {
        ImGui::Checkbox("Grid", &m_showGrid); ImGui::SameLine();
        ImGui::Checkbox("Ground", &m_showGround); ImGui::SameLine();
        ImGui::Checkbox("Bloom", &m_bloom);
        ImGui::ColorEdit3("Background", &m_background.x);
        if (m_bloom) ImGui::SliderFloat("Bloom Strength", &m_bloomStrength, 0.0f, 2.0f);
    }

    const float previewHeight = std::clamp(ImGui::GetContentRegionAvail().y * 0.48f, 180.0f, 420.0f);
    const ImVec2 previewSize(std::max(64.0f, ImGui::GetContentRegionAvail().x), previewHeight);
    const float focalPixels = previewSize.y / (2.0f * std::tan(glm::radians(m_camera.fov) * 0.5f));
    double coveredPixels = 0.0;
    for (const engine::Particle& particle : m_emitter.Particles()) {
        const float distance = std::max(glm::length(particle.pos - m_camera.Position()), 0.1f);
        const float radiusPixels = particle.size * focalPixels / distance * 0.5f;
        coveredPixels += glm::pi<double>() * radiusPixels * radiusPixels;
    }
    m_overdraw = static_cast<float>(coveredPixels /
        std::max(1.0, static_cast<double>(previewSize.x * previewSize.y)));
    const unsigned int texture = RenderPreview(static_cast<int>(previewSize.x), static_cast<int>(previewSize.y), dt);
    if (texture) {
        ImGui::Image((ImTextureID)(std::intptr_t)texture, previewSize, ImVec2(0, 1), ImVec2(1, 0));
        const ImVec2 imageMin = ImGui::GetItemRectMin();
        const ImVec2 imageMax = ImGui::GetItemRectMax();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float horizon = imageMin.y + previewSize.y * 0.58f;
        if (m_showGround)
            draw->AddRectFilled(ImVec2(imageMin.x, horizon), imageMax, IM_COL32(70, 72, 78, 65));
        if (m_showGrid) {
            const ImU32 minor = IM_COL32(150, 160, 175, 65);
            const ImU32 major = IM_COL32(185, 195, 215, 105);
            for (int i = -8; i <= 8; ++i) {
                const float x = imageMin.x + previewSize.x * (0.5f + i * 0.075f);
                draw->AddLine(ImVec2(imageMin.x + previewSize.x * 0.5f, horizon),
                              ImVec2(x, imageMax.y), i == 0 ? major : minor);
            }
            for (int i = 0; i <= 8; ++i) {
                const float t = static_cast<float>(i) / 8.0f;
                const float y = horizon + (imageMax.y - horizon) * t * t;
                draw->AddLine(ImVec2(imageMin.x, y), ImVec2(imageMax.x, y), i == 8 ? major : minor);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGuiIO& io = ImGui::GetIO();
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                m_yaw -= io.MouseDelta.x * 0.35f;
                m_pitch = std::clamp(m_pitch + io.MouseDelta.y * 0.35f, -80.0f, 80.0f);
            }
            if (io.MouseWheel != 0.0f) m_distance = std::clamp(m_distance - io.MouseWheel * 0.5f, 1.0f, 30.0f);
            ImGui::SetTooltip("Drag to orbit | Mouse wheel to zoom");
        }
    } else if (!m_error.empty()) ImGui::TextColored(ImVec4(1, 0.35f, 0.3f, 1), "%s", m_error.c_str());

    ImGui::SeparatorText("Particle Properties");
    ImGui::TextUnformatted("Preset:");
    ImGui::SameLine();
    bool presetApplied = false;
    for (int i = 0; i < 5; ++i) {
        if (i > 0) ImGui::SameLine();
        const auto preset = static_cast<ParticlePreset>(i);
        if (ImGui::SmallButton(ParticlePresetName(preset))) {
            m_settings = MakeParticlePreset(preset);
            engine::NormalizeParticleModuleStack(m_settings.config, false);
            presetApplied = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) {
        m_settings = engine::ParticleSystemComponent{};
        presetApplied = true;
    }
    if (presetApplied) {
        scene.SetSelectedParticleSystem(true, m_settings);
        m_assetDirty = !m_assetPath.empty() || m_assetDirty;
        RestartPreview();
    }

    bool settingsChanged = DrawModuleStack(m_settings);
    settingsChanged |= DrawSettings(m_settings);
    if (settingsChanged) {
        m_settings.config.speedMax = std::max(m_settings.config.speedMax, m_settings.config.speedMin);
        m_settings.config.lifeMax = std::max(m_settings.config.lifeMax, m_settings.config.lifeMin);
        engine::SyncParticleModuleStack(m_settings.config);
        m_emitter.cfg = m_settings.config;
        if (ImGui::IsAnyItemActive()) scene.BeginParticleEdit();
        scene.SetSelectedParticleSystem(true, m_settings);
        m_assetDirty = !m_assetPath.empty() || m_assetDirty;
    }
    if (!ImGui::IsAnyItemActive()) scene.EndParticleEdit();
    for (const std::string& warning : ValidateParticleSettings(m_settings))
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.2f, 1.0f), "! %s", warning.c_str());
    ImGui::End();
}
