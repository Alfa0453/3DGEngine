#pragma once

#include <functional>
#include <string>
#include <vector>

namespace engine {
namespace ai {

// A generic finite state machine templated on a context type Ctx (the agent's
// blackboard). States have optional enter/update/exit callbacks; transitions have
// a condition predicate and fire when true. A transition with from == kAny fires
// from any state. Used to choose an agent's behaviour (patrol / chase / flee).
template <class Ctx>
class StateMachine {
public:
    static constexpr int kAny = -1;
    using UpdateFn = std::function<void(Ctx&, float)>;  // per-frame while active
    using Hook  = std::function<void(Ctx&)>;            // on enter / exit
    using Cond  = std::function<bool(Ctx&)>;            // transition guard

    int AddState(const std::string& name, UpdateFn onUpdate = {}, Hook onEnter = {}, Hook onExit = {}) {
        m_states.push_back({name, std::move(onUpdate), std::move(onEnter), std::move(onExit)});
        return static_cast<int>(m_states.size()) - 1;
    }
    void AddTransition(int from, int to, Cond cond) {
        m_transitions.push_back({from, to, std::move(cond)});
    }

    void Start(int state, Ctx& c) {
        m_current = state;
        if (m_current >= 0 && m_states[static_cast<std::size_t>(m_current)].onEnter)
            m_states[static_cast<std::size_t>(m_current)].onEnter(c);
    }

    void Tick(Ctx& c, float dt) {           // check transitions, then run the current state
        for (const Transition& t : m_transitions) {
            if (t.to == m_current) continue;
            if (t.from != kAny && t.from != m_current) continue;
            if (t.cond && t.cond(c)) { SwitchTo(t.to, c); break; }
        }
        if (m_current >= 0 && m_states[static_cast<std::size_t>(m_current)].onUpdate)
            m_states[static_cast<std::size_t>(m_current)].onUpdate(c, dt);
    }

    int Current() const { return m_current; }
    const std::string& CurrentName() const {
        static const std::string none = "(none)";
        return (m_current >= 0) ? m_states[static_cast<std::size_t>(m_current)].name : none;
    }

private:
    struct State      { std::string name; UpdateFn onUpdate; Hook onEnter; Hook onExit; };
    struct Transition { int from; int to; Cond cond; };

    void SwitchTo(int to, Ctx& c) {
        if (m_current >= 0 && m_states[static_cast<std::size_t>(m_current)].onExit)
            m_states[static_cast<std::size_t>(m_current)].onExit(c);
        m_current = to;
        if (m_current >= 0 && m_states[static_cast<std::size_t>(m_current)].onEnter)
            m_states[static_cast<std::size_t>(m_current)].onEnter(c);
    }

    std::vector<State>      m_states;
    std::vector<Transition> m_transitions;
    int                     m_current = -1;
};
}
}