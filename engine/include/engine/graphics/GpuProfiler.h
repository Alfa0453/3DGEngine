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

    void BeginFrame();               // rotate buffers + publish the ready results
    void Begin(const char* name);    // start a timed scope
    void End();                      // end the current scope

    // Per-scope GPU milliseconds from the most recently completed frame.
    const std::vector<std::pair<std::string, double>>& Results() const { return m_results; }

private:
    static constexpr int kFrames = 3;   // frames in flight before a query is read

    struct Scope { std::string name; unsigned int query = 0; };
    struct Frame { std::vector<Scope> scopes; int used = 0; };

    Frame m_frames[kFrames];
    int   m_current = 0;
    bool  m_inScope = false;
    std::vector<std::pair<std::string, double>> m_results;
};

} // namespace engine
