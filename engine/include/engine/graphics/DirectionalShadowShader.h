#pragma once

#include <string>

namespace engine {

// Shared directional CSM/PCSS shader implementation used by both the static
// and skinned PBR fragment shaders. Keeping one source block prevents shadow
// quality or bias behavior from drifting between the two render paths.
inline constexpr int kDirectionalBlockerSamples = 16;
inline constexpr int kDirectionalFilterSamples = 24;

std::string ComposeDirectionalShadowShader(const std::string& fragmentSource);

} // namespace engine
