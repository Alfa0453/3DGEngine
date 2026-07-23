#include "game/GameModule.h"
#include "game/EditorGeneratedScripts.h"

#include <engine/gameplay/Script.h>   // engine::ScriptRegistry, engine::Script
#include <engine/ai/BtScript.h>       // engine::ai::BtScriptRegistry

#include <memory>

// -----------------------------------------------------------------------------
// The one place your game registers its scripts. Compiled into the shared `game`
// static library, which both the editor and the standalone player link -- so every
// script here runs identically in Play mode and in the shipped game.
//
// HOW TO ADD A SCRIPT
//   1. Put the class in a header under game/include/game/scripts/ (a subclass of
//      engine::Script for gameplay, or engine::ai::BtScript for behaviour-tree nodes).
//      If it has a .cpp, list it in game/CMakeLists.txt.
//   2. #include it below.
//   3. Add a Register(...) line. The name MUST match the class name a scene stores
//      (the name shown in the editor's Script / BT-node fields).
// -----------------------------------------------------------------------------

// Gameplay scripts (engine::Script):
#include "game/scripts/Spinner.h"
// #include "game/scripts/FireballCaster.h"
// #include "game/scripts/FireballProjectile.h"

// Behaviour-tree scripts (engine::ai::BtScript):
// #include "game/scripts/ChaseAndShoot.h"

void RegisterGameModule() {
    engine::ScriptRegistry&       scripts = engine::ScriptRegistry::Instance();
    engine::ai::BtScriptRegistry& bt      = engine::ai::BtScriptRegistry::Instance();
    (void)bt;

    // --- Gameplay scripts ---------------------------------------------------
    scripts.Register("Spinner", [] { return std::make_unique<Spinner>(); });
    RegisterEditorGeneratedScripts(scripts);
    // scripts.Register("FireballCaster",     [] { return std::make_unique<FireballCaster>(); });
    // scripts.Register("FireballProjectile", [] { return std::make_unique<FireballProjectile>(); });

    // --- Behaviour-tree scripts --------------------------------------------
    // bt.Register("ChaseAndShoot", [] { return std::make_unique<ChaseAndShoot>(); });
}
