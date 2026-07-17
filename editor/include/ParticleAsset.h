#pragma once

#include <engine/graphics/ParticleSystem.h>

#include <string>

namespace particle_asset {

bool Save(const std::string& path, const engine::ParticleSystemComponent& settings,
          std::string* error = nullptr);
bool Load(const std::string& path, engine::ParticleSystemComponent* settings,
          std::string* error = nullptr);
bool SaveEffect(const std::string& path, const std::vector<engine::ParticleEffectLayer>& layers,
                std::string* error = nullptr);
bool LoadEffect(const std::string& path, std::vector<engine::ParticleEffectLayer>* layers,
                std::string* error = nullptr);

} // namespace particle_asset
