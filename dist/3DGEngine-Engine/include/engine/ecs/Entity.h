#pragma once

#include <cstdint>
#include <limits>

namespace engine {
namespace ecs {
  
// An entity is a 32-bit handle that packs an INDEX (which slot it occupies) with
// a GENERATION (a counter bumped each time that slot is recycled). The generation
// is what lets the Registry detect a stale handle: if you keep an Entity after it
// is destroyed and its slot is reused, the generations no longer match.
//
// Layout: [ 8-bit generation | 24-bit index ]  -> up to ~16M live entities.
using Entity = std::uint32_t;

inline constexpr std::uint32_t kIndexBits = 24;
inline constexpr std::uint32_t kIndexMask = (1u << kIndexBits) - 1;      // 0x00FFFFFF
inline constexpr Entity        kNull      = std::numeric_limits<Entity>::max();

inline constexpr std::uint32_t EntityIndex(Entity e)      { return e & kIndexMask; }
inline constexpr std::uint32_t EntityGeneration(Entity e) { return e >> kIndexBits; }
inline constexpr Entity MakeEntity(std::uint32_t index, std::uint32_t generation) {
    return (generation << kIndexBits) | (index & kIndexMask);
}

} // namespace ecs
} // namespace engine