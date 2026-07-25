#pragma once
//
// DECORATOR template. The editor copies this into Content/Scripts, renames and
// registers it automatically. Attach it through Script Decorator in a Behavior Graph.
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
