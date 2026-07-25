#pragma once

#include "engine/graphics/ParticleSystem.h"

#include <string>

namespace engine {

// Sets the active project's Content directory. Relative particle references such
// as "Content/Assets/Particles/Fire.particle", "Assets/Particles/Fire.particle",
// and "Fire" are resolved against this directory at runtime.
void SetParticleAssetContentRoot(const std::string& contentRoot);
std::string ParticleAssetContentRoot();
std::string ResolveParticleAssetPath(const std::string& path);

bool SaveParticleAsset(const std::string& path, const ParticleSystemComponent& settings,
                       std::string* error = nullptr);
bool LoadParticleAsset(const std::string& path, ParticleSystemComponent* settings,
                       std::string* error = nullptr);

} // namespace engine
