#include "MemoryProfilerPanel.h"

#include "EditorPanels.h"
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <map>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>
#endif

namespace {
constexpr double kMb = 1024.0 * 1024.0;

bool ContainsInsensitive(const std::string& value, const char* filter) {
    if (!filter || !*filter) return true;
    std::string needle(filter);
    std::string haystack(value);
    std::transform(needle.begin(), needle.end(), needle.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(haystack.begin(), haystack.end(), haystack.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return haystack.find(needle) != std::string::npos;
}
}

void MemoryProfilerPanel::QueryProcessMemory(std::uint64_t* workingSetBytes,
                                              std::uint64_t* privateBytes) {
    if (workingSetBytes) *workingSetBytes = 0;
    if (privateBytes) *privateBytes = 0;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
        if (workingSetBytes) *workingSetBytes = counters.WorkingSetSize;
        if (privateBytes) *privateBytes = counters.PrivateUsage;
    }
#endif
}

MemoryProfilerPanel::Totals MemoryProfilerPanel::Sum(const Snapshot& snapshot) {
    Totals totals;
    for (const Entry& entry : snapshot.entries) {
        if (!entry.resident) continue;
        totals.cpu += entry.cpuBytes;
        totals.gpu += entry.gpuBytes;
        totals.count += entry.count;
    }
    return totals;
}

const char* MemoryProfilerPanel::FormatBytes(std::uint64_t bytes, char* buffer,
                                             std::size_t bufferSize) {
    const double value = static_cast<double>(bytes);
    if (bytes >= 1024ull * 1024ull * 1024ull)
        std::snprintf(buffer, bufferSize, "%.2f GB", value / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= 1024ull * 1024ull)
        std::snprintf(buffer, bufferSize, "%.2f MB", value / kMb);
    else if (bytes >= 1024ull)
        std::snprintf(buffer, bufferSize, "%.1f KB", value / 1024.0);
    else std::snprintf(buffer, bufferSize, "%llu B", static_cast<unsigned long long>(bytes));
    return buffer;
}

void MemoryProfilerPanel::Draw(const Snapshot& snapshot, bool* open) {
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::MemoryProfiler), open)) {
        ImGui::End(); return;
    }
    const Totals totals = Sum(snapshot);
    const double now = ImGui::GetTime();
    if (m_lastSampleTime < 0.0 || now - m_lastSampleTime >= 0.25) {
        m_lastSampleTime = now;
        m_privateHistory.push_back(static_cast<float>(snapshot.processPrivateBytes / kMb));
        m_vramHistory.push_back(static_cast<float>(snapshot.driverVramUsedBytes / kMb));
        constexpr std::size_t kHistory = 240;
        if (m_privateHistory.size() > kHistory) m_privateHistory.erase(m_privateHistory.begin());
        if (m_vramHistory.size() > kHistory) m_vramHistory.erase(m_vramHistory.begin());
    }

    char a[32], b[32], c[32], d[32];
    ImGui::Text("Process private: %s", FormatBytes(snapshot.processPrivateBytes, a, sizeof(a)));
    ImGui::SameLine(); ImGui::TextDisabled("working set %s",
        FormatBytes(snapshot.processWorkingSetBytes, b, sizeof(b)));
    ImGui::Text("Tracked CPU: %s  |  tracked GPU: %s",
        FormatBytes(totals.cpu, c, sizeof(c)), FormatBytes(totals.gpu, d, sizeof(d)));
    if (snapshot.driverVramTotalBytes > 0) {
        ImGui::Text("Driver VRAM: %s / %s",
            FormatBytes(snapshot.driverVramUsedBytes, a, sizeof(a)),
            FormatBytes(snapshot.driverVramTotalBytes, b, sizeof(b)));
    } else ImGui::TextDisabled("Driver VRAM total is unavailable on this GPU/driver.");

    ImGui::SetNextItemWidth(130.0f); ImGui::DragFloat("RAM budget (MB)", &m_ramBudgetMb, 16.0f, 64.0f, 262144.0f, "%.0f");
    ImGui::SameLine(); ImGui::SetNextItemWidth(130.0f); ImGui::DragFloat("VRAM budget (MB)", &m_vramBudgetMb, 16.0f, 64.0f, 262144.0f, "%.0f");
    if (snapshot.processPrivateBytes > static_cast<std::uint64_t>(m_ramBudgetMb * kMb))
        ImGui::TextColored(ImVec4(1.0f,0.3f,0.15f,1.0f), "RAM budget exceeded by %.1f MB",
            snapshot.processPrivateBytes / kMb - m_ramBudgetMb);
    const std::uint64_t comparedVram = snapshot.driverVramUsedBytes > 0
        ? snapshot.driverVramUsedBytes : totals.gpu;
    if (comparedVram > static_cast<std::uint64_t>(m_vramBudgetMb * kMb))
        ImGui::TextColored(ImVec4(1.0f,0.3f,0.15f,1.0f), "VRAM budget exceeded by %.1f MB",
            comparedVram / kMb - m_vramBudgetMb);

    if (!m_privateHistory.empty())
        ImGui::PlotLines("Private RAM history (MB)", m_privateHistory.data(),
            static_cast<int>(m_privateHistory.size()), 0, nullptr, 0.0f,
            std::max(m_ramBudgetMb, 1.0f), ImVec2(-1.0f, 55.0f));
    if (!m_vramHistory.empty() && snapshot.driverVramUsedBytes > 0)
        ImGui::PlotLines("VRAM history (MB)", m_vramHistory.data(),
            static_cast<int>(m_vramHistory.size()), 0, nullptr, 0.0f,
            std::max(m_vramBudgetMb, 1.0f), ImVec2(-1.0f, 55.0f));

    if (ImGui::Button("Set Baseline##memory_profiler")) { m_baseline = snapshot; m_hasBaseline = true; }
    ImGui::SameLine();
    if (ImGui::Button("Clear Baseline##memory_profiler")) { m_baseline = {}; m_hasBaseline = false; }
    if (m_hasBaseline) {
        const Totals base = Sum(m_baseline);
        const auto signedMb = [](std::uint64_t value, std::uint64_t old) {
            return (static_cast<double>(value) - static_cast<double>(old)) / kMb;
        };
        ImGui::Text("Since baseline: private %+.2f MB | tracked CPU %+.2f MB | tracked GPU %+.2f MB",
            signedMb(snapshot.processPrivateBytes, m_baseline.processPrivateBytes),
            signedMb(totals.cpu, base.cpu), signedMb(totals.gpu, base.gpu));
    }

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##memory_filter", "Filter category or owner...", m_filter, sizeof(m_filter));
    std::map<std::string, Totals> categories;
    for (const Entry& entry : snapshot.entries) {
        Totals& total = categories[entry.category];
        if (entry.resident) { total.cpu += entry.cpuBytes; total.gpu += entry.gpuBytes; }
        total.count += entry.count;
    }
    if (ImGui::BeginTable("##memory_ownership", 4,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
            | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 330.0f))) {
        ImGui::TableSetupColumn("Owner", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 65.0f);
        ImGui::TableSetupColumn("CPU", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("GPU", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableHeadersRow();
        for (const auto& [category, total] : categories) {
            bool categoryMatches = ContainsInsensitive(category, m_filter);
            bool anyChildMatches = categoryMatches;
            if (!anyChildMatches) for (const Entry& entry : snapshot.entries)
                if (entry.category == category && ContainsInsensitive(entry.owner, m_filter)) { anyChildMatches = true; break; }
            if (!anyChildMatches) continue;
            ImGui::TableNextRow(); ImGui::TableNextColumn();
            const bool expanded = ImGui::TreeNodeEx(category.c_str(), ImGuiTreeNodeFlags_SpanAllColumns);
            ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(total.count));
            ImGui::TableNextColumn(); ImGui::TextUnformatted(FormatBytes(total.cpu, a, sizeof(a)));
            ImGui::TableNextColumn(); ImGui::TextUnformatted(FormatBytes(total.gpu, b, sizeof(b)));
            if (!expanded) continue;
            for (std::size_t i = 0; i < snapshot.entries.size(); ++i) {
                const Entry& entry = snapshot.entries[i];
                if (entry.category != category || (!categoryMatches && !ContainsInsensitive(entry.owner, m_filter))) continue;
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow(); ImGui::TableNextColumn();
                ImGui::Indent(); ImGui::TextDisabled("%s%s", entry.owner.c_str(), entry.resident ? "" : " (not resident)"); ImGui::Unindent();
                ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(entry.count));
                ImGui::TableNextColumn(); ImGui::TextUnformatted(FormatBytes(entry.cpuBytes, a, sizeof(a)));
                ImGui::TableNextColumn(); ImGui::TextUnformatted(FormatBytes(entry.gpuBytes, b, sizeof(b)));
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Tracked totals cover engine-owned resources; process private memory includes allocator, driver, DLL, and editor overhead.");
    ImGui::End();
}
