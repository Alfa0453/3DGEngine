#include "engine/gameplay/CameraDirector.h"

#include <utility>

namespace engine {

void CameraDirector::Play(const std::string& name, bool lockInput, bool skippable) {
    if (name.empty()) return;
    m_commands.push_back({CameraSequenceCommand::Type::Play, name, lockInput, skippable});
}

void CameraDirector::Stop() {
    m_commands.push_back({CameraSequenceCommand::Type::Stop, {}, false, false});
}

void CameraDirector::Skip() {
    m_commands.push_back({CameraSequenceCommand::Type::Skip, {}, false, false});
}

std::vector<CameraSequenceCommand> CameraDirector::TakeCommands() {
    std::vector<CameraSequenceCommand> commands = std::move(m_commands);
    m_commands.clear();
    return commands;
}

void CameraDirector::SetPlaying(const std::string& name, bool lockInput, bool skippable) {
    m_activeName = name;
    m_playing = true;
    m_lockInput = lockInput;
    m_skippable = skippable;
}

void CameraDirector::SetStopped() {
    m_activeName.clear();
    m_playing = false;
    m_lockInput = false;
    m_skippable = false;
}

void CameraDirector::NotifyFinished(const std::string& name, bool skipped) {
    if (!name.empty()) m_events.push_back({name, skipped});
    SetStopped();
}

void CameraDirector::NotifyTimelineEvent(
    const std::string& sequenceName, const std::string& eventName) {
    if (!sequenceName.empty() && !eventName.empty()) {
        m_timelineEvents.push_back({sequenceName, eventName});
    }
}

bool CameraDirector::Playing(const std::string& name) const {
    return m_playing && m_activeName == name;
}

} // namespace engine
