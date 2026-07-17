#pragma once
//
// EXAMPLE TASK: flee straight away from the target while it can still see you.
// Registered as "FleeTarget" in src/GameBtScripts.cpp.
//
#include <engine/ai/BtScript.h>
#include <engine/ai/BehaviorGraph.h>
#include <engine/ai/Steering.h>

class FleeTarget : public engine::ai::BtScript {
public:
    engine::ai::BtStatus Tick(engine::ai::AgentContext& c, float /*dt*/) override {
        c.steer = engine::ai::Flee(c.agent, c.targetPos);
        return c.seesTarget ? engine::ai::BtStatus::Running : engine::ai::BtStatus::Success;
    }
};
