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

#include <memory>

#if defined(_WIN32)
#define SCRIPT_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define SCRIPT_MODULE_EXPORT extern "C"
#endif

SCRIPT_MODULE_EXPORT void RegisterScriptModule(engine::ScriptRegistry& scripts) {
    scripts.Register("Spinner", [] { return std::make_unique<Spinner>(); });
    RegisterEditorGeneratedScripts(scripts);
}
