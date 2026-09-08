#pragma once

// Scripting Pass 4 -- deterministic RNG service (Phase 15).
//
// An engine-owned random service that scripts use instead of ad-hoc std::random, so gameplay can be
// made reproducible (replays, deterministic tests, networked lockstep). Two separate streams:
//   * Gameplay  -- seeded and deterministic; the same seed replays the exact same sequence.
//   * Cosmetic  -- reseeded from wall-clock by the host; for particles/visual jitter where
//                  determinism does not matter and must NOT perturb the gameplay stream.
// This does NOT replace std::random elsewhere in the engine (per the pass scope) -- it is the
// script-facing service. Header-only, GL-free, no global state.

#include <cstdint>

namespace engine {
namespace script {

// SplitMix64 -- tiny, fast, well-distributed, fully deterministic from its seed. Chosen over
// std::mt19937 because its state is one 64-bit word (cheap to seed per-stream and to serialize into
// a save/replay) and it has no platform/library-dependent behavior.
class DeterministicRng {
public:
    explicit DeterministicRng(std::uint64_t seed = 0) { Seed(seed); }

    void Seed(std::uint64_t seed) { m_state = seed ? seed : 0x9E3779B97F4A7C15ull; }
    std::uint64_t State() const { return m_state; }              // for save/replay
    void SetState(std::uint64_t s) { m_state = s; }

    std::uint64_t NextU64() {
        std::uint64_t z = (m_state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    // Uniform float in [0,1).
    float NextFloat() { return static_cast<float>(NextU64() >> 40) * (1.0f / 16777216.0f); }

    // Inclusive integer range [minInclusive, maxInclusive]; order-tolerant; unbiased enough for games.
    int RandomInt(int minInclusive, int maxInclusive) {
        if (maxInclusive < minInclusive) { int t = minInclusive; minInclusive = maxInclusive; maxInclusive = t; }
        const std::uint64_t span = static_cast<std::uint64_t>(maxInclusive - minInclusive) + 1ull;
        return minInclusive + static_cast<int>(NextU64() % span);
    }
    // Float in [a, b) (or [b, a) if reversed).
    float RandomFloat(float a = 0.0f, float b = 1.0f) { return a + NextFloat() * (b - a); }
    float RandomRange(float a, float b) { return RandomFloat(a, b); }
    // 50/50 (or weighted) boolean.
    bool RandomBool(float pTrue = 0.5f) { return NextFloat() < pTrue; }
    // Derive an independent child stream (e.g. per-entity) without disturbing this one.
    DeterministicRng Fork(std::uint64_t salt) const {
        DeterministicRng r(m_state ^ (salt * 0x9E3779B97F4A7C15ull));
        return r;
    }

private:
    std::uint64_t m_state = 0x9E3779B97F4A7C15ull;
};

// The script-facing service: one deterministic gameplay stream + one cosmetic stream.
class RngService {
public:
    void SeedGameplay(std::uint64_t seed) { m_gameplay.Seed(seed); }
    void SeedCosmetic(std::uint64_t seed) { m_cosmetic.Seed(seed); }

    DeterministicRng& Gameplay() { return m_gameplay; }
    DeterministicRng& Cosmetic() { return m_cosmetic; }
    const DeterministicRng& Gameplay() const { return m_gameplay; }

    // Convenience passthroughs to the gameplay (deterministic) stream -- the default scripts use.
    int   RandomInt(int lo, int hi)          { return m_gameplay.RandomInt(lo, hi); }
    float RandomFloat(float a = 0, float b = 1) { return m_gameplay.RandomFloat(a, b); }
    float RandomRange(float a, float b)      { return m_gameplay.RandomRange(a, b); }
    bool  RandomBool(float p = 0.5f)         { return m_gameplay.RandomBool(p); }

private:
    DeterministicRng m_gameplay{0x123456789ABCDEFull};
    DeterministicRng m_cosmetic{0xC0FFEEULL};
};

} // namespace script
} // namespace engine
