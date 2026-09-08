#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

class MemoryProfilerPanel {
public:
    struct Entry {
        std::string category;
        std::string owner;
        std::uint64_t cpuBytes = 0;
        std::uint64_t gpuBytes = 0;
        std::uint64_t count = 1;
        bool resident = true;
    };

    struct Snapshot {
        std::uint64_t processWorkingSetBytes = 0;
        std::uint64_t processPrivateBytes = 0;
        std::uint64_t driverVramUsedBytes = 0;
        std::uint64_t driverVramTotalBytes = 0;
        std::vector<Entry> entries;
    };

    static void QueryProcessMemory(std::uint64_t* workingSetBytes,
                                   std::uint64_t* privateBytes);
    void Draw(const Snapshot& snapshot, bool* open);

private:
    struct Totals { std::uint64_t cpu = 0, gpu = 0, count = 0; };
    static Totals Sum(const Snapshot& snapshot);
    static const char* FormatBytes(std::uint64_t bytes, char* buffer,
                                   std::size_t bufferSize);

    Snapshot m_baseline;
    bool m_hasBaseline = false;
    float m_ramBudgetMb = 4096.0f;
    float m_vramBudgetMb = 4096.0f;
    char m_filter[128]{};
    std::vector<float> m_privateHistory;
    std::vector<float> m_vramHistory;
    double m_lastSampleTime = -1.0;
};
