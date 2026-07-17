#pragma once
//
// DECORATOR template. Copy into ../ , rename file + class, fill in Check(), then
// #include + Register it in src/GameBtScripts.cpp. Attach it to a node via
// "+ Decorator > Script Decorator" in the inspector.
//
// A Decorator is a GATE: return false and the node it is attached to is blocked
// (reports Failure without running); return true and the node runs normally.
//
#include <engine/ai/BtScript.h>
#include <engine/ai/BehaviorGraph.h>   // AgentContext

#include <glm/glm.hpp>

class MyDecorator : public engine::ai::BtScript {
public:
    bool Check(engine::ai::AgentContext& c) override {
        // TODO: return true to allow the node, false to block it. Example: only allow
        // the node when a target is currently visible.
        return c.seesTarget;
    }
};
