#pragma once

#include <string>
#include <utility>
#include <vector>

namespace engine {

// A lightweight GPU timer built on GL_TIME_ELAPSED queries. Results are read from a
// few frames back so the CPU never stalls waiting on the GPU. Scopes are SEQUENTIAL
// (no nesting -- only one GL_TIME_ELAPSED query may be active at a time).
//
//   profiler.BeginFrame();
//   profiler.Begin("Scene");  drawScene();  profiler.End();
//   profiler.Begin("Post");   post();       profiler.End();
//   for (auto& [name, ms] : profiler.Results()) ...   // previous-frame timings
class GpuProfiler {
public:
    ~GpuProfiler();

    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool Enabled() const { return m_enabled; }

    void BeginFrame();               // rotate buffers + publish the ready results
    void Begin(const char* name);    // start a timed scope
    void End();                      // end the current scope

    // Per-scope GPU milliseconds from the most recently completed frame.
    const std::vector<std::pair<std::string, double>>& Results() const { return m_results; }
    int DrawCalls() const {
        return m_enabled ? m_frames[m_current].drawCalls : 0;
    }
    // Directional shadow-map caster draws this frame, split by culling policy — lets the
    // profiler confirm closed architecture uses the one-sided (front-face) solid policy.
    int OneSidedShadowDraws() const {
        return m_enabled ? m_frames[m_current].shadowDrawsOneSided : 0;
    }
    int TwoSidedShadowDraws() const {
        return m_enabled ? m_frames[m_current].shadowDrawsTwoSided : 0;
    }

    // Render backends call this once for every glDraw* submission. The active
    // profiler is optional, so player builds can use the same renderers without
    // paying for counters when no profiler is installed.
    static void RecordDrawCall();
    // Called per shadow-map caster draw with its culling policy (twoSided = no cull).
    static void RecordShadowDraw(bool twoSided);

private:
    static constexpr int kFrames = 3;   // frames in flight before a query is read

    struct Scope { std::string name; unsigned int query = 0; };
    struct Frame { std::vector<Scope> scopes; int used = 0; int drawCalls = 0;
                  int shadowDrawsOneSided = 0; int shadowDrawsTwoSided = 0; };

    Frame m_frames[kFrames];
    int   m_current = 0;
    bool  m_inScope = false;
    bool  m_enabled = true;
    std::vector<std::pair<std::string, double>> m_results;
    static GpuProfiler* s_active;
};

} // namespace engine
