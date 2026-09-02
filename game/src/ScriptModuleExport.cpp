// Exported entry point for the hot-reloadable script DLL (the `game_scripts` target).
// It registers the same gameplay scripts as the static game module, but into the HOST's
// ScriptRegistry (passed by reference) rather than this module's own singleton, so a
// reloaded DLL shares the editor's registry. The engine ScriptModule loader calls this.
//
// Keep the manual Register(...) lines here in sync with game/src/GameModule.cpp.
// (Editor-created scripts under Content/Scripts are handled by RegisterEditorGeneratedScripts.)

#include "game/EditorGeneratedScripts.h"
#include "game/scripts/Spinner.h"

#include <engine/gameplay/Script.h>
#include <engine/gameplay/ScriptModule.h>
#include <engine/gameplay/ScriptModuleAbi.h>   // Scripting Pass 3: module info handshake
#include <engine/ai/BtScript.h>

#include <memory>

#if defined(_WIN32)
#define SCRIPT_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define SCRIPT_MODULE_EXPORT extern "C"
#endif

SCRIPT_MODULE_EXPORT void RegisterScriptModule(
    engine::ScriptRegistry& scripts, engine::ai::BtScriptRegistry& bt) {
    scripts.Register("Spinner", [] { return std::make_unique<Spinner>(); });
    RegisterEditorGeneratedScripts(scripts);
    RegisterEditorGeneratedBtScripts(bt);
}

SCRIPT_MODULE_EXPORT std::uint32_t Get3DGScriptApiVersion() {
    return engine::kScriptModuleApiVersion;
}

// Scripting Pass 3: richer ABI handshake. Additive and OPTIONAL -- the engine still checks
// Get3DGScriptApiVersion above as the hard gate; a loader that also reads this gets the full
// ScriptModuleInfo (abi / scriptApi / reflection schema / build stamps) for transactional validation.
// The struct is POD (fixed-width ints only), so returning it by value is C-ABI safe.
SCRIPT_MODULE_EXPORT engine::script::ScriptModuleInfo Get3DGScriptModuleInfo() {
    engine::script::ScriptModuleInfo info;
    info.abiVersion              = engine::kScriptModuleApiVersion;
    info.scriptApiVersion        = engine::kScriptModuleApiVersion;
    info.reflectionSchemaVersion = engine::script::kReflectionSchemaVersion;
    // A stable-per-build stamp derived from this TU's compile time (informational / drift detection).
    info.moduleBuildId           = engine::script::StableId(__DATE__ " " __TIME__);
    info.engineBuildId           = 0;   // filled by a future engine-build-stamp handshake
    return info;
}
