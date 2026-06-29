#include "EditorLog.h"

void EditorLog::Info(const std::string &message)
{
    Add(Level::Info, message);
}

void EditorLog::Warning(const std::string &message)
{
    Add(Level::Warning, message);
}

void EditorLog::Error(const std::string &message)
{
    Add(Level::Error, message);
}

void EditorLog::Clear()
{
    m_entries.clear();
    m_latestMessage = "Log cleared";
}

const char *EditorLog::LevelName(Level level)
{
    switch (level) {
    case Level::Info: return "Info";
    case Level::Warning: return "Warn";
    case Level::Error: return "Error";
    }
    return "Info";
}

void EditorLog::Add(Level level, const std::string &message)
{
    m_latestMessage = message;
    m_entries.push_back({level, message});
    if (m_entries.size() > kMaxEntries) {
        m_entries.erase(m_entries.begin());
    }
}
