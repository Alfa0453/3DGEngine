#include "engine/gameplay/ScriptModule.h"

#include "engine/gameplay/Script.h"   // ScriptRegistry (passed by reference to the DLL)
#include "engine/ai/BtScript.h"

#include <exception>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace engine {

ScriptModule::ScriptModule(ScriptModule&& other) noexcept
    : m_handle(other.m_handle), m_loadedPath(std::move(other.m_loadedPath)) {
    other.m_handle = nullptr;
}

ScriptModule& ScriptModule::operator=(ScriptModule&& other) noexcept {
    if (this != &other) {
        if (!Unload()) return *this;
        m_handle = other.m_handle;
        m_loadedPath = std::move(other.m_loadedPath);
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
        if (!Unload(error)) return false;
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
    auto version = reinterpret_cast<GetScriptModuleApiVersionFn>(
        reinterpret_cast<void*>(GetProcAddress(handle, "Get3DGScriptApiVersion")));
    if (!version || version() != kScriptModuleApiVersion) {
        FreeLibrary(handle);
        if (error) *error = "Script module '" + path
            + "' uses an incompatible or missing 3DG scripting API version.";
        return false;
    }
    try {
        entry(registry, btRegistry);
    } catch (const std::exception& exception) {
        registry.Clear();
        btRegistry.Clear();
        FreeLibrary(handle);
        if (error) *error = "Script module registration failed: "
            + std::string(exception.what());
        return false;
    } catch (...) {
        registry.Clear();
        btRegistry.Clear();
        FreeLibrary(handle);
        if (error) *error = "Script module registration failed with an unknown exception.";
        return false;
    }
    m_handle = handle;
    m_loadedPath = path;
    return true;
#else
    (void)path;
    (void)registry;
    (void)btRegistry;
    if (error) *error = "Script module hot-reload is only implemented on Windows.";
    return false;
#endif
}

bool ScriptModule::Unload(std::string* error) {
#if defined(_WIN32)
    if (m_handle) {
        if (!FreeLibrary(static_cast<HMODULE>(m_handle))) {
            if (error) *error = "Could not unload script module '" + m_loadedPath + "'.";
            return false;
        }
        m_handle = nullptr;
        m_loadedPath.clear();
    }
#else
    (void)error;
#endif
    return true;
}

void ScriptModule::Swap(ScriptModule& other) noexcept {
    void* handle = m_handle;
    m_handle = other.m_handle;
    other.m_handle = handle;
    m_loadedPath.swap(other.m_loadedPath);
}

}  // namespace engine
