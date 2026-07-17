#pragma once
//
// SERVICE template. Copy into ../ , rename file + class, fill in Tick(), then
// #include + Register it in src/GameBtScripts.cpp. Attach it to a node via
// "+ Service > Script Service" in the inspector and set its interval.
//
// A Service runs in the BACKGROUND while its node's branch is active. Its Tick()
// return value is ignored -- use it for side effects (refresh a blackboard value,
// keep facing the target, poll for something, etc.). The interval you set on the
// attachment throttles how often it runs.
//
#include <engine/ai/BtScript.h>
#include <engine/ai/BehaviorGraph.h>   // AgentContext, BtStatus

#include <glm/glm.hpp>

class MyService : public engine::ai::BtScript {
public:
    engine::ai::BtStatus Tick(engine::ai::AgentContext& c, float /*dt*/) override {
        // TODO: background work. Example: keep the agent facing its target.
        const glm::vec3 to = c.targetPos - c.agent.position;
        if (glm::dot(to, to) > 1e-6f) c.facing = glm::normalize(to);
        return engine::ai::BtStatus::Success;   // ignored for services
    }
};
