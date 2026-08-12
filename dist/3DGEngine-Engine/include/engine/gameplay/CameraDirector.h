#pragma once

#include <string>
#include <vector>

namespace engine {

struct CameraSequenceCommand {
    enum class Type { Play, Stop, Skip };
    Type type = Type::Play;
    std::string name;
    bool lockInput = true;
    bool skippable = true;
};

struct CameraSequenceEvent {
    std::string name;
    bool skipped = false;
};

struct CameraTimelineEvent {
    std::string sequenceName;
    std::string eventName;
};

// Command/event bridge used by gameplay scripts. The host application resolves
// sequence names and owns the actual camera playback.
class CameraDirector {
public:
    void Play(const std::string& name, bool lockInput = true, bool skippable = true);
    void Stop();
    void Skip();

    std::vector<CameraSequenceCommand> TakeCommands();
    void SetPlaying(const std::string& name, bool lockInput, bool skippable);
    void SetStopped();
    void NotifyFinished(const std::string& name, bool skipped);
    void NotifyTimelineEvent(const std::string& sequenceName, const std::string& eventName);
    void ClearEvents() { m_events.clear(); m_timelineEvents.clear(); }

    bool Playing() const { return m_playing; }
    bool Playing(const std::string& name) const;
    bool InputLocked() const { return m_playing && m_lockInput; }
    bool Skippable() const { return m_playing && m_skippable; }
    const std::string& ActiveName() const { return m_activeName; }
    const std::vector<CameraSequenceEvent>& Events() const { return m_events; }
    const std::vector<CameraTimelineEvent>& TimelineEvents() const { return m_timelineEvents; }

private:
    std::vector<CameraSequenceCommand> m_commands;
    std::vector<CameraSequenceEvent> m_events;
    std::vector<CameraTimelineEvent> m_timelineEvents;
    std::string m_activeName;
    bool m_playing = false;
    bool m_lockInput = false;
    bool m_skippable = false;
};

} // namespace engine
