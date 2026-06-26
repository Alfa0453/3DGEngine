#pragma once

#include "engine/core/Window.h"

#include <memory>

namespace engine {

// Base class for a game built on the engine.
//
// It owns the window and runs the main loop. A game subclasses Application and
// overrides the virtual hooks; it never has to write a loop of its own (the
// "template method" pattern: the base fixes the loop's shape, subclasses fill in
// behaviour).
//
// The loop runs at two clocks:
//   * OnUpdate(dt)      — once per rendered frame, variable dt. Good for input
//                         and anything that should feel immediate.
//   * OnFixedUpdate(h)  — zero or more times per frame at a FIXED step h, via an
//                         accumulator. Good for physics: it makes the simulation
//                         deterministic and independent of frame rate.
//   * OnRender()        — once per frame; read InterpolationAlpha() to smoothly
//                         interpolate between the last two fixed states.
class Application {
public:
    explicit Application(const WindowProps& props = {});
    virtual ~Application() = default;

    // Enter the main loop. Returns when the window is closed. Call once.
    void Run();

protected:
    // Override these in your game. Default implementations do nothing, so you
    // only implement the hooks you need.
    virtual void OnInit()                        {}                    // load resources
    virtual void OnUpdate(float deltaTime)       { (void)deltaTime; }  // per-frame (variable)
    virtual void OnFixedUpdate(float fixedDelta) { (void)fixedDelta; } // physics (fixed)
    virtual void OnRender()                      {}                    // draw the frame
    virtual void OnShutdown()                    {}                    // release resources

    Window& GetWindow() { return *m_window; }

    // Fraction in [0,1) of the way from the previous fixed step to the next.
    // Use it in OnRender to interpolate fast-moving objects: their motion then
    // looks smooth even when the render rate differs from the physics rate.
    float InterpolationAlpha() const { return m_alpha; }

    // The fixed physics step, in seconds (default 1/120 s). Set before Run().
    void SetFixedTimeStep(float seconds) { m_fixedDelta = seconds; }
    float FixedTimeStep() const { return m_fixedDelta; }

private:
    std::unique_ptr<Window> m_window;
    bool m_running = false;
    float m_fixedDelta = 1.0f / 120.0f; // physics ticks 120x per second
    float m_accumulator = 0.0f;         // unsimulated time carried between frames
    float m_alpha       = 0.0f;         // render interpolation factor
};

} // namespace engine