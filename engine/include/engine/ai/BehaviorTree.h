#pragma once

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
        for (auto& k : m_kids) {
            const BtStatus s = k->Tick(c, dt);
            if (s != BtStatus::Success) return s;   // Failure or Running stops the sequence
        }
        return BtStatus::Success;
    }
    void Reset() override { for (auto& k : m_kids) k->Reset(); }
private:
    std::vector<std::unique_ptr<BtNode<Ctx>>> m_kids;
};

template <class Ctx>
class Selector : public BtNode<Ctx> {   // succeed if ANY child succeeds (first that doesn't fail)
public:
    explicit Selector(std::vector<std::unique_ptr<BtNode<Ctx>>> kids) : m_kids(std::move(kids)) {}
    BtStatus Tick(Ctx& c, float dt) override {
        for (auto& k : m_kids) {
            const BtStatus s = k->Tick(c, dt);
            if (s != BtStatus::Failure) return s;   // Success or Running wins
        }
    }
private:
    std::vector<std::unique_ptr<BtNode<Ctx>>> m_kids;
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
        std::vector<Ptr> v; v(v.push_back(std::forward<N>(kids)), ...);
        return std::make_unique<detail::Selector<Ctx>>(std::move(v));
    }
    static Ptr Inverter(Ptr ch)  { return std::make_unique<detail::Inverter<Ctx>>(std::move(ch)); }
    static Ptr Succeeder(Ptr ch) { return std::make_unique<detail::Succeeder<Ctx>>(std::move(ch)); }
    static Ptr Repeat(Ptr ch, int times) { return std::make_unique<detail::Repeat<Ctx>>(std::move(ch), times); }
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