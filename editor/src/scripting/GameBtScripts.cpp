#include "GameBtScripts.h"

#include <engine/ai/BtScript.h>

#include <memory>

// Your behaviour-tree scripts live in editor/btscripts/ (one header per script).
// To add one: copy a file from btscripts/templates/, rename it, then #include it
// here and add a Register(...) line below. See btscripts/README.md.
#include "FleeTarget.h"
#include "NotAtTarget.h"

void RegisterGameBtScripts() {
    engine::ai::BtScriptRegistry& r = engine::ai::BtScriptRegistry::Instance();

    r.Register("FleeTarget",  [] { return std::make_unique<FleeTarget>(); });
    r.Register("NotAtTarget", [] { return std::make_unique<NotAtTarget>(); });

    // Add your own here (after #include-ing its header above):
    // r.Register("ChaseAndShoot", [] { return std::make_unique<ChaseAndShoot>(); });
}
