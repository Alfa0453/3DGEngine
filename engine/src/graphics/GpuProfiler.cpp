#include "engine/graphics/GpuProfiler.h"

#include <glad/glad.h>

namespace engine {

GpuProfiler* GpuProfiler::s_active = nullptr;

GpuProfiler::~GpuProfiler() {
    if (s_active == this) s_active = nullptr;
    for (Frame& f : m_frames) {
        for (Scope& s : f.scopes) {
            if (s.query) glDeleteQueries(1, &s.query);
        }
    }
}

void GpuProfiler::BeginFrame() {
    if (!m_enabled) {
        if (s_active == this) s_active = nullptr;
        m_results.clear();
        m_inScope = false;
        return;
    }
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
    f.drawCalls = 0;
    f.shadowDrawsOneSided = 0;
    f.shadowDrawsTwoSided = 0;
    m_inScope = false;
    s_active = this;
}

void GpuProfiler::Begin(const char* name) {
    if (!m_enabled) return;
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
    if (!m_enabled || !m_inScope) return;
    glEndQuery(GL_TIME_ELAPSED);
    ++m_frames[m_current].used;
    m_inScope = false;
}

void GpuProfiler::RecordDrawCall() {
    if (s_active && s_active->m_enabled)
        ++s_active->m_frames[s_active->m_current].drawCalls;
}

void GpuProfiler::RecordShadowDraw(bool twoSided) {
    if (!s_active || !s_active->m_enabled) return;
    Frame& f = s_active->m_frames[s_active->m_current];
    if (twoSided) ++f.shadowDrawsTwoSided; else ++f.shadowDrawsOneSided;
}

} // namespace engine
