#pragma once

#include "engine/graphics/ParticleSystem.h"

#include <string>

namespace engine {

bool SaveParticleAsset(const std::string& path, const ParticleSystemComponent& settings,
                       std::string* error = nullptr);
bool LoadParticleAsset(const std::string& path, ParticleSystemComponent* settings,
                       std::string* error = nullptr);

} // namespace engine
