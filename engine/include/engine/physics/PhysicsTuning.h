#pragma once

// Pass-5 production tuning: quality presets (Phases 51-52) and a hardened fixed-timestep
// accumulator (Phases 49-50). Header-only so it needs no new .cpp / CMake reconfigure. These are
// authored/config values -- they change solver *effort*, never collision *semantics* (a Low preset
// still uses the same shapes and the same CCD guarantee for bodies that opt into it).

#include "engine/physics/PhysicsWorld.h"

#include <algorithm>

namespace engine {

enum class PhysicsQuality { Low, Medium, High, Ultra };

// Solver-effort knobs a preset controls. Kept as a plain struct so the editor can show the exact
// numbers a preset resolves to, and so a project can start from a preset and override one field.
struct PhysicsQualitySettings {
    int substeps;            // TGS substeps per fixed step (stack coherence / fast-body robustness)
    int velocityIterations;  // sequential-impulse velocity passes
    int positionIterations;  // split-impulse position passes
    int ccdMaxIterations;    // per-body CCD sweep iteration budget (Phase 32; consumed once CCD budgeting lands)
};

inline PhysicsQualitySettings QualitySettingsFor(PhysicsQuality q) {
    switch (q) {
        // Cheap: fine for casual / mobile / preview. One substep + light iteration. Normal-speed
        // bodies still collide correctly; genuinely fast bodies still need CCD (unchanged).
        case PhysicsQuality::Low:    return { 1,  6, 2, 2 };
        case PhysicsQuality::Medium: return { 2, 10, 3, 4 };
        // High == the engine's shipped defaults (holds tall/tilted stacks in the headless tests).
        case PhysicsQuality::High:   return { 4, 14, 4, 6 };
        // Ultra: heavy stacks / showcase. Diminishing returns beyond this.
        case PhysicsQuality::Ultra:  return { 8, 20, 6, 8 };
    }
    return { 4, 14, 4, 6 };
}

// Apply a preset to a world. Only effort knobs are touched; gravity, filtering, broad-phase cell
// size, restitution threshold, etc. are left as the caller set them.
inline void ApplyPhysicsQuality(PhysicsWorld& world, PhysicsQuality q) {
    const PhysicsQualitySettings s = QualitySettingsFor(q);
    world.substeps          = s.substeps;
    world.solverIterations  = s.velocityIterations;
    world.positionIterations = s.positionIterations;
}

inline const char* PhysicsQualityName(PhysicsQuality q) {
    switch (q) { case PhysicsQuality::Low: return "Low"; case PhysicsQuality::Medium: return "Medium";
                 case PhysicsQuality::High: return "High"; case PhysicsQuality::Ultra: return "Ultra"; }
    return "High";
}

// Fixed-timestep accumulator with spiral-of-death protection (Phase 49). Feed it the variable
// render frame time; it returns how many fixed steps to run this frame, capped so a long stall
// (breakpoint, asset load, alt-tab) can never queue an unbounded backlog that then runs all at once
// and stalls further -- the classic spiral of death. Leftover time under one step is retained for
// smooth pacing; leftover *above* the cap is dropped (the sim runs slightly slow for that one frame
// rather than freezing). alpha() gives the 0..1 remainder for render interpolation.
struct FixedStepAccumulator {
    float fixedDt         = 1.0f / 60.0f;  // the deterministic step size
    int   maxStepsPerFrame = 8;            // hard cap on steps run in one frame
    float accumulator     = 0.0f;

    // Returns the number of fixed steps to run now. Call Step(fixedDt) that many times.
    int Advance(float frameDt) {
        if (!(frameDt > 0.0f)) frameDt = 0.0f;                 // guard NaN / negative frame times
        // Clamp a single monstrous frame up front so accumulator can't overflow to infinity.
        accumulator += std::min(frameDt, fixedDt * static_cast<float>(maxStepsPerFrame) * 4.0f);
        int steps = 0;
        while (accumulator >= fixedDt && steps < maxStepsPerFrame) { accumulator -= fixedDt; ++steps; }
        // Spiral-of-death clamp: if we hit the step cap and a backlog still remains, discard it.
        if (steps == maxStepsPerFrame && accumulator > fixedDt) accumulator = 0.0f;
        return steps;
    }
    float alpha() const { return fixedDt > 0.0f ? accumulator / fixedDt : 0.0f; }
    void  reset() { accumulator = 0.0f; }
};

} // namespace engine
