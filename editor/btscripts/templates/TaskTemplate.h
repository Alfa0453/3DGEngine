#pragma once
//
// TASK template. Copy this file into ../ (the btscripts folder), rename the file and
// the class, fill in Tick(), then #include + Register it in src/GameBtScripts.cpp.
//
// A Task is a leaf that DOES something and reports how it went.
//
#include <engine/ai/BtScript.h>
#include <engine/ai/BehaviorGraph.h>   // AgentContext, BtStatus
#include <engine/ai/Steering.h>        // Seek / Flee / Arrive / FollowPath / ...

#include <glm/glm.hpp>

class MyTask : public engine::ai::BtScript {
public:
    // Optional: called once when this node starts running.
    void OnEnter(engine::ai::AgentContext& /*c*/) override {}

    engine::ai::BtStatus Tick(engine::ai::AgentContext& c, float /*dt*/) override {
        // TODO: your logic here. Set c.steer to move the agent, then return one of:
        //   Running  -> still working; keep me active next tick
        //   Success  -> finished successfully; parent moves on
        //   Failure  -> couldn't do it; let the tree try a sibling
        c.steer = engine::ai::Seek(c.agent, c.targetPos);
        return engine::ai::BtStatus::Running;
    }

    // Optional: called once when this node stops running (after a non-Running Tick).
    void OnExit(engine::ai::AgentContext& /*c*/) override {}
};
