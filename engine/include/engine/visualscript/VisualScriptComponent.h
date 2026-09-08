#pragma once

// =============================================================================
// Visual Scripting — Pass 1 : the authored ECS component (Phase 13).
//
// Deliberately tiny. It stores ONLY authored data — a graph asset reference and
// an enable flag. It holds NO runtime node state, interpreter stack, graph
// pointer, or editor object; that all lives in VisualScriptInstance, owned by
// the VisualScriptRuntime (Phase 11/12).
// =============================================================================

#include <engine/assets/AssetIdentity.h>

namespace engine::vs {

struct VisualScriptComponent {
    AssetHandle graph;          // reference to a .3dgvs asset (never the graph itself)
    bool        enabled = true;
};

} // namespace engine::vs
