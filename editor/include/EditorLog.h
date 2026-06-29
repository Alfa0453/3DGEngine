#pragma once

#include <string>
#include <vector>

class EditorLog {
public:
    enum class Level {
        Info,
        Warning,
        Error
    };

    struct Entry {
        Level level = Level::Info;
        std::string message;
    };

    void Info(const std::string& message);
    void Warning(const std::string& message);
    void Error(const std::string& message);
    void Clear();

    const std::vector<Entry>& Entries() const { return m_entries; }
    const std::string& LatestMessage() const { return m_latestMessage; }

    static const char* LevelName(Level level);

private:
    void Add(Level level, const std::string& message);

    std::vector<Entry> m_entries;
    std::string m_latestMessage = "Ready";
    static constexpr std::size_t kMaxEntries = 64;
};