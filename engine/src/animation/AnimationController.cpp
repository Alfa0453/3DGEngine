#include "engine/animation/AnimationController.h"

#include <algorithm>
#include <cmath>

namespace engine {

int AnimationController::AddState(const State& s) {
    const int idx = static_cast<int>(m_states.size());
    m_states.push_back(s);
    if (m_cur < 0) { m_cur = idx; m_curTime = 0.0f; m_blend = 1.0f; }
    return idx;
}

void AnimationController::AddTransition(const Transition& t) {
    if (t.from < -1 || t.to < 0
        || t.from >= static_cast<int>(m_states.size())
        || t.to >= static_cast<int>(m_states.size())
        || t.from == t.to) {
        return;
    }
    m_transitions.push_back(t);
}

void AnimationController::DeclareParameter(const ParameterDefinition& definition) {
    if (definition.name.empty()) return;
    for (ParameterDefinition& current : m_parameterDefinitions) {
        if (current.name == definition.name) {
            current = definition;
            return;
        }
    }
    m_parameterDefinitions.push_back(definition);
    SetParameter(definition.name, definition.defaultValue);
}

AnimationController::ParameterType AnimationController::ParameterKind(const std::string& name) const {
    for (const ParameterDefinition& definition : m_parameterDefinitions) {
        if (definition.name == name) return definition.type;
    }
    return ParameterType::Float;
}

AnimationController AnimationController::Locomotion(int idleClip, int walkClip, int runClip,
                                                   float walkAt, float runAt, float crossfadeSeconds) {
    AnimationController c;
    c.crossfade = crossfadeSeconds;
    const float inf = std::numeric_limits<float>::infinity();
    c.AddState({"Idle", idleClip, true, 1.0f, -inf,   walkAt});
    c.AddState({"Walk", walkClip, true, 1.0f, walkAt, runAt });
    c.AddState({"Run",  runClip,  true, 1.0f, runAt,  inf   });
    return c;
}

void AnimationController::SetParameter(const std::string& name, float value) {
    const std::string key = name.empty() ? std::string("Speed") : name;
    m_parameters[key] = value;
    if (key == "Speed") {
        m_param = value;
    }
}

void AnimationController::SetBoolParameter(const std::string& name, bool value) {
    SetParameter(name, value ? 1.0f : 0.0f);
}

void AnimationController::SetTriggerParameter(const std::string& name) {
    if (name.empty()) {
        return;
    }
    m_triggers.insert(name);
    SetParameter(name, 1.0f);
}

void AnimationController::ResetTriggerParameter(const std::string& name) {
    if (name.empty()) {
        return;
    }
    SetParameter(name, 0.0f);
    m_triggers.erase(name);
}

float AnimationController::Parameter(const std::string& name, float fallback) const {
    const std::string key = name.empty() ? std::string("Speed") : name;
    if (key == "Speed") {
        return m_param;
    }
    const auto found = m_parameters.find(key);
    return found == m_parameters.end() ? fallback : found->second;
}

bool AnimationController::BoolParameter(const std::string& name, bool fallback) const {
    const std::string key = name.empty() ? std::string("Speed") : name;
    const auto found = m_parameters.find(key);
    if (found == m_parameters.end()) {
        return fallback;
    }
    return std::abs(found->second) > 0.0001f;
}

void AnimationController::Play(int stateIndex, bool immediate) {
    if (stateIndex < 0 || stateIndex >= static_cast<int>(m_states.size())) return;
    if (stateIndex == m_cur && !Blending()) return;
    Begin(stateIndex, immediate);
}

bool AnimationController::TestTransition(const Transition& transition) const {
    if (!ExitTimeReached(transition)) {
        return false;
    }

    const float value = Parameter(transition.parameter, 0.0f);

    switch (transition.compare) {
    case Transition::Compare::GreaterOrEqual:
        return value >= transition.threshold;
    case Transition::Compare::Less:
        return value < transition.threshold;
    case Transition::Compare::Equal:
        return std::abs(value - transition.threshold) <= 0.0001f;
    case Transition::Compare::NotEqual:
        return std::abs(value - transition.threshold) > 0.0001f;
    }
    return false;
}

bool AnimationController::ExitTimeReached(const Transition& transition) const {
    if (transition.exitTime <= 0.0f || m_cur < 0) {
        return true;
    }
    const State& state = m_states[static_cast<std::size_t>(m_cur)];
    const float duration = state.durationSeconds;
    if (duration <= 0.0f) {
        return true;
    }
    return (m_curTime / duration) >= transition.exitTime;
}

const AnimationController::Transition* AnimationController::BestTransition() const {
    if (m_cur < 0) {
        return nullptr;
    }

    const Transition* best = nullptr;
    for (const Transition& transition : m_transitions) {
        if ((transition.from != -1 && transition.from != m_cur)
            || transition.to == m_cur
            || (Blending() && !transition.canInterrupt)
            || !TestTransition(transition)) {
            continue;
        }
        if (!best || transition.priority > best->priority) {
            best = &transition;
        }
    }
    return best;
}

std::vector<AnimationController::TransitionDebugInfo> AnimationController::TransitionDebug() const {
    std::vector<TransitionDebugInfo> rows;
    rows.reserve(m_transitions.size());
    const Transition* selected = BestTransition();

    for (const Transition& transition : m_transitions) {
        TransitionDebugInfo row;
        if (transition.from == -1) {
            row.fromState = "Any State";
        } else if (transition.from >= 0 && transition.from < static_cast<int>(m_states.size())) {
            row.fromState = m_states[static_cast<std::size_t>(transition.from)].name;
        }
        if (transition.to >= 0 && transition.to < static_cast<int>(m_states.size())) {
            row.toState = m_states[static_cast<std::size_t>(transition.to)].name;
        }
        row.parameter = transition.parameter;
        row.value = Parameter(transition.parameter, 0.0f);
        row.threshold = transition.threshold;
        row.exitTime = transition.exitTime;
        row.priority = transition.priority;
        row.canInterrupt = transition.canInterrupt;
        row.exitTimeReached = ExitTimeReached(transition);
        row.blockedByBlend = Blending() && !transition.canInterrupt;
        switch (transition.compare) {
        case Transition::Compare::GreaterOrEqual:
            row.conditionMet = row.value >= transition.threshold;
            break;
        case Transition::Compare::Less:
            row.conditionMet = row.value < transition.threshold;
            break;
        case Transition::Compare::Equal:
            row.conditionMet = std::abs(row.value - transition.threshold) <= 0.0001f;
            break;
        case Transition::Compare::NotEqual:
            row.conditionMet = std::abs(row.value - transition.threshold) > 0.0001f;
            break;
        }
        row.eligible = (transition.from == -1 || transition.from == m_cur)
            && transition.to != m_cur
            && row.conditionMet
            && row.exitTimeReached
            && !row.blockedByBlend;
        row.selected = selected == &transition;
        rows.push_back(std::move(row));
    }
    return rows;
}

int AnimationController::PickByTransition() const {
    const Transition* transition = BestTransition();
    return transition ? transition->to : m_cur;
}

int AnimationController::PickByParam() const {
    // Keep the current state if its range still contains the parameter (avoids
    // oscillation at the boundary); otherwise pick the first state that matches.
    if (m_cur >= 0) {
        const State& s = m_states[static_cast<std::size_t>(m_cur)];
        if (m_param >= s.paramMin && m_param <= s.paramMax) return m_cur;
    }
    for (std::size_t i = 0; i < m_states.size(); ++i) {
        const State& s = m_states[i];
        // Only states with a finite range participate in parameter selection.
        const bool ranged = s.paramMin > -std::numeric_limits<float>::infinity()
                         || s.paramMax <  std::numeric_limits<float>::infinity();
        if (ranged && m_param >= s.paramMin && m_param <= s.paramMax)
            return static_cast<int>(i);
    }
    return m_cur;   // no match -> hold
}

void AnimationController::Begin(int to, bool immediate) {
    Begin(to, immediate, crossfade);
}

void AnimationController::Begin(int to, bool immediate, float fadeSeconds) {
    m_prev     = m_cur;
    m_prevTime = m_curTime;
    m_cur      = to;
    m_curTime  = 0.0f;
    crossfade = std::max(fadeSeconds, 0.0f);
    m_blend    = (immediate || m_prev < 0 || crossfade <= 0.0f) ? 1.0f : 0.0f;
    if (m_blend >= 1.0f) m_prev = -1;
}

void AnimationController::Update(float dt) {
    if (m_cur < 0) return;

    // Parameter-driven transition.
    const Transition* transition = BestTransition();
    if (transition) {
        const int want = transition->to;
        const float fade = transition->fade;
        std::string consumedTrigger;
        if (m_triggers.find(transition->parameter) != m_triggers.end()) {
            consumedTrigger = transition->parameter;
        }
        Begin(want, false, fade);
        if (!consumedTrigger.empty()) {
            ResetTriggerParameter(consumedTrigger);
        }
    } else if (m_transitions.empty()) {
        int want = PickByParam();
        if (want != m_cur) Begin(want, false);
    }

    // Advance clip time(s).
    m_curTime += dt * m_states[static_cast<std::size_t>(m_cur)].speed;
    if (m_prev >= 0) m_prevTime += dt * m_states[static_cast<std::size_t>(m_prev)].speed;

    // Progress the cross-fade.
    if (m_prev >= 0 && m_blend < 1.0f) {
        m_blend += (crossfade > 0.0f) ? dt / crossfade : 1.0f;
        if (m_blend >= 1.0f) { m_blend = 1.0f; m_prev = -1; }
    }
}

const std::string& AnimationController::CurrentStateName() const {
    static const std::string kNone = "(none)";
    return (m_cur >= 0) ? m_states[static_cast<std::size_t>(m_cur)].name : kNone;
}

float AnimationController::CurrentBlendWeight() const {
    if (m_cur < 0) return 0.0f;
    const State& state = m_states[static_cast<std::size_t>(m_cur)];
    if (state.blendClip < 0 || state.blendParameter.empty()) return 0.0f;
    const float span = state.blendMax - state.blendMin;
    if (std::abs(span) <= 0.0001f) return 0.0f;
    return std::clamp((Parameter(state.blendParameter) - state.blendMin) / span, 0.0f, 1.0f);
}

} // namespace engine
