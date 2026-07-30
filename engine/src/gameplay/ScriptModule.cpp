#include "engine/gameplay/ScriptModule.h"

#include "engine/gameplay/Script.h"   // ScriptRegistry (passed by reference to the DLL)

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace engine {

ScriptModule::~ScriptModule() {
    Unload();
}

bool ScriptModule::Load(const std::string& path, ScriptRegistry& registry, std::string* error) {
#if defined(_WIN32)
    if (m_handle) {
        Unload();
    }
    HMODULE handle = LoadLibraryA(path.c_str());
    if (!handle) {
        if (error) *error = "Could not load script module: " + path;
        return false;
    }
    // Two-step cast quiets the object-pointer <-> function-pointer conversion warning.
    auto entry = reinterpret_cast<RegisterScriptModuleFn>(
        reinterpret_cast<void*>(GetProcAddress(handle, "RegisterScriptModule")));
    if (!entry) {
        FreeLibrary(handle);
        if (error) {
            *error = "Script module '" + path + "' has no RegisterScriptModule export.";
        }
        return false;
    }
    entry(registry);
    m_handle = handle;
    return true;
#else
    (void)path;
    (void)registry;
    if (error) *error = "Script module hot-reload is only implemented on Windows.";
    return false;
#endif
}

void ScriptModule::Unload() {
#if defined(_WIN32)
    if (m_handle) {
        FreeLibrary(static_cast<HMODULE>(m_handle));
        m_handle = nullptr;
    }
#endif
}

}  // namespace engine
