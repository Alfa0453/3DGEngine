#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace engine {
namespace ai {

// A behaviour tree: a composable alternative to a state machine for agent logic.
// Every node ticks to Success, Failure, or Running; composites and decorators
// combine leaves (Action/Condition) into a tree. Ticking is reactive -- composites
// re-evaluate their children from the top each tick, so a higher-priority branch
// (e.g. "if you see the player, chase") preempts a lower one automatically.
enum class BtStatus { Success, Failure, Running };

// Completion rule for a Parallel composite: RequireAll succeeds only when every
// child has succeeded (fails the moment one fails); RequireOne succeeds as soon as
// any child succeeds (fails only when all have failed).
enum class ParallelPolicy { RequireAll, RequireOne };

template <class Ctx>
class BtNode {
public:
    virtual ~BtNode() = default;
    virtual BtStatus Tick(Ctx& c, float dt) = 0;
    virtual void      Reset() {}
};

namespace detail {

template <class Ctx>
class Action : public BtNode<Ctx> {
public:
    explicit Action(std::function<BtStatus(Ctx&, float)> fn) : m_fn(std::move(fn)) {}
    BtStatus Tick(Ctx& c, float dt) override { return m_fn(c, dt); }
private:
    std::function<BtStatus(Ctx&, float)> m_fn;
};

template <class Ctx>
class Condition : public BtNode<Ctx> {
public:
    explicit Condition(std::function<bool(Ctx&)> fn) : m_fn(std::move(fn)) {}
    BtStatus Tick(Ctx& c, float) override { return m_fn(c) ? BtStatus::Success : BtStatus::Failure; }
private:
    std::function<bool(Ctx&)> m_fn;
};

template <class Ctx>
class Sequence : public BtNode<Ctx> {    // succeed only if ALL children succeed, in order
public:
    explicit Sequence(std::vector<std::unique_ptr<BtNode<Ctx>>> kids) : m_kids(std::move(kids)) {}
    BtStatus Tick(Ctx& c, float dt) override {
        for (std::size_t i = 0; i < m_kids.size(); ++i) {
            const BtStatus s = m_kids[i]->Tick(c, dt);
            if (s != BtStatus::Success) {   // Failure or Running stops the sequence
                // If a later child was left Running last tick and we bailed earlier
                // this tick, that child was abandoned -- reset it so it starts clean.
                if (m_active >= 0 && m_active != static_cast<int>(i)) m_kids[m_active]->Reset();
                m_active = (s == BtStatus::Running) ? static_cast<int>(i) : -1;
                return s;
            }
        }
        if (m_active >= 0) { m_kids[m_active]->Reset(); m_active = -1; }
        return BtStatus::Success;
    }
    void Reset() override { for (auto& k : m_kids) k->Reset(); m_active = -1; }
private:
    std::vector<std::unique_ptr<BtNode<Ctx>>> m_kids;
    int m_active = -1;   // index of the child left Running last tick, or -1
};

template <class Ctx>
class Selector : public BtNode<Ctx> {   // succeed if ANY child succeeds (first that doesn't fail)
public:
    explicit Selector(std::vector<std::unique_ptr<BtNode<Ctx>>> kids) : m_kids(std::move(kids)) {}
    BtStatus Tick(Ctx& c, float dt) override {
        for (std::size_t i = 0; i < m_kids.size(); ++i) {
            const BtStatus s = m_kids[i]->Tick(c, dt);
            if (s != BtStatus::Failure) {   // Success or Running wins
                // A higher-priority child took over; if a different child was left
                // Running last tick it's now preempted -- reset it so it starts clean.
                if (m_active >= 0 && m_active != static_cast<int>(i)) m_kids[m_active]->Reset();
                m_active = (s == BtStatus::Running) ? static_cast<int>(i) : -1;
                return s;
            }
        }
        if (m_active >= 0) { m_kids[m_active]->Reset(); m_active = -1; }
        return BtStatus::Failure;   // every child failed
    }
    void Reset() override { for (auto& k : m_kids) k->Reset(); m_active = -1; }
private:
    std::vector<std::unique_ptr<BtNode<Ctx>>> m_kids;
    int m_active = -1;   // index of the child left Running last tick, or -1
};

template <class Ctx>
class Inverter : public BtNode<Ctx> {
public:
    explicit Inverter(std::unique_ptr<BtNode<Ctx>> ch) : m_ch(std::move(ch)) {}
    BtStatus Tick(Ctx& c, float dt) override {
        const BtStatus s = m_ch->Tick(c, dt);
        if (s == BtStatus::Running) return s;
        return (s == BtStatus::Success) ? BtStatus::Failure : BtStatus::Success;
    }
    void Reset() override { m_ch->Reset(); }
private:
    std::unique_ptr<BtNode<Ctx>> m_ch;
};

template <class Ctx>
class Succeeder : public BtNode<Ctx> {       // always Success once the child finishes
public:
    explicit Succeeder(std::unique_ptr<BtNode<Ctx>> ch) : m_ch(std::move(ch)) {}
    BtStatus Tick(Ctx& c, float dt) override {
        const BtStatus s = m_ch->Tick(c, dt);
        return (s == BtStatus::Running) ? BtStatus::Running : BtStatus::Success;
    }
    void Reset() override { m_ch->Reset(); }
private:
    std::unique_ptr<BtNode<Ctx>> m_ch;
};

template <class Ctx>
class Repeat : public BtNode<Ctx> {   // repeat the child N times (times < 0 = forever)
public:
    Repeat(std::unique_ptr<BtNode<Ctx>> ch, int times) : m_ch(std::move(ch)), m_times(times) {}
    BtStatus Tick(Ctx& c, float dt) override {
        const BtStatus s = m_ch->Tick(c, dt);
        if (s == BtStatus::Running) return BtStatus::Running;
        ++m_done; m_ch->Reset();
        if (m_times >= 0 && m_done >= m_times) { m_done = 0; return BtStatus::Success; }
        return BtStatus::Running;   // keep going next tick
    }
    void Reset() override { m_done = 0; m_ch->Reset(); }
private:
    std::unique_ptr<BtNode<Ctx>> m_ch;
    int m_times = 1;
    int m_done  = 0;
};

template <class Ctx>
class Failer : public BtNode<Ctx> {          // always Failure once the child finishes
public:
    explicit Failer(std::unique_ptr<BtNode<Ctx>> ch) : m_ch(std::move(ch)) {}
    BtStatus Tick(Ctx& c, float dt) override {
        const BtStatus s = m_ch->Tick(c, dt);
        return (s == BtStatus::Running) ? BtStatus::Running : BtStatus::Failure;
    }
    void Reset() override { m_ch->Reset(); }
private:
    std::unique_ptr<BtNode<Ctx>> m_ch;
};

template <class Ctx>
class Wait : public BtNode<Ctx> {   // Running until 'seconds' elapse, then Success
public:
    explicit Wait(float seconds) : m_seconds(seconds) {}
    BtStatus Tick(Ctx&, float dt) override {
        m_elapsed += dt;
        return (m_elapsed >= m_seconds) ? BtStatus::Success : BtStatus::Running;
    }
    void Reset() override { m_elapsed = 0.0f; }
private:
    float m_seconds = 0.0f;
    float m_elapsed = 0.0f;
};

template <class Ctx>
class Cooldown : public BtNode<Ctx> {   // gate: after the child succeeds, block for 'seconds'
public:
    Cooldown(std::unique_ptr<BtNode<Ctx>> ch, float seconds)
        : m_ch(std::move(ch)), m_seconds(seconds) {}
    BtStatus Tick(Ctx& c, float dt) override {
        if (m_cooling) {
            m_elapsed += dt;
            if (m_elapsed < m_seconds) return BtStatus::Failure;   // still cooling down
            m_cooling = false; m_elapsed = 0.0f;
        }
        const BtStatus s = m_ch->Tick(c, dt);
        if (s == BtStatus::Success) { m_cooling = true; m_elapsed = 0.0f; }
        return s;
    }
    void Reset() override { m_ch->Reset(); m_cooling = false; m_elapsed = 0.0f; }
private:
    std::unique_ptr<BtNode<Ctx>> m_ch;
    float m_seconds = 0.0f;
    float m_elapsed = 0.0f;
    bool  m_cooling = false;
};

template <class Ctx>
class Timeout : public BtNode<Ctx> {   // fail (and reset) the child if it runs too long
public:
    Timeout(std::unique_ptr<BtNode<Ctx>> ch, float seconds)
        : m_ch(std::move(ch)), m_seconds(seconds) {}
    BtStatus Tick(Ctx& c, float dt) override {
        m_elapsed += dt;
        if (m_elapsed >= m_seconds) { m_ch->Reset(); m_elapsed = 0.0f; return BtStatus::Failure; }
        const BtStatus s = m_ch->Tick(c, dt);
        if (s != BtStatus::Running) m_elapsed = 0.0f;   // finished in time
        return s;
    }
    void Reset() override { m_ch->Reset(); m_elapsed = 0.0f; }
private:
    std::unique_ptr<BtNode<Ctx>> m_ch;
    float m_seconds = 0.0f;
    float m_elapsed = 0.0f;
};

template <class Ctx>
class Retry : public BtNode<Ctx> {   // re-run a failing child up to N times (times < 0 = forever)
public:
    Retry(std::unique_ptr<BtNode<Ctx>> ch, int times) : m_ch(std::move(ch)), m_times(times) {}
    BtStatus Tick(Ctx& c, float dt) override {
        const BtStatus s = m_ch->Tick(c, dt);
        if (s == BtStatus::Running) return BtStatus::Running;
        if (s == BtStatus::Success) { m_done = 0; return BtStatus::Success; }
        ++m_done; m_ch->Reset();
        if (m_times >= 0 && m_done >= m_times) { m_done = 0; return BtStatus::Failure; }
        return BtStatus::Running;   // try again next tick
    }
    void Reset() override { m_done = 0; m_ch->Reset(); }
private:
    std::unique_ptr<BtNode<Ctx>> m_ch;
    int m_times = 1;
    int m_done  = 0;
};

template <class Ctx>
class Parallel : public BtNode<Ctx> {   // tick every child each tick; resolve by policy
public:
    Parallel(ParallelPolicy policy, std::vector<std::unique_ptr<BtNode<Ctx>>> kids)
        : m_kids(std::move(kids)), m_policy(policy) {}
    BtStatus Tick(Ctx& c, float dt) override {
        std::size_t succeeded = 0, failed = 0;
        for (auto& k : m_kids) {
            const BtStatus s = k->Tick(c, dt);
            if (s == BtStatus::Success) ++succeeded;
            else if (s == BtStatus::Failure) ++failed;
        }
        const std::size_t n = m_kids.size();
        BtStatus result = BtStatus::Running;
        if (m_policy == ParallelPolicy::RequireOne) {
            if (succeeded > 0)  result = BtStatus::Success;
            else if (failed == n) result = BtStatus::Failure;
        } else { // RequireAll
            if (failed > 0)     result = BtStatus::Failure;
            else if (succeeded == n) result = BtStatus::Success;
        }
        if (result != BtStatus::Running) Reset();   // start clean next time
        return result;
    }
    void Reset() override { for (auto& k : m_kids) k->Reset(); }
private:
    std::vector<std::unique_ptr<BtNode<Ctx>>> m_kids;
    ParallelPolicy m_policy = ParallelPolicy::RequireAll;
};

} // namespace detail

// Fluent factory: fix the context once (using B = Bt<MyCtx>;) then build the tree.
template <class Ctx>
struct Bt {
    using Ptr = std::unique_ptr<BtNode<Ctx>>;

    static Ptr Action(std::function<BtStatus(Ctx&, float)> fn) {
        return std::make_unique<detail::Action<Ctx>>(std::move(fn));
    }
    static Ptr Condition(std::function<bool(Ctx&)> fn) {
        return std::make_unique<detail::Condition<Ctx>>(std::move(fn));
    }
    template <class... N> static Ptr Sequence(N&&... kids) {
        std::vector<Ptr> v; (v.push_back(std::forward<N>(kids)), ...);
        return std::make_unique<detail::Sequence<Ctx>>(std::move(v));
    }
    template <class... N> static Ptr Selector(N&&... kids) {
        std::vector<Ptr> v; (v.push_back(std::forward<N>(kids)), ...);
        return std::make_unique<detail::Selector<Ctx>>(std::move(v));
    }
    static Ptr Inverter(Ptr ch)  { return std::make_unique<detail::Inverter<Ctx>>(std::move(ch)); }
    static Ptr Succeeder(Ptr ch) { return std::make_unique<detail::Succeeder<Ctx>>(std::move(ch)); }
    static Ptr Failer(Ptr ch)    { return std::make_unique<detail::Failer<Ctx>>(std::move(ch)); }
    static Ptr Repeat(Ptr ch, int times) { return std::make_unique<detail::Repeat<Ctx>>(std::move(ch), times); }
    static Ptr Retry(Ptr ch, int times)  { return std::make_unique<detail::Retry<Ctx>>(std::move(ch), times); }
    static Ptr Wait(float seconds)       { return std::make_unique<detail::Wait<Ctx>>(seconds); }
    static Ptr Cooldown(Ptr ch, float seconds) {
        return std::make_unique<detail::Cooldown<Ctx>>(std::move(ch), seconds);
    }
    static Ptr Timeout(Ptr ch, float seconds) {
        return std::make_unique<detail::Timeout<Ctx>>(std::move(ch), seconds);
    }
    template <class... N> static Ptr Parallel(ParallelPolicy policy, N&&... kids) {
        std::vector<Ptr> v; (v.push_back(std::forward<N>(kids)), ...);
        return std::make_unique<detail::Parallel<Ctx>>(policy, std::move(v));
    }
};

// Owns a root node; tick it each frame. Resets when a whole tree finishes so it
// re-evaluates from the top next time.
template <class Ctx>
class BehaviorTree {
public:
    explicit BehaviorTree(std::unique_ptr<BtNode<Ctx>> root = nullptr) : m_root(std::move(root)) {}
    void SetRoot(std::unique_ptr<BtNode<Ctx>> root) { m_root = std::move(root); }

    BtStatus Tick(Ctx& c, float dt) {
        if (!m_root) return BtStatus::Failure;
        const BtStatus s = m_root->Tick(c, dt);
        if (s != BtStatus::Running) m_root->Reset();
        return s;
    }
private:
    std::unique_ptr<BtNode<Ctx>> m_root;
};

} // namespace ai
} // namespace engine