#pragma once
//
// EXAMPLE DECORATOR: only run the node while the agent is not already at the target.
// Registered as "NotAtTarget" in src/GameBtScripts.cpp.
//
#include <engine/ai/BtScript.h>
#include <engine/ai/BehaviorGraph.h>

#include <glm/glm.hpp>

class NotAtTarget : public engine::ai::BtScript {
public:
    bool Check(engine::ai::AgentContext& c) override {
        return glm::length(c.targetPos - c.agent.position) > c.reachRadius;
    }
};
