#pragma once

// Scripting Pass 4 -- unified sequence/timer scheduler (Phases 11-14).
//
// Extends the Pass-1 per-script ScriptSequence idea into ONE runtime-owned scheduler with stable
// ownership and cancellation. The existing Script::Sequence()/SetTimer API is unchanged; this is the
// central engine that such flows can be backed by. Every flow has an owner ScriptHandle, a unique id,
// and an optional group name, so it can be cancelled individually, by group, by owner, or
// automatically when its owner entity dies -- and no delayed callback can ever fire into a
// destroyed/unloaded script.
//
// Wait kinds: Do (action), Delay (seconds), Repeat (every interval, N times), WaitUntil (predicate),
// WaitForEvent (by stable ScriptEventId via the event bus -- NOT per-frame string polling),
// WaitForFixedSteps (N fixed physics steps). WaitForAnimation/WaitForActionClip/WaitForSceneLoad are
// modeled as WaitForEvent on the relevant runtime completion event (the runtime posts it), so no
// asset-file polling. GL-free, header-only.

#include "engine/gameplay/Script.h"           // ScriptHandle, ecs::Entity
#include "engine/gameplay/ScriptReflection.h" // ScriptEventId

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace engine {
namespace script {

using SequenceId = std::uint64_t;

class ScriptScheduler; // fwd

// Fluent builder for one flow. Steps run top-to-bottom across frames.
class Flow {
public:
    Flow& Do(std::function<void()> action) { m_steps.push_back(Step::MakeAction(std::move(action))); return *this; }
    Flow& Delay(float seconds)            { m_steps.push_back(Step::MakeDelay(seconds)); return *this; }
    Flow& WaitUntil(std::function<bool()> cond) { m_steps.push_back(Step::MakeWaitUntil(std::move(cond))); return *this; }
    Flow& WaitForEvent(ScriptEventId id)  { m_steps.push_back(Step::MakeWaitForEvent(id)); return *this; }
    Flow& WaitForFixedSteps(int steps)    { m_steps.push_back(Step::MakeWaitFixed(steps)); return *this; }
    // Run `action` every `interval` seconds, `count` times (count < 0 == forever until cancelled).
    Flow& Repeat(int count, float interval, std::function<void()> action) {
        m_steps.push_back(Step::MakeRepeat(count, interval, std::move(action))); return *this;
    }
    SequenceId Id() const { return m_id; }

private:
    friend class ScriptScheduler;
    struct Step {
        enum class Kind { Action, Delay, WaitUntil, WaitForEvent, WaitForFixedSteps, Repeat };
        Kind kind = Kind::Action;
        std::function<void()> action;
        std::function<bool()> condition;
        float         seconds = 0.0f;
        ScriptEventId eventId = 0;
        int           intArg = 0;         // fixed steps remaining, or repeat count remaining
        float         timer = 0.0f;
        bool          eventFired = false;

        static Step MakeAction(std::function<void()> a){ Step s; s.kind=Kind::Action; s.action=std::move(a); return s; }
        static Step MakeDelay(float sec){ Step s; s.kind=Kind::Delay; s.seconds=sec<0?0:sec; return s; }
        static Step MakeWaitUntil(std::function<bool()> c){ Step s; s.kind=Kind::WaitUntil; s.condition=std::move(c); return s; }
        static Step MakeWaitForEvent(ScriptEventId id){ Step s; s.kind=Kind::WaitForEvent; s.eventId=id; return s; }
        static Step MakeWaitFixed(int n){ Step s; s.kind=Kind::WaitForFixedSteps; s.intArg=n<0?0:n; return s; }
        static Step MakeRepeat(int count,float interval,std::function<void()> a){ Step s; s.kind=Kind::Repeat; s.intArg=count; s.seconds=interval<0?0:interval; s.action=std::move(a); return s; }
    };

    SequenceId         m_id = 0;
    ScriptHandle       m_owner;
    std::string        m_group;
    std::vector<Step>  m_steps;
    std::size_t        m_cursor = 0;
    bool               m_cancelled = false;

    bool Done() const { return m_cancelled || m_cursor >= m_steps.size(); }
};

class ScriptScheduler {
public:
    // Optional: lets the scheduler auto-cancel flows whose owner entity has died (Phase 12).
    void SetOwnerAliveCheck(std::function<bool(ecs::Entity)> fn) { m_ownerAlive = std::move(fn); }

    // Begin a flow owned by `owner`, in optional `group`. Returns a reference to build it; keep the
    // returned Id() for later cancellation. The flow starts advancing on the next Tick.
    Flow& Start(ScriptHandle owner, const std::string& group = {}) {
        // Started flows go to a pending list first: an action running inside Tick/Notify may Start a
        // new flow, and appending to the live list mid-iteration would invalidate references. Pending
        // flows are merged in at the next safe point (top of Tick/Notify).
        m_pending.emplace_back();
        Flow& f = m_pending.back();
        f.m_id = ++m_nextId; f.m_owner = owner; f.m_group = group;
        return f;
    }

    // Advance all time-based waits and run ready actions. Call once per frame with the frame dt.
    void Tick(float dt) {
        MergePending();
        for (std::size_t i = 0; i < m_flows.size(); ++i) {
            Flow& f = m_flows[i];
            if (f.Done()) continue;
            if (m_ownerAlive && f.m_owner.entity != ecs::kNull && !m_ownerAlive(f.m_owner.entity)) {
                f.m_cancelled = true;   // owner died -> auto-cancel (documented policy, Phase 12)
                continue;
            }
            Advance(f, dt);
        }
        Reap();
    }

    // Resolve WaitForEvent flows without polling: the event bus calls this when an event fires.
    void NotifyEvent(ScriptEventId id) {
        MergePending();
        for (std::size_t i = 0; i < m_flows.size(); ++i) {
            Flow& f = m_flows[i];
            if (f.Done() || f.m_cursor >= f.m_steps.size()) continue;
            Flow::Step& s = f.m_steps[f.m_cursor];
            if (s.kind == Flow::Step::Kind::WaitForEvent && s.eventId == id) {
                s.eventFired = true;
                Advance(f, 0.0f);   // progress immediately past the satisfied wait
            }
        }
        Reap();
    }

    // Advance WaitForFixedSteps flows by one fixed step. Call from the fixed-step loop.
    void NotifyFixedStep() {
        MergePending();
        for (std::size_t i = 0; i < m_flows.size(); ++i) {
            Flow& f = m_flows[i];
            if (f.Done() || f.m_cursor >= f.m_steps.size()) continue;
            Flow::Step& s = f.m_steps[f.m_cursor];
            if (s.kind == Flow::Step::Kind::WaitForFixedSteps && s.intArg > 0) {
                if (--s.intArg == 0) Advance(f, 0.0f);
            }
        }
        Reap();
    }

    // Cancellation (Phase 12). Cancelled flows never run another step; their std::function actions are
    // destroyed at Reap, so nothing can call back into a dead script.
    bool Cancel(SequenceId id) {
        for (auto* v : {&m_flows, &m_pending})
            for (Flow& f : *v) if (f.m_id == id && !f.m_cancelled) { f.m_cancelled = true; return true; }
        return false;
    }
    int CancelGroup(const std::string& group) {
        if (group.empty()) return 0;
        int n = 0; for (auto* v : {&m_flows, &m_pending})
            for (Flow& f : *v) if (!f.m_cancelled && f.m_group == group) { f.m_cancelled = true; ++n; }
        return n;
    }
    int CancelOwned(const ScriptHandle& owner) { return CancelForEntity(owner.entity); }
    int CancelForEntity(ecs::Entity owner) {
        if (owner == ecs::kNull) return 0;
        int n = 0; for (auto* v : {&m_flows, &m_pending})
            for (Flow& f : *v) if (!f.m_cancelled && f.m_owner.entity == owner) { f.m_cancelled = true; ++n; }
        return n;
    }
    void CancelAll() { for (auto* v : {&m_flows, &m_pending}) for (Flow& f : *v) f.m_cancelled = true; }

    std::size_t ActiveCount() const {
        std::size_t n = 0;
        for (const auto* v : {&m_flows, &m_pending}) for (const Flow& f : *v) if (!f.Done()) ++n;
        return n;
    }
    bool IsActive(SequenceId id) const {
        for (const auto* v : {&m_flows, &m_pending}) for (const Flow& f : *v) if (f.m_id == id) return !f.Done();
        return false;
    }

private:
    void Advance(Flow& f, float dt) {
        float remaining = dt < 0 ? 0 : dt;
        while (!f.Done()) {
            Flow::Step& s = f.m_steps[f.m_cursor];
            switch (s.kind) {
                case Flow::Step::Kind::Action:
                    if (s.action) s.action();
                    ++f.m_cursor; continue;
                case Flow::Step::Kind::Delay: {
                    const float left = s.seconds - s.timer;
                    if (remaining < left) { s.timer += remaining; return; }
                    remaining -= (left > 0 ? left : 0); s.timer = 0; ++f.m_cursor; continue;
                }
                case Flow::Step::Kind::WaitUntil:
                    if (s.condition && !s.condition()) return;
                    ++f.m_cursor; continue;
                case Flow::Step::Kind::WaitForEvent:
                    if (!s.eventFired) return;
                    ++f.m_cursor; continue;
                case Flow::Step::Kind::WaitForFixedSteps:
                    if (s.intArg > 0) return;
                    ++f.m_cursor; continue;
                case Flow::Step::Kind::Repeat: {
                    if (s.intArg == 0) { ++f.m_cursor; continue; }   // count exhausted
                    s.timer += remaining; remaining = 0;
                    while (s.timer >= s.seconds && s.intArg != 0) {
                        if (s.action) s.action();
                        s.timer -= s.seconds;
                        if (s.intArg > 0) --s.intArg;                // negative == forever
                    }
                    if (s.intArg == 0) { ++f.m_cursor; continue; }
                    return;   // still repeating; wait for more time
                }
            }
        }
    }
    void Reap() {
        for (std::size_t i = 0; i < m_flows.size();) {
            if (m_flows[i].Done()) { m_flows[i] = std::move(m_flows.back()); m_flows.pop_back(); }
            else ++i;
        }
    }
    void MergePending() {
        if (m_pending.empty()) return;
        for (Flow& f : m_pending) m_flows.push_back(std::move(f));
        m_pending.clear();
    }

    std::vector<Flow> m_flows;
    std::vector<Flow> m_pending;   // started this frame; merged at the next safe point
    SequenceId        m_nextId = 0;
    std::function<bool(ecs::Entity)> m_ownerAlive;
};

} // namespace script
} // namespace engine
