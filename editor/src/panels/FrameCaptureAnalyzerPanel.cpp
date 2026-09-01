#include "FrameCaptureAnalyzerPanel.h"

#include "EditorPanels.h"
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>

namespace {
using Lane = FrameCaptureAnalyzerPanel::Lane;
constexpr std::array<Lane, 10> kLanes{{Lane::Frame, Lane::Scripts, Lane::AI,
    Lane::Physics, Lane::Animation, Lane::Rendering, Lane::Particles, Lane::Audio,
    Lane::UI, Lane::GPU}};

ImU32 LaneColor(Lane lane) {
    switch (lane) {
    case Lane::Frame: return IM_COL32(120, 140, 170, 255);
    case Lane::Scripts: return IM_COL32(220, 170, 70, 255);
    case Lane::AI: return IM_COL32(180, 100, 220, 255);
    case Lane::Physics: return IM_COL32(70, 180, 230, 255);
    case Lane::Animation: return IM_COL32(240, 110, 150, 255);
    case Lane::Rendering: return IM_COL32(80, 200, 120, 255);
    case Lane::Particles: return IM_COL32(240, 140, 60, 255);
    case Lane::Audio: return IM_COL32(100, 210, 210, 255);
    case Lane::UI: return IM_COL32(150, 180, 240, 255);
    case Lane::GPU: return IM_COL32(110, 230, 100, 255);
    }
    return IM_COL32_WHITE;
}
}

FrameCaptureAnalyzerPanel::Scope::Scope(FrameCaptureAnalyzerPanel* owner,
    Lane lane, const char* name)
    : m_owner(owner), m_lane(lane), m_name(name), m_start(std::chrono::steady_clock::now()) {}

FrameCaptureAnalyzerPanel::Scope::Scope(Scope&& other) noexcept
    : m_owner(other.m_owner), m_lane(other.m_lane), m_name(other.m_name), m_start(other.m_start) {
    other.m_owner = nullptr;
}

FrameCaptureAnalyzerPanel::Scope& FrameCaptureAnalyzerPanel::Scope::operator=(Scope&& other) noexcept {
    if (this == &other) return *this;
    Finish();
    m_owner = other.m_owner; m_lane = other.m_lane; m_name = other.m_name; m_start = other.m_start;
    other.m_owner = nullptr;
    return *this;
}

FrameCaptureAnalyzerPanel::Scope::~Scope() { Finish(); }

void FrameCaptureAnalyzerPanel::Scope::Finish() {
    if (!m_owner) return;
    m_owner->AddEvent(m_lane, m_name, m_start, std::chrono::steady_clock::now());
    m_owner = nullptr;
}

void FrameCaptureAnalyzerPanel::BeginFrame(std::uint64_t frameNumber) {
    m_capturing = m_captureNext || m_continuous;
    m_captureNext = false;
    if (!m_capturing) return;
    m_frameStart = std::chrono::steady_clock::now();
    m_working = {};
    m_working.frameNumber = frameNumber;
}

FrameCaptureAnalyzerPanel::Scope FrameCaptureAnalyzerPanel::Measure(Lane lane, const char* name) {
    return m_capturing ? Scope(this, lane, name) : Scope{};
}

void FrameCaptureAnalyzerPanel::AddEvent(Lane lane, const char* name,
    std::chrono::steady_clock::time_point start,
    std::chrono::steady_clock::time_point end) {
    if (!m_capturing) return;
    Event event;
    event.lane = lane;
    event.name = name ? name : "Event";
    event.startMs = std::chrono::duration<double, std::milli>(start - m_frameStart).count();
    event.durationMs = std::chrono::duration<double, std::milli>(end - start).count();
    m_working.events.push_back(std::move(event));
}

void FrameCaptureAnalyzerPanel::EndFrame(
    const std::vector<std::pair<std::string, double>>& gpuTimings,
    int drawCalls, int physicsSteps) {
    if (!m_capturing) return;
    const auto end = std::chrono::steady_clock::now();
    m_working.durationMs = std::chrono::duration<double, std::milli>(end - m_frameStart).count();
    m_working.drawCalls = drawCalls;
    m_working.physicsSteps = physicsSteps;
    double gpuCursor = 0.0;
    for (const auto& timing : gpuTimings) {
        if (timing.second <= 0.0) continue;
        m_working.events.push_back({Lane::GPU, timing.first, gpuCursor, timing.second});
        gpuCursor += timing.second;
    }
    m_working.events.push_back({Lane::Frame, "Captured frame", 0.0, m_working.durationMs});
    m_last = std::move(m_working);
    m_selectedEvent = -1;
    m_scrollMs = 0.0f;
    m_capturing = false;
}

const char* FrameCaptureAnalyzerPanel::LaneName(Lane lane) {
    switch (lane) {
    case Lane::Frame: return "Frame";
    case Lane::Scripts: return "Scripts";
    case Lane::AI: return "AI";
    case Lane::Physics: return "Physics";
    case Lane::Animation: return "Animation";
    case Lane::Rendering: return "Rendering";
    case Lane::Particles: return "Particles";
    case Lane::Audio: return "Audio";
    case Lane::UI: return "UI";
    case Lane::GPU: return "GPU (latest resolved)";
    }
    return "Unknown";
}

void FrameCaptureAnalyzerPanel::Draw(bool* open) {
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::FrameCaptureAnalyzer), open)) {
        ImGui::End(); return;
    }
    if (ImGui::Button(m_capturing ? "Capturing...##frame_capture" : "Capture Next Frame##frame_capture"))
        RequestCapture();
    ImGui::SameLine();
    ImGui::Checkbox("Continuous##frame_capture", &m_continuous);
    ImGui::SameLine();
    if (ImGui::Button("Clear##frame_capture")) { m_last = {}; m_selectedEvent = -1; }

    if (!HasCapture()) {
        ImGui::TextDisabled("Capture a frame to inspect CPU and GPU work.");
        ImGui::End(); return;
    }

    ImGui::Text("Frame %llu  %.3f ms  %d draw calls  %d physics steps",
        static_cast<unsigned long long>(m_last.frameNumber), m_last.durationMs,
        m_last.drawCalls, m_last.physicsSteps);
    ImGui::SetNextItemWidth(180.0f);
    ImGui::SliderFloat("Zoom##frame_capture", &m_zoom, 0.5f, 20.0f, "%.1fx",
                       ImGuiSliderFlags_Logarithmic);

    const float labelWidth = 128.0f;
    const float rowHeight = 25.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = std::max(ImGui::GetContentRegionAvail().x, 300.0f);
    const float plotWidth = std::max(width - labelWidth, 100.0f);
    const float visibleMs = std::max(static_cast<float>(m_last.durationMs) / m_zoom, 0.01f);
    m_scrollMs = std::clamp(m_scrollMs, 0.0f,
        std::max(0.0f, static_cast<float>(m_last.durationMs) - visibleMs));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (std::size_t row = 0; row < kLanes.size(); ++row) {
        const float y = origin.y + static_cast<float>(row) * rowHeight;
        draw->AddText(ImVec2(origin.x + 3.0f, y + 5.0f), IM_COL32(210,210,210,255),
                      LaneName(kLanes[row]));
        draw->AddRectFilled(ImVec2(origin.x + labelWidth, y),
                            ImVec2(origin.x + width, y + rowHeight - 2.0f),
                            IM_COL32(28,31,36,255));
        for (std::size_t i = 0; i < m_last.events.size(); ++i) {
            const Event& event = m_last.events[i];
            if (event.lane != kLanes[row]) continue;
            const float x0 = origin.x + labelWidth
                + static_cast<float>((event.startMs - m_scrollMs) / visibleMs) * plotWidth;
            const float x1 = origin.x + labelWidth
                + static_cast<float>((event.startMs + event.durationMs - m_scrollMs) / visibleMs) * plotWidth;
            if (x1 < origin.x + labelWidth || x0 > origin.x + width) continue;
            const ImVec2 a(std::max(x0, origin.x + labelWidth), y + 3.0f);
            const ImVec2 b(std::min(std::max(x1, x0 + 2.0f), origin.x + width), y + rowHeight - 5.0f);
            draw->AddRectFilled(a, b, LaneColor(event.lane), 2.0f);
            if (b.x - a.x > 42.0f) draw->AddText(ImVec2(a.x + 3.0f, a.y + 1.0f),
                                                  IM_COL32(20,20,20,255), event.name.c_str());
            if (ImGui::IsMouseHoveringRect(a, b)) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(event.name.c_str());
                ImGui::Text("%s | start %.3f ms | duration %.3f ms",
                            LaneName(event.lane), event.startMs, event.durationMs);
                ImGui::EndTooltip();
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) m_selectedEvent = static_cast<int>(i);
            }
        }
    }
    ImGui::Dummy(ImVec2(width, rowHeight * static_cast<float>(kLanes.size())));
    if (m_zoom > 1.0f) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##frame_capture_scroll", &m_scrollMs, 0.0f,
            std::max(0.0f, static_cast<float>(m_last.durationMs) - visibleMs), "%.2f ms");
    }

    ImGui::SeparatorText("Longest CPU events");
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < m_last.events.size(); ++i)
        if (m_last.events[i].lane != Lane::GPU && m_last.events[i].lane != Lane::Frame)
            order.push_back(i);
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return m_last.events[a].durationMs > m_last.events[b].durationMs;
    });
    const std::size_t shown = std::min<std::size_t>(order.size(), 12);
    if (ImGui::BeginTable("##frame_capture_events", 3,
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Lane"); ImGui::TableSetupColumn("Event");
        ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableHeadersRow();
        for (std::size_t n = 0; n < shown; ++n) {
            const Event& event = m_last.events[order[n]];
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(LaneName(event.lane));
            ImGui::TableNextColumn(); ImGui::TextUnformatted(event.name.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%.3f ms", event.durationMs);
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("GPU query results are resolved asynchronously and represent the latest available GPU frame.");
    ImGui::End();
}
