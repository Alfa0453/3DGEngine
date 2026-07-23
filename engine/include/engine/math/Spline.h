#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace engine {

// A Catmull-Rom spline through a list of control points. General purpose and reusable:
// river flow, motion paths for platforms/props, camera rails, spawn lanes, patrol
// routes, road/fence generation, etc. Pure math (no GL), so it is unit-testable.
//
// Header-only (all methods inline) so it needs no separate translation unit / CMake
// entry -- just include and use.
//
// Sampling comes in two flavours:
//   * Parametric  -- Position(t)/Tangent(t) with t in [0,1] across the whole curve.
//   * Arc-length  -- PositionAtDistance(d)/TangentAtDistance(d) for even-speed motion.
// The arc-length lookup table is built lazily and cached until the points change.
class Spline {
public:
    Spline() = default;
    explicit Spline(std::vector<glm::vec3> points, bool closed = false)
        : m_points(std::move(points)), m_closed(closed) {}

    void SetPoints(std::vector<glm::vec3> points) { m_points = std::move(points); m_lutDirty = true; }
    void SetClosed(bool closed) { if (m_closed != closed) { m_closed = closed; m_lutDirty = true; } }
    void AddPoint(const glm::vec3& point) { m_points.push_back(point); m_lutDirty = true; }
    void InsertPoint(std::size_t index, const glm::vec3& point) {
        index = std::min(index, m_points.size());
        m_points.insert(m_points.begin() + static_cast<std::ptrdiff_t>(index), point);
        m_lutDirty = true;
    }
    void RemovePoint(std::size_t index) {
        if (index >= m_points.size()) return;
        m_points.erase(m_points.begin() + static_cast<std::ptrdiff_t>(index));
        m_lutDirty = true;
    }
    void SetPoint(std::size_t index, const glm::vec3& point) {
        if (index >= m_points.size()) return;
        m_points[index] = point;
        m_lutDirty = true;
    }

    const std::vector<glm::vec3>& Points() const { return m_points; }
    std::size_t PointCount() const { return m_points.size(); }
    bool  Closed() const { return m_closed; }
    bool  Valid()  const { return m_points.size() >= 2; }

    // Parametric sampling (t clamped to [0,1]).
    glm::vec3 Position(float t) const {
        const int segs = SegmentCount();
        if (segs <= 0) return m_points.empty() ? glm::vec3(0.0f) : m_points.front();
        t = std::clamp(t, 0.0f, 1.0f);
        const float scaled = t * static_cast<float>(segs);
        int seg = static_cast<int>(std::floor(scaled));
        if (seg >= segs) seg = segs - 1;
        return EvalSegment(seg, scaled - static_cast<float>(seg));
    }
    glm::vec3 Tangent(float t) const {
        const int segs = SegmentCount();
        if (segs <= 0) return glm::vec3(0.0f, 0.0f, 1.0f);
        t = std::clamp(t, 0.0f, 1.0f);
        const float scaled = t * static_cast<float>(segs);
        int seg = static_cast<int>(std::floor(scaled));
        if (seg >= segs) seg = segs - 1;
        const glm::vec3 d = EvalSegmentTangent(seg, scaled - static_cast<float>(seg));
        const float len = glm::length(d);
        return (len > 1.0e-6f) ? d / len : glm::vec3(0.0f, 0.0f, 1.0f);
    }

    // Arc-length sampling.
    float Length() const { EnsureLut(); return m_length; }
    glm::vec3 PositionAtDistance(float distance) const {
        EnsureLut();
        if (m_lutPosition.empty()) return glm::vec3(0.0f);
        if (m_lutPosition.size() == 1 || m_length <= 1.0e-6f) return m_lutPosition.front();
        distance = std::clamp(distance, 0.0f, m_length);
        const auto it = std::lower_bound(m_lutDistance.begin(), m_lutDistance.end(), distance);
        std::size_t hi = static_cast<std::size_t>(it - m_lutDistance.begin());
        if (hi == 0) return m_lutPosition.front();
        if (hi >= m_lutPosition.size()) return m_lutPosition.back();
        const std::size_t lo = hi - 1;
        const float span = m_lutDistance[hi] - m_lutDistance[lo];
        const float f = span > 1.0e-6f ? (distance - m_lutDistance[lo]) / span : 0.0f;
        return glm::mix(m_lutPosition[lo], m_lutPosition[hi], f);
    }
    glm::vec3 TangentAtDistance(float distance) const {
        EnsureLut();
        if (m_length <= 1.0e-6f) return glm::vec3(0.0f, 0.0f, 1.0f);
        return Tangent(std::clamp(distance / m_length, 0.0f, 1.0f));
    }

    // Closest point on the curve to a world position (LUT-approximate). Optionally
    // returns the arc-length distance to that point and the curve tangent there.
    glm::vec3 ClosestPoint(const glm::vec3& world, float* outDistance = nullptr,
                           glm::vec3* outTangent = nullptr) const {
        EnsureLut();
        if (m_lutPosition.empty()) {
            if (outDistance) *outDistance = 0.0f;
            if (outTangent)  *outTangent = glm::vec3(0.0f, 0.0f, 1.0f);
            return glm::vec3(0.0f);
        }
        std::size_t best = 0;
        float bestSq = std::numeric_limits<float>::max();
        for (std::size_t i = 0; i < m_lutPosition.size(); ++i) {
            const glm::vec3 d = m_lutPosition[i] - world;
            const float sq = glm::dot(d, d);
            if (sq < bestSq) { bestSq = sq; best = i; }
        }
        if (outDistance) *outDistance = m_lutDistance[best];
        if (outTangent)  *outTangent = TangentAtDistance(m_lutDistance[best]);
        return m_lutPosition[best];
    }

    // Evenly-spaced points along the curve (for drawing a smooth polyline). Fills `out`.
    void SampleUniform(int count, std::vector<glm::vec3>& out) const {
        out.clear();
        if (count < 2 || !Valid()) { for (const glm::vec3& p : m_points) out.push_back(p); return; }
        EnsureLut();
        out.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            const float d = m_length * static_cast<float>(i) / static_cast<float>(count - 1);
            out.push_back(PositionAtDistance(d));
        }
    }

private:
    int SegmentCount() const {
        const int n = static_cast<int>(m_points.size());
        if (n < 2) return 0;
        return m_closed ? n : n - 1;
    }
    glm::vec3 ControlPoint(int index) const {
        const int n = static_cast<int>(m_points.size());
        if (n == 0) return glm::vec3(0.0f);
        if (m_closed) { index = ((index % n) + n) % n; return m_points[static_cast<std::size_t>(index)]; }
        index = std::clamp(index, 0, n - 1);
        return m_points[static_cast<std::size_t>(index)];
    }
    glm::vec3 EvalSegment(int segment, float u) const {
        const glm::vec3 p0 = ControlPoint(segment - 1), p1 = ControlPoint(segment);
        const glm::vec3 p2 = ControlPoint(segment + 1), p3 = ControlPoint(segment + 2);
        const float u2 = u * u, u3 = u2 * u;
        return 0.5f * ((2.0f * p1) + (-p0 + p2) * u
                       + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * u2
                       + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * u3);
    }
    glm::vec3 EvalSegmentTangent(int segment, float u) const {
        const glm::vec3 p0 = ControlPoint(segment - 1), p1 = ControlPoint(segment);
        const glm::vec3 p2 = ControlPoint(segment + 1), p3 = ControlPoint(segment + 2);
        const float u2 = u * u;
        return 0.5f * ((-p0 + p2)
                       + 2.0f * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * u
                       + 3.0f * (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * u2);
    }
    void EnsureLut() const {
        if (!m_lutDirty) return;
        m_lutDirty = false;
        m_lutDistance.clear();
        m_lutPosition.clear();
        m_length = 0.0f;
        const int segs = SegmentCount();
        if (segs <= 0) {
            if (!m_points.empty()) { m_lutPosition.push_back(m_points.front()); m_lutDistance.push_back(0.0f); }
            return;
        }
        const int total = segs * kSamplesPerSegment;
        glm::vec3 prev = EvalSegment(0, 0.0f);
        m_lutPosition.push_back(prev);
        m_lutDistance.push_back(0.0f);
        for (int i = 1; i <= total; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(total);
            const float scaled = t * static_cast<float>(segs);
            int seg = static_cast<int>(std::floor(scaled));
            if (seg >= segs) seg = segs - 1;
            const glm::vec3 pos = EvalSegment(seg, scaled - static_cast<float>(seg));
            m_length += glm::length(pos - prev);
            m_lutPosition.push_back(pos);
            m_lutDistance.push_back(m_length);
            prev = pos;
        }
    }

    std::vector<glm::vec3> m_points;
    bool m_closed = false;

    mutable bool                   m_lutDirty = true;
    mutable std::vector<float>     m_lutDistance;
    mutable std::vector<glm::vec3> m_lutPosition;
    mutable float                  m_length = 0.0f;

    static constexpr int kSamplesPerSegment = 16;
};

} // namespace engine
