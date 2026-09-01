#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class FrameCaptureAnalyzerPanel {
public:
    enum class Lane {
        Frame,
        Scripts,
        AI,
        Physics,
        Animation,
        Rendering,
        Particles,
        Audio,
        UI,
        GPU
    };

    struct Event {
        Lane lane = Lane::Frame;
        std::string name;
        double startMs = 0.0;
        double durationMs = 0.0;
    };

    struct Capture {
        std::uint64_t frameNumber = 0;
        double durationMs = 0.0;
        int drawCalls = 0;
        int physicsSteps = 0;
        std::vector<Event> events;
    };

    class Scope {
    public:
        Scope() = default;
        Scope(FrameCaptureAnalyzerPanel* owner, Lane lane, const char* name);
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        Scope(Scope&& other) noexcept;
        Scope& operator=(Scope&& other) noexcept;
        ~Scope();

    private:
        void Finish();
        FrameCaptureAnalyzerPanel* m_owner = nullptr;
        Lane m_lane = Lane::Frame;
        const char* m_name = nullptr;
        std::chrono::steady_clock::time_point m_start{};
    };

    void BeginFrame(std::uint64_t frameNumber);
    Scope Measure(Lane lane, const char* name);
    void EndFrame(const std::vector<std::pair<std::string, double>>& gpuTimings,
                  int drawCalls, int physicsSteps);
    void Draw(bool* open);

    void RequestCapture() { m_captureNext = true; }
    bool Capturing() const { return m_capturing; }
    bool HasCapture() const { return !m_last.events.empty(); }
    const Capture& LastCapture() const { return m_last; }

    static const char* LaneName(Lane lane);

private:
    friend class Scope;
    void AddEvent(Lane lane, const char* name,
                  std::chrono::steady_clock::time_point start,
                  std::chrono::steady_clock::time_point end);

    bool m_captureNext = false;
    bool m_continuous = false;
    bool m_capturing = false;
    std::chrono::steady_clock::time_point m_frameStart{};
    Capture m_working;
    Capture m_last;
    float m_zoom = 1.0f;
    float m_scrollMs = 0.0f;
    int m_selectedEvent = -1;
};
