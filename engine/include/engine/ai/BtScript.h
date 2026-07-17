#pragma once

#include "engine/ai/BehaviorTree.h"   // BtStatus

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {
namespace ai {

struct AgentContext;

// -----------------------------------------------------------------------------
// Author your own behaviour-tree tasks, decorators and services in C++.
//
// Subclass BtScript, override the hook that matches the role you want, register the
// class by name, then pick that name on a Script Task / Script Decorator / Script
// Service node in the editor. One instance is created per node when the tree is
// built and lives for the play session. Everything operates on the AgentContext
// blackboard (steering body, target, perception, nav, path) -- and, because the
// context also carries the entity + registry, a script can read/write any ECS
// component too (play animations, check health, etc.).
//
//   class FaceTarget : public engine::ai::BtScript {
//   public:
//       engine::ai::BtStatus Tick(engine::ai::AgentContext& c, float) override {
//           c.facing = glm::normalize(c.targetPos - c.agent.position);
//           return engine::ai::BtStatus::Running;
//       }
//   };
//   // once, at startup:
//   engine::ai::BtScriptRegistry::Instance().Register(
//       "FaceTarget", []{ return std::make_unique<FaceTarget>(); });
// -----------------------------------------------------------------------------
class BtScript {
public:
    virtual ~BtScript() = default;

    // Called once when this node's branch (re)starts, before the first Tick.
    virtual void OnEnter(AgentContext& /*c*/) {}

    // TASK / SERVICE role. Return Success, Failure or Running. For a Task the status
    // drives the tree; for a Service the status is ignored (it just does work).
    virtual BtStatus Tick(AgentContext& /*c*/, float /*dt*/) { return BtStatus::Success; }

    // DECORATOR role. Return false to block the node it is attached to (gate).
    virtual bool Check(AgentContext& /*c*/) { return true; }

    // Called when the branch stops running (after a non-Running Tick).
    virtual void OnExit(AgentContext& /*c*/) {}
};

// Name -> factory registry, mirroring the gameplay ScriptRegistry. Register once at
// startup; the editor lists Names() in the node's script picker.
class BtScriptRegistry {
public:
    using Factory = std::function<std::unique_ptr<BtScript>()>;

    static BtScriptRegistry& Instance();

    void Register(const std::string& name, Factory factory);
    bool Has(const std::string& name) const;
    std::unique_ptr<BtScript> Create(const std::string& name) const;   // nullptr if unknown
    std::vector<std::string>  Names() const;                           // sorted, for the UI

private:
    std::unordered_map<std::string, Factory> m_factories;
};

// Registers a few example BtScripts (StrafeTarget task, FaceTarget service,
// NearTarget decorator) so the pickers are non-empty and show the pattern. Call once
// at editor/game startup. Add your own registrations alongside these.
void RegisterExampleBtScripts();

} // namespace ai
} // namespace engine
