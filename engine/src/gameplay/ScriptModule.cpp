#include "engine/gameplay/ScriptModule.h"

#include "engine/gameplay/Script.h"   // ScriptRegistry (passed by reference to the DLL)
#include "engine/ai/BtScript.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace engine {

ScriptModule::ScriptModule(ScriptModule&& other) noexcept
    : m_handle(other.m_handle) {
    other.m_handle = nullptr;
}

ScriptModule& ScriptModule::operator=(ScriptModule&& other) noexcept {
    if (this != &other) {
        Unload();
        m_handle = other.m_handle;
        other.m_handle = nullptr;
    }
    return *this;
}

ScriptModule::~ScriptModule() {
    Unload();
}

bool ScriptModule::Load(const std::string& path, ScriptRegistry& registry,
                        ai::BtScriptRegistry& btRegistry, std::string* error) {
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
    entry(registry, btRegistry);
    m_handle = handle;
    return true;
#else
    (void)path;
    (void)registry;
    (void)btRegistry;
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

void ScriptModule::Swap(ScriptModule& other) noexcept {
    void* handle = m_handle;
    m_handle = other.m_handle;
    other.m_handle = handle;
}

}  // namespace engine
