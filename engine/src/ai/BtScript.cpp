#include "engine/ai/BtScript.h"

#include "engine/ai/BehaviorGraph.h"   // AgentContext

#include <algorithm>

namespace engine {
namespace ai {

BtScriptRegistry& BtScriptRegistry::Instance() {
    static BtScriptRegistry instance;
    return instance;
}

void BtScriptRegistry::Register(const std::string& name, Factory factory) {
    if (!name.empty() && factory) {
        m_factories[name] = std::move(factory);
    }
}

bool BtScriptRegistry::Has(const std::string& name) const {
    return m_factories.find(name) != m_factories.end();
}

std::unique_ptr<BtScript> BtScriptRegistry::Create(const std::string& name) const {
    const auto it = m_factories.find(name);
    return (it != m_factories.end() && it->second) ? it->second() : nullptr;
}

std::vector<std::string> BtScriptRegistry::Names() const {
    std::vector<std::string> names;
    names.reserve(m_factories.size());
    for (const auto& kv : m_factories) names.push_back(kv.first);
    std::sort(names.begin(), names.end());
    return names;
}

// ------------------------------ example scripts -----------------------------
namespace {

// TASK: circle-strafe the target (perpendicular steering) while it is visible.
class StrafeTarget : public BtScript {
public:
    BtStatus Tick(AgentContext& c, float) override {
        const glm::vec3 to = c.targetPos - c.agent.position;
        const glm::vec3 side(-to.z, 0.0f, to.x);   // 90 deg in XZ
        if (glm::dot(side, side) > 1e-6f) {
            c.steer = glm::normalize(side) * c.agent.maxForce;
        }
        return c.seesTarget ? BtStatus::Running : BtStatus::Failure;
    }
};

// SERVICE: turn to face the target every tick (does not move the agent).
class FaceTarget : public BtScript {
public:
    BtStatus Tick(AgentContext& c, float) override {
        const glm::vec3 to = c.targetPos - c.agent.position;
        if (glm::dot(to, to) > 1e-6f) c.facing = glm::normalize(to);
        return BtStatus::Success;
    }
};

// DECORATOR: gate the node on the target being within a fixed distance.
class NearTarget : public BtScript {
public:
    bool Check(AgentContext& c) override {
        return glm::length(c.targetPos - c.agent.position) <= 6.0f;
    }
};

} // namespace

void RegisterExampleBtScripts() {
    BtScriptRegistry& r = BtScriptRegistry::Instance();
    r.Register("StrafeTarget", [] { return std::make_unique<StrafeTarget>(); });
    r.Register("FaceTarget",   [] { return std::make_unique<FaceTarget>(); });
    r.Register("NearTarget",   [] { return std::make_unique<NearTarget>(); });
}

} // namespace ai
} // namespace engine
