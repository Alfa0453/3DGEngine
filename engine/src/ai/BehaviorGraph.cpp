#include "engine/ai/BehaviorGraph.h"

#include "engine/ai/AStar.h"
#include "engine/ai/BtScript.h"
#include "engine/ai/NavGrid.h"
#include "engine/ai/NavMesh.h"
#include "engine/ecs/Registry.h"
#include "engine/gameplay/GameplayComponents.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <unordered_set>

namespace engine {
namespace ai {

namespace {
float HorizontalDistance(const glm::vec3& a, const glm::vec3& b) {
    return glm::length(glm::vec2(a.x - b.x, a.z - b.z));
}
} // namespace

// ------------------------------- vocabulary ---------------------------------

const char* BtNodeTypeName(BtNodeType type) {
    switch (type) {
        case BtNodeType::Sequence:     return "Sequence";
        case BtNodeType::Selector:     return "Selector";
        case BtNodeType::Inverter:     return "Inverter";
        case BtNodeType::Succeeder:    return "Succeeder";
        case BtNodeType::Repeat:       return "Repeat";
        case BtNodeType::SeesTarget:   return "See Target?";
        case BtNodeType::TargetWithin: return "Target Within?";
        case BtNodeType::Chase:        return "Chase";
        case BtNodeType::Patrol:       return "Patrol";
        case BtNodeType::MoveToTarget: return "Move To Target";
        case BtNodeType::Wait:         return "Wait";
        case BtNodeType::Idle:         return "Idle";
        case BtNodeType::Repath:       return "Repath";
        case BtNodeType::ScriptTask:      return "Script Task";
        case BtNodeType::ScriptDecorator: return "Script Decorator";
        case BtNodeType::ScriptService:   return "Script Service";
        case BtNodeType::BbSetBool:       return "Set Bool";
        case BtNodeType::BbSetFloat:      return "Set Float";
        case BtNodeType::BbCheckBool:     return "Check Bool?";
        case BtNodeType::BbCheckFloat:    return "Float >= ?";
        case BtNodeType::Flee:            return "Flee";
        case BtNodeType::Wander:          return "Wander";
        case BtNodeType::Cooldown:        return "Cooldown";
        case BtNodeType::TimeLimit:       return "Time Limit";
        case BtNodeType::RandomChance:    return "Random Chance";
        case BtNodeType::BbFloatBelow:    return "Float < ?";
        case BtNodeType::Subtree:         return "Subtree";
        case BtNodeType::HealthBelow:     return "Health Below?";
        case BtNodeType::TargetDead:      return "Target Dead?";
        case BtNodeType::Attack:          return "Attack";
        case BtNodeType::FocusTarget:     return "Focus Target";
        case BtNodeType::ClearFocus:      return "Clear Focus";
        case BtNodeType::Count:        break;
    }
    return "?";
}

bool BtNodeTypeIsComposite(BtNodeType t) {
    return t == BtNodeType::Sequence || t == BtNodeType::Selector;
}
bool BtNodeTypeIsDecorator(BtNodeType t) {
    return t == BtNodeType::Inverter || t == BtNodeType::Succeeder || t == BtNodeType::Repeat;
}
bool BtNodeTypeIsLeaf(BtNodeType t) {
    return !BtNodeTypeIsComposite(t) && !BtNodeTypeIsDecorator(t);
}
bool BtNodeTypeUsesParam(BtNodeType t) {
    return t == BtNodeType::Repeat || t == BtNodeType::TargetWithin ||
           t == BtNodeType::Wait   || t == BtNodeType::Repath ||
           t == BtNodeType::ScriptService ||
           t == BtNodeType::BbSetBool  || t == BtNodeType::BbSetFloat ||
           t == BtNodeType::BbCheckBool || t == BtNodeType::BbCheckFloat ||
           t == BtNodeType::Cooldown   || t == BtNodeType::TimeLimit ||
           t == BtNodeType::RandomChance || t == BtNodeType::BbFloatBelow ||
           t == BtNodeType::HealthBelow || t == BtNodeType::Attack;
}
const char* BtNodeTypeParamLabel(BtNodeType t) {
    switch (t) {
        case BtNodeType::Repeat:        return "Times (<1 = forever)";
        case BtNodeType::TargetWithin:  return "Range (m)";
        case BtNodeType::Wait:          return "Seconds";
        case BtNodeType::Repath:        return "Interval (s)";
        case BtNodeType::ScriptService: return "Interval (s)";
        case BtNodeType::BbSetBool:     return "Value (0/1)";
        case BtNodeType::BbCheckBool:   return "Equals (0/1)";
        case BtNodeType::BbSetFloat:    return "Value";
        case BtNodeType::BbCheckFloat:  return "At least";
        case BtNodeType::BbFloatBelow:  return "Below";
        case BtNodeType::Cooldown:      return "Seconds";
        case BtNodeType::TimeLimit:     return "Seconds";
        case BtNodeType::RandomChance:  return "Chance (0..1)";
        case BtNodeType::HealthBelow:   return "HP fraction (0..1)";
        case BtNodeType::Attack:        return "Damage";
        default:                        return "";
    }
}

bool BtNodeTypeUsesScript(BtNodeType t) {
    return t == BtNodeType::ScriptTask || t == BtNodeType::ScriptDecorator ||
           t == BtNodeType::ScriptService;
}

bool BtNodeTypeUsesKey(BtNodeType t) {
    return t == BtNodeType::BbSetBool  || t == BtNodeType::BbSetFloat ||
           t == BtNodeType::BbCheckBool || t == BtNodeType::BbCheckFloat ||
           t == BtNodeType::BbFloatBelow;
}

bool BtNodeTypeIsSubtree(BtNodeType t) {
    return t == BtNodeType::Subtree;
}

const char* BlackboardTypeName(BlackboardEntry::Type t) {
    switch (t) {
        case BlackboardEntry::Type::Bool:   return "Bool";
        case BlackboardEntry::Type::Int:    return "Int";
        case BlackboardEntry::Type::Float:  return "Float";
        case BlackboardEntry::Type::Vec3:   return "Vec3";
        case BlackboardEntry::Type::String: return "String";
    }
    return "?";
}

void SeedBlackboard(const std::vector<BlackboardEntry>& entries, Blackboard& out) {
    for (const BlackboardEntry& e : entries) {
        if (e.key.empty()) continue;
        switch (e.type) {
            case BlackboardEntry::Type::Bool:   out.SetBool(e.key, e.b); break;
            case BlackboardEntry::Type::Int:    out.SetInt(e.key, e.i); break;
            case BlackboardEntry::Type::Float:  out.SetFloat(e.key, e.f); break;
            case BlackboardEntry::Type::Vec3:   out.SetVec3(e.key, e.v); break;
            case BlackboardEntry::Type::String: out.SetString(e.key, e.s); break;
        }
    }
}

// ------------------------------- blackboard ---------------------------------

std::vector<glm::vec3> AgentContext::Plan(const glm::vec3& from, const glm::vec3& to) const {
    if (mesh && mesh->PolyCount() > 0) {
        std::vector<glm::vec3> p;
        mesh->FindPath(from, to, p);
        return p;
    }
    if (grid && grid->width > 0 && grid->height > 0) {
        return AStar::FindPathWorld(*grid, from, to);
    }
    return {};
}

// -------------------------------- graph edit --------------------------------

int BehaviorGraph::AddNode(BtNodeType type, const glm::vec2& canvasPos) {
    BtGraphNode node;
    node.type = type;
    node.canvasPos = canvasPos;
    nodes.push_back(node);
    const int index = static_cast<int>(nodes.size()) - 1;
    if (root < 0) {
        root = index;
    }
    return index;
}

void BehaviorGraph::RemoveNode(int index) {
    if (index < 0 || index >= static_cast<int>(nodes.size())) {
        return;
    }
    nodes.erase(nodes.begin() + index);
    // Re-index every reference: drop links to the removed node, shift higher ones down.
    auto fix = [index](int& ref) {
        if (ref == index)      ref = -1;
        else if (ref > index)  --ref;
    };
    fix(root);
    for (BtGraphNode& n : nodes) {
        for (int& c : n.children) {
            fix(c);
        }
        n.children.erase(std::remove(n.children.begin(), n.children.end(), -1), n.children.end());
    }
}

bool BehaviorGraph::IsValid() const {
    if (root < 0 || root >= static_cast<int>(nodes.size())) {
        return false;
    }
    // Depth-first walk guarding against cycles / out-of-range children.
    std::unordered_set<int> onStack;
    std::function<bool(int)> visit = [&](int i) -> bool {
        if (i < 0 || i >= static_cast<int>(nodes.size())) return false;
        if (onStack.count(i)) return false;   // cycle
        onStack.insert(i);
        for (int c : nodes[static_cast<std::size_t>(i)].children) {
            if (!visit(c)) return false;
        }
        onStack.erase(i);
        return true;
    };
    return visit(root);
}

// ------------------------------- interpreter --------------------------------

namespace {

using Ptr = std::unique_ptr<BtNode<AgentContext>>;

// A leaf that always fails -- the safe fallback for broken/empty slots.
Ptr FailNode() {
    return Bt<AgentContext>::Condition([](AgentContext&) { return false; });
}

Ptr MakeBlackboardNode(BtNodeType type, const std::string& key, float param);   // defined below

// Records a node's per-tick status into c.nodeStatus[index] for the live debugger,
// then passes the child's result through unchanged.
class ProbeNode : public BtNode<AgentContext> {
public:
    ProbeNode(int index, Ptr child) : m_index(index), m_child(std::move(child)) {}
    BtStatus Tick(AgentContext& c, float dt) override {
        const BtStatus s = m_child ? m_child->Tick(c, dt) : BtStatus::Failure;
        if (m_index >= 0 && static_cast<std::size_t>(m_index) < c.nodeStatus.size()) {
            c.nodeStatus[static_cast<std::size_t>(m_index)] =
                (s == BtStatus::Running) ? 1 : (s == BtStatus::Success) ? 2 : 3;
        }
        return s;
    }
    void Reset() override { if (m_child) m_child->Reset(); }
private:
    int m_index = -1;
    Ptr m_child;
};

// --- script-backed runtime nodes (bind a BtScript instance) ---

// Leaf: drives a user BtScript's Tick() as a task, with OnEnter/OnExit bracketing.
class ScriptTaskNode : public BtNode<AgentContext> {
public:
    explicit ScriptTaskNode(std::unique_ptr<BtScript> s) : m_script(std::move(s)) {}
    BtStatus Tick(AgentContext& c, float dt) override {
        if (!m_script) return BtStatus::Failure;
        if (!m_entered) { m_script->OnEnter(c); m_entered = true; }
        const BtStatus s = m_script->Tick(c, dt);
        if (s != BtStatus::Running) { m_script->OnExit(c); m_entered = false; }
        return s;
    }
    void Reset() override { m_entered = false; }
private:
    std::unique_ptr<BtScript> m_script;
    bool m_entered = false;
};

// Decorator: gates 'child' on a user BtScript's Check().
class ScriptGateNode : public BtNode<AgentContext> {
public:
    ScriptGateNode(std::unique_ptr<BtScript> s, Ptr child)
        : m_script(std::move(s)), m_child(std::move(child)) {}
    BtStatus Tick(AgentContext& c, float dt) override {
        if (m_script && !m_script->Check(c)) return BtStatus::Failure;
        return m_child ? m_child->Tick(c, dt) : BtStatus::Failure;
    }
    void Reset() override { if (m_child) m_child->Reset(); }
private:
    std::unique_ptr<BtScript> m_script;
    Ptr m_child;
};

// Service: runs a user BtScript's Tick() (throttled), then delegates to 'child'.
class ScriptServiceNode : public BtNode<AgentContext> {
public:
    ScriptServiceNode(std::unique_ptr<BtScript> s, float interval, Ptr child)
        : m_script(std::move(s)), m_interval(interval), m_child(std::move(child)) {}
    BtStatus Tick(AgentContext& c, float dt) override {
        m_timer += c.dt;
        if (m_interval <= 0.0f || m_timer >= m_interval) {
            m_timer = 0.0f;
            if (m_script) m_script->Tick(c, dt);
        }
        return m_child ? m_child->Tick(c, dt) : BtStatus::Failure;
    }
    void Reset() override { m_timer = 0.0f; if (m_child) m_child->Reset(); }
private:
    std::unique_ptr<BtScript> m_script;
    float m_interval = 0.0f;
    Ptr   m_child;
    float m_timer = 0.0f;
};

// Decorator: block the child for 'cooldown' seconds after it succeeds.
class CooldownNode : public BtNode<AgentContext> {
public:
    CooldownNode(float cooldown, Ptr child) : m_cooldown(cooldown), m_child(std::move(child)) {}
    BtStatus Tick(AgentContext& c, float dt) override {
        if (m_cooling) {
            m_timer += c.dt;
            if (m_timer < m_cooldown) return BtStatus::Failure;
            m_cooling = false;
        }
        const BtStatus s = m_child ? m_child->Tick(c, dt) : BtStatus::Failure;
        if (s == BtStatus::Success) { m_cooling = true; m_timer = 0.0f; }
        return s;
    }
    void Reset() override { if (m_child) m_child->Reset(); }
private:
    float m_cooldown = 0.0f, m_timer = 0.0f; bool m_cooling = false; Ptr m_child;
};

// Decorator: fail the child if it stays Running longer than 'limit' seconds.
class TimeLimitNode : public BtNode<AgentContext> {
public:
    TimeLimitNode(float limit, Ptr child) : m_limit(limit), m_child(std::move(child)) {}
    BtStatus Tick(AgentContext& c, float dt) override {
        const BtStatus s = m_child ? m_child->Tick(c, dt) : BtStatus::Failure;
        if (s == BtStatus::Running) {
            m_elapsed += c.dt;
            if (m_limit > 0.0f && m_elapsed > m_limit) {
                if (m_child) m_child->Reset();
                m_elapsed = 0.0f;
                return BtStatus::Failure;
            }
        } else {
            m_elapsed = 0.0f;
        }
        return s;
    }
    void Reset() override { m_elapsed = 0.0f; if (m_child) m_child->Reset(); }
private:
    float m_limit = 0.0f, m_elapsed = 0.0f; Ptr m_child;
};

// Decorator: enter the child with probability 'chance' (rolled once per activation,
// latched while the child keeps Running).
class RandomChanceNode : public BtNode<AgentContext> {
public:
    RandomChanceNode(float chance, Ptr child) : m_chance(chance), m_child(std::move(child)) {}
    BtStatus Tick(AgentContext& c, float dt) override {
        if (!m_running) {
            const float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
            if (r > m_chance) return BtStatus::Failure;
        }
        const BtStatus s = m_child ? m_child->Tick(c, dt) : BtStatus::Failure;
        m_running = (s == BtStatus::Running);
        return s;
    }
    void Reset() override { m_running = false; if (m_child) m_child->Reset(); }
private:
    float m_chance = 1.0f; bool m_running = false; Ptr m_child;
};

// A service wrapper: runs a background side-effect (throttled by 'interval') each
// tick, then delegates to and returns the wrapped child's status.
class ServiceNode : public BtNode<AgentContext> {
public:
    ServiceNode(std::function<void(AgentContext&)> fn, float interval, Ptr child)
        : m_fn(std::move(fn)), m_interval(interval), m_child(std::move(child)) {}
    BtStatus Tick(AgentContext& c, float dt) override {
        m_timer += c.dt;
        if (m_interval <= 0.0f || m_timer >= m_interval) {
            m_timer = 0.0f;
            if (m_fn) m_fn(c);
        }
        return m_child ? m_child->Tick(c, dt) : BtStatus::Failure;
    }
    void Reset() override { m_timer = 0.0f; if (m_child) m_child->Reset(); }
private:
    std::function<void(AgentContext&)> m_fn;
    float m_interval = 0.0f;
    Ptr   m_child;
    float m_timer = 0.0f;
};

// Wrap 'child' with an attached decorator (a gate or result-modifier).
Ptr WrapDecorator(const BtAttachment& d, Ptr child) {
    switch (d.type) {
        case BtNodeType::SeesTarget: {
            std::vector<Ptr> v;
            v.push_back(Bt<AgentContext>::Condition([](AgentContext& c) { return c.seesTarget; }));
            v.push_back(std::move(child));
            return std::make_unique<detail::Sequence<AgentContext>>(std::move(v));  // gate: cond then child
        }
        case BtNodeType::TargetWithin: {
            const float r = d.param;
            std::vector<Ptr> v;
            v.push_back(Bt<AgentContext>::Condition([r](AgentContext& c) {
                return HorizontalDistance(c.targetPos, c.agent.position) <= r;
            }));
            v.push_back(std::move(child));
            return std::make_unique<detail::Sequence<AgentContext>>(std::move(v));
        }
        case BtNodeType::Inverter:  return Bt<AgentContext>::Inverter(std::move(child));
        case BtNodeType::Succeeder: return Bt<AgentContext>::Succeeder(std::move(child));
        case BtNodeType::Repeat:
            return Bt<AgentContext>::Repeat(std::move(child), (d.param < 1.0f) ? -1 : static_cast<int>(d.param));
        case BtNodeType::ScriptDecorator:
            return std::make_unique<ScriptGateNode>(BtScriptRegistry::Instance().Create(d.script),
                                                    std::move(child));
        case BtNodeType::BbCheckBool:
        case BtNodeType::BbCheckFloat:
        case BtNodeType::BbFloatBelow: {
            std::vector<Ptr> v;
            v.push_back(MakeBlackboardNode(d.type, d.key, d.param));   // condition gate
            v.push_back(std::move(child));
            return std::make_unique<detail::Sequence<AgentContext>>(std::move(v));
        }
        case BtNodeType::Cooldown:     return std::make_unique<CooldownNode>(d.param, std::move(child));
        case BtNodeType::TimeLimit:    return std::make_unique<TimeLimitNode>(d.param, std::move(child));
        case BtNodeType::RandomChance: return std::make_unique<RandomChanceNode>(d.param, std::move(child));
        default: return child;
    }
}

// Wrap 'child' with an attached service (a background tick).
Ptr WrapService(const BtAttachment& s, Ptr child) {
    switch (s.type) {
        case BtNodeType::Repath:
            return std::make_unique<ServiceNode>(
                [](AgentContext& c) { c.path = c.Plan(c.agent.position, c.targetPos); c.pathIndex = 0; },
                s.param, std::move(child));
        case BtNodeType::ScriptService:
            return std::make_unique<ScriptServiceNode>(BtScriptRegistry::Instance().Create(s.script),
                                                       s.param, std::move(child));
        default: return child;
    }
}

// Apply a node's services (innermost) then decorators (outermost) around 'base'.
Ptr ApplyAttachments(const BtGraphNode& node, Ptr base) {
    for (auto it = node.services.rbegin(); it != node.services.rend(); ++it) {
        base = WrapService(*it, std::move(base));
    }
    for (auto it = node.decorators.rbegin(); it != node.decorators.rend(); ++it) {
        base = WrapDecorator(*it, std::move(base));
    }
    return base;
}

// Build a no-code blackboard node: set/check a key against 'param'.
Ptr MakeBlackboardNode(BtNodeType type, const std::string& key, float param) {
    switch (type) {
        case BtNodeType::BbSetBool:
            return Bt<AgentContext>::Action([key, param](AgentContext& c, float) {
                c.blackboard.SetBool(key, param != 0.0f);
                return BtStatus::Success;
            });
        case BtNodeType::BbSetFloat:
            return Bt<AgentContext>::Action([key, param](AgentContext& c, float) {
                c.blackboard.SetFloat(key, param);
                return BtStatus::Success;
            });
        case BtNodeType::BbCheckBool:
            return Bt<AgentContext>::Condition([key, param](AgentContext& c) {
                return c.blackboard.GetBool(key, false) == (param != 0.0f);
            });
        case BtNodeType::BbCheckFloat:
            return Bt<AgentContext>::Condition([key, param](AgentContext& c) {
                return c.blackboard.GetFloat(key, 0.0f) >= param;
            });
        case BtNodeType::BbFloatBelow:
            return Bt<AgentContext>::Condition([key, param](AgentContext& c) {
                return c.blackboard.GetFloat(key, 0.0f) < param;
            });
        default:
            return FailNode();
    }
}

Ptr MakeAction(BtNodeType type, float param) {
    switch (type) {
        case BtNodeType::SeesTarget:
            return Bt<AgentContext>::Condition([](AgentContext& c) { return c.seesTarget; });

        case BtNodeType::TargetWithin:
            return Bt<AgentContext>::Condition([param](AgentContext& c) {
                return HorizontalDistance(c.targetPos, c.agent.position) <= param;
            });

        case BtNodeType::Chase:
            return Bt<AgentContext>::Action([](AgentContext& c, float) {
                const float targetDistance = HorizontalDistance(
                    c.targetPos, c.agent.position);
                if (targetDistance <= c.reachRadius) {
                    c.steer = glm::vec3(0.0f);
                    c.agent.velocity = glm::vec3(0.0f);
                    c.path.clear();
                    c.pathIndex = 0;
                    c.pathGoalValid = false;
                    return BtStatus::Success;
                }
                c.repathTimer += c.dt;
                const float goalRefreshDistance = std::max(0.15f, c.reachRadius * 0.25f);
                const bool targetMoved = !c.pathGoalValid
                    || HorizontalDistance(c.targetPos, c.pathGoal) > goalRefreshDistance;
                if (c.path.empty() || targetMoved || c.repathTimer > c.repathInterval) {
                    c.path = c.Plan(c.agent.position, c.targetPos);
                    c.pathIndex = 0;
                    c.pathGoal = c.targetPos;
                    c.pathGoalValid = true;
                    c.repathTimer = 0.0f;
                }
                if (c.seesTarget && glm::length(c.targetPos - c.agent.position) < c.chargeRadius) {
                    c.steer = Seek(c.agent, c.targetPos);
                } else {
                    c.steer = FollowPath(c.agent, c.path, c.pathIndex, c.reachRadius);
                }
                return BtStatus::Running;
            });

        case BtNodeType::MoveToTarget:
            return Bt<AgentContext>::Action([](AgentContext& c, float) {
                if (HorizontalDistance(c.targetPos, c.agent.position)
                    <= c.reachRadius) {
                    c.steer = glm::vec3(0.0f);
                    c.agent.velocity = glm::vec3(0.0f);
                    c.path.clear();
                    c.pathIndex = 0;
                    c.pathGoalValid = false;
                    return BtStatus::Success;
                }
                const float goalRefreshDistance = std::max(0.15f, c.reachRadius * 0.25f);
                const bool targetMoved = !c.pathGoalValid
                    || HorizontalDistance(c.targetPos, c.pathGoal) > goalRefreshDistance;
                if (c.path.empty() || targetMoved) {
                    c.path = c.Plan(c.agent.position, c.targetPos);
                    c.pathIndex = 0;
                    c.pathGoal = c.targetPos;
                    c.pathGoalValid = true;
                }
                c.steer = FollowPath(c.agent, c.path, c.pathIndex, c.reachRadius);
                return BtStatus::Running;
            });

        case BtNodeType::Patrol:
            return Bt<AgentContext>::Action([](AgentContext& c, float) {
                if (c.patrol.empty()) return BtStatus::Failure;
                c.steer = FollowPath(c.agent, c.patrol, c.patrolIndex, c.reachRadius, 1.5f);
                if (c.patrolIndex >= c.patrol.size()) c.patrolIndex = 0;   // loop
                return BtStatus::Running;
            });

        case BtNodeType::Wait: {
            auto elapsed = std::make_shared<float>(0.0f);
            return Bt<AgentContext>::Action([elapsed, param](AgentContext& c, float) {
                *elapsed += c.dt;
                c.steer = glm::vec3(0.0f);
                if (*elapsed >= param) { *elapsed = 0.0f; return BtStatus::Success; }
                return BtStatus::Running;
            });
        }

        case BtNodeType::Idle:
            return Bt<AgentContext>::Action([](AgentContext& c, float) {
                c.steer = glm::vec3(0.0f);
                return BtStatus::Running;
            });

        case BtNodeType::Flee:
            return Bt<AgentContext>::Action([](AgentContext& c, float) {
                c.steer = Flee(c.agent, c.targetPos);
                return BtStatus::Running;
            });

        case BtNodeType::Wander:
            return Bt<AgentContext>::Action([](AgentContext& c, float) {
                c.steer = Wander(c.agent, 0.5f);
                return BtStatus::Running;
            });

        case BtNodeType::HealthBelow:
            return Bt<AgentContext>::Condition([param](AgentContext& c) {
                const Health* h = c.registry ? c.registry->TryGet<Health>(c.self) : nullptr;
                return h && h->maxHp > 0.0f && (h->hp / h->maxHp) < param;
            });

        case BtNodeType::TargetDead:
            return Bt<AgentContext>::Condition([](AgentContext& c) {
                const Health* th = (c.registry && c.targetEntity != ecs::kNull)
                                       ? c.registry->TryGet<Health>(c.targetEntity) : nullptr;
                return th && !th->alive;
            });

        case BtNodeType::Attack:
            return Bt<AgentContext>::Action([param](AgentContext& c, float) {
                if (!c.registry || c.targetEntity == ecs::kNull) return BtStatus::Failure;
                Health* th = c.registry->TryGet<Health>(c.targetEntity);
                if (th && th->alive
                    && HorizontalDistance(c.targetPos, c.agent.position) <= c.reachRadius) {
                    th->Damage(param);
                    return BtStatus::Success;
                }
                return BtStatus::Failure;
            });

        case BtNodeType::FocusTarget:
            return Bt<AgentContext>::Action([](AgentContext& c, float) {
                if (!c.seesTarget || c.targetEntity == ecs::kNull) {
                    return BtStatus::Failure;
                }
                glm::vec3 direction = c.targetPos - c.agent.position;
                direction.y = 0.0f;
                if (glm::dot(direction, direction) <= 1.0e-6f) {
                    return BtStatus::Failure;
                }
                c.focusTarget = true;
                c.facing = glm::normalize(direction);
                return BtStatus::Success;
            });

        case BtNodeType::ClearFocus:
            return Bt<AgentContext>::Action([](AgentContext& c, float) {
                c.focusTarget = false;
                return BtStatus::Success;
            });

        default:
            return FailNode();
    }
}

} // namespace

BehaviorTree<AgentContext> BuildBehaviorTree(const BehaviorGraph& graph,
                                             const SubtreeResolver& resolveSubtree) {
    if (!graph.IsValid()) {
        return BehaviorTree<AgentContext>(FailNode());
    }

    // Recurses over an arbitrary graph 'g' so a Subtree node can switch into the
    // graph it references. 'depth' caps both graph depth and subtree nesting.
    std::function<Ptr(const BehaviorGraph&, int, int)> build =
        [&](const BehaviorGraph& g, int index, int depth) -> Ptr {
        if (depth > 256 || index < 0 || index >= static_cast<int>(g.nodes.size())) {
            return FailNode();
        }
        const BtGraphNode& node = g.nodes[static_cast<std::size_t>(index)];

        Ptr base;
        if (BtNodeTypeIsComposite(node.type)) {
            std::vector<Ptr> kids;
            kids.reserve(node.children.size());
            for (int c : node.children) {
                kids.push_back(build(g, c, depth + 1));
            }
            if (kids.empty()) {
                base = FailNode();
            } else if (node.type == BtNodeType::Sequence) {
                base = std::make_unique<detail::Sequence<AgentContext>>(std::move(kids));
            } else {
                base = std::make_unique<detail::Selector<AgentContext>>(std::move(kids));
            }
        } else if (BtNodeTypeIsDecorator(node.type)) {
            Ptr child = node.children.empty() ? FailNode() : build(g, node.children.front(), depth + 1);
            switch (node.type) {
                case BtNodeType::Inverter:  base = Bt<AgentContext>::Inverter(std::move(child)); break;
                case BtNodeType::Succeeder: base = Bt<AgentContext>::Succeeder(std::move(child)); break;
                case BtNodeType::Repeat: {
                    const int times = (node.param < 1.0f) ? -1 : static_cast<int>(node.param);
                    base = Bt<AgentContext>::Repeat(std::move(child), times);
                    break;
                }
                default: base = std::move(child); break;
            }
        } else if (BtNodeTypeIsSubtree(node.type)) {
            const BehaviorGraph* sub = resolveSubtree ? resolveSubtree(node.script) : nullptr;
            base = (sub && sub->IsValid()) ? build(*sub, sub->root, depth + 1) : FailNode();
        } else if (node.type == BtNodeType::ScriptTask) {
            base = std::make_unique<ScriptTaskNode>(BtScriptRegistry::Instance().Create(node.script));
        } else if (BtNodeTypeUsesKey(node.type)) {
            base = MakeBlackboardNode(node.type, node.key, node.param);   // blackboard leaf
        } else {
            base = MakeAction(node.type, node.param);   // leaf
        }

        Ptr wrapped = ApplyAttachments(node, std::move(base));   // attached decorators/services
        // Instrument only top-level graph nodes (their indices map to c.nodeStatus).
        if (&g == &graph) {
            return std::make_unique<ProbeNode>(index, std::move(wrapped));
        }
        return wrapped;
    };

    return BehaviorTree<AgentContext>(build(graph, graph.root, 0));
}

// ------------------------------ serialization -------------------------------
//
// Text format (whitespace-separated), version 1:
//   3DGBehaviorGraph 2
//   root <int>
//   nodes <count>
//   <typeInt> <param> <canvasX> <canvasY> <childCount> [<child> ...]
//       <decCount> [<typeInt> <param> ...] <svcCount> [<typeInt> <param> ...]   (v2+)

bool SaveBehaviorGraph(const std::string& path, const BehaviorGraph& graph, std::string* error) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        if (error) *error = "could not open '" + path + "' for writing.";
        return false;
    }
    auto tok = [](const std::string& s) -> std::string { return s.empty() ? "-" : s; };
    out << "3DGBehaviorGraph 5\n";
    out << "root " << graph.root << "\n";
    out << "nodes " << graph.nodes.size() << "\n";
    auto writeAttachment = [&](const BtAttachment& a) {
        out << ' ' << static_cast<int>(a.type) << ' ' << a.param << ' '
            << tok(a.script) << ' ' << tok(a.key);
    };
    for (const BtGraphNode& n : graph.nodes) {
        out << static_cast<int>(n.type) << ' ' << n.param << ' '
            << n.canvasPos.x << ' ' << n.canvasPos.y << ' '
            << n.children.size();
        for (int c : n.children) out << ' ' << c;
        out << ' ' << n.decorators.size();
        for (const BtAttachment& a : n.decorators) writeAttachment(a);
        out << ' ' << n.services.size();
        for (const BtAttachment& a : n.services) writeAttachment(a);
        out << ' ' << tok(n.script) << ' ' << tok(n.key);
        out << '\n';
    }
    // Blackboard schema (v4+): count, then one line per entry.
    out << "blackboard " << graph.blackboard.size() << "\n";
    for (const BlackboardEntry& e : graph.blackboard) {
        out << tok(e.key) << ' ' << static_cast<int>(e.type) << ' '
            << (e.b ? 1 : 0) << ' ' << e.i << ' ' << e.f << ' '
            << e.v.x << ' ' << e.v.y << ' ' << e.v.z << ' ' << tok(e.s) << '\n';
    }
    if (!out) {
        if (error) *error = "write failed for '" + path + "'.";
        return false;
    }
    return true;
}

bool LoadBehaviorGraph(const std::string& path, BehaviorGraph& outGraph, std::string* error) {
    std::ifstream in(path);
    if (!in) {
        if (error) *error = "could not open '" + path + "'.";
        return false;
    }
    std::string magic;
    int version = 0;
    in >> magic >> version;
    if (magic != "3DGBehaviorGraph" || version < 1 || version > 5) {
        if (error) *error = "not a recognised behaviour-graph file.";
        return false;
    }

    BehaviorGraph graph;
    std::string key;
    std::size_t count = 0;
    in >> key >> graph.root;      // "root <int>"
    in >> key >> count;           // "nodes <count>"
    if (!in) {
        if (error) *error = "malformed header.";
        return false;
    }

    graph.nodes.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        BtGraphNode node;
        int typeInt = 0;
        std::size_t childCount = 0;
        in >> typeInt >> node.param >> node.canvasPos.x >> node.canvasPos.y >> childCount;
        if (!in) {
            if (error) *error = "truncated node list.";
            return false;
        }
        if (typeInt < 0 || typeInt >= static_cast<int>(BtNodeType::Count)) {
            typeInt = 0;   // unknown type -> Sequence, harmless
        }
        node.type = static_cast<BtNodeType>(typeInt);
        node.children.reserve(childCount);
        for (std::size_t c = 0; c < childCount; ++c) {
            int child = -1;
            in >> child;
            node.children.push_back(child);
        }
        if (version >= 2) {
            auto readAttachments = [&](std::vector<BtAttachment>& out) {
                std::size_t n = 0;
                in >> n;
                out.reserve(n);
                for (std::size_t a = 0; a < n; ++a) {
                    int typeInt = 0;
                    BtAttachment att;
                    in >> typeInt >> att.param;
                    if (version >= 3) {
                        std::string sc;
                        in >> sc;
                        if (sc != "-") att.script = sc;
                    }
                    if (version >= 5) {
                        std::string ky;
                        in >> ky;
                        if (ky != "-") att.key = ky;
                    }
                    if (typeInt < 0 || typeInt >= static_cast<int>(BtNodeType::Count)) typeInt = 0;
                    att.type = static_cast<BtNodeType>(typeInt);
                    out.push_back(att);
                }
            };
            readAttachments(node.decorators);
            readAttachments(node.services);
            if (version >= 3) {
                std::string sc;
                in >> sc;
                if (sc != "-") node.script = sc;
            }
            if (version >= 5) {
                std::string ky;
                in >> ky;
                if (ky != "-") node.key = ky;
            }
            if (!in) {
                if (error) *error = "truncated attachment list.";
                return false;
            }
        }
        graph.nodes.push_back(std::move(node));
    }

    if (version >= 4) {
        std::string bbLabel;
        std::size_t bbCount = 0;
        in >> bbLabel >> bbCount;   // "blackboard <count>"
        if (in && bbLabel == "blackboard") {
            graph.blackboard.reserve(bbCount);
            for (std::size_t e = 0; e < bbCount; ++e) {
                BlackboardEntry entry;
                int typeInt = 0, boolInt = 0;
                std::string k, s;
                in >> k >> typeInt >> boolInt >> entry.i >> entry.f
                   >> entry.v.x >> entry.v.y >> entry.v.z >> s;
                entry.key = (k == "-") ? "" : k;
                entry.s   = (s == "-") ? "" : s;
                entry.b   = boolInt != 0;
                if (typeInt < 0 || typeInt > static_cast<int>(BlackboardEntry::Type::String)) typeInt = 0;
                entry.type = static_cast<BlackboardEntry::Type>(typeInt);
                graph.blackboard.push_back(std::move(entry));
            }
        }
    }

    outGraph = std::move(graph);
    return true;
}

} // namespace ai
} // namespace engine
