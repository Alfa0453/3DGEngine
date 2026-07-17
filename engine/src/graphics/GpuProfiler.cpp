#include "engine/graphics/GpuProfiler.h"

#include <glad/glad.h>

namespace engine {

GpuProfiler::~GpuProfiler() {
    for (Frame& f : m_frames) {
        for (Scope& s : f.scopes) {
            if (s.query) glDeleteQueries(1, &s.query);
        }
    }
}

void GpuProfiler::BeginFrame() {
    // Rotate to a slot last written kFrames-1 frames ago -- its queries are ready.
    m_current = (m_current + 1) % kFrames;
    Frame& f = m_frames[m_current];

    m_results.clear();
    for (int i = 0; i < f.used; ++i) {
        GLint available = 0;
        glGetQueryObjectiv(f.scopes[i].query, GL_QUERY_RESULT_AVAILABLE, &available);
        if (available) {
            GLuint64 ns = 0;
            glGetQueryObjectui64v(f.scopes[i].query, GL_QUERY_RESULT, &ns);
            m_results.emplace_back(f.scopes[i].name, static_cast<double>(ns) / 1.0e6);  // ns -> ms
        }
    }

    f.used = 0;        // reuse this slot for the new frame
    m_inScope = false;
}

void GpuProfiler::Begin(const char* name) {
    if (m_inScope) End();   // scopes can't nest; close a dangling one defensively
    Frame& f = m_frames[m_current];
    if (f.used >= static_cast<int>(f.scopes.size())) {
        Scope s;
        glGenQueries(1, &s.query);
        f.scopes.push_back(s);
    }
    Scope& s = f.scopes[static_cast<std::size_t>(f.used)];
    s.name = name;
    glBeginQuery(GL_TIME_ELAPSED, s.query);
    m_inScope = true;
}

void GpuProfiler::End() {
    if (!m_inScope) return;
    glEndQuery(GL_TIME_ELAPSED);
    ++m_frames[m_current].used;
    m_inScope = false;
}

} // namespace engine
