#include "engine/gameplay/ScriptModule.h"

#include "engine/gameplay/Script.h"   // ScriptRegistry (passed by reference to the DLL)
#include "engine/gameplay/ScriptModuleAbi.h"
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
    auto moduleInfo = reinterpret_cast<script::GetScriptModuleInfoFn>(
        reinterpret_cast<void*>(GetProcAddress(handle, "Get3DGScriptModuleInfo")));
    if (moduleInfo) {
        script::ReloadDiagnostics diagnostics;
        script::ScriptModuleAbiExpectation expectation;
        expectation.hasRegisterExport = entry != nullptr;
        script::ScriptModuleInfo info;
        try {
            info = moduleInfo();
        } catch (...) {
            FreeLibrary(handle);
            if (error) *error = "Script module '" + path
                + "' failed while reporting ABI information.";
            return false;
        }
        if (!script::ValidateModuleAbi(expectation, info, diagnostics)) {
            FreeLibrary(handle);
            if (error) {
                *error = "Script module '" + path + "' is incompatible";
                for (const std::string& diagnostic : diagnostics.errors) {
                    *error += ": " + diagnostic;
                }
            }
            return false;
        }
    }
    // Registration is transactional. Module code never writes directly into the
    // live registries, so a throw or duplicate cannot erase/replace built-ins.
    ScriptRegistry stagedScripts;
    ai::BtScriptRegistry stagedBtScripts;
    stagedScripts.SetStrictValidation(true);
    stagedBtScripts.SetStrictValidation(true);
    try {
        entry(stagedScripts, stagedBtScripts);
    } catch (const std::exception& exception) {
        stagedScripts.Clear();
        stagedBtScripts.Clear();
        FreeLibrary(handle);
        if (error) *error = "Script module registration failed: "
            + std::string(exception.what());
        return false;
    } catch (...) {
        stagedScripts.Clear();
        stagedBtScripts.Clear();
        FreeLibrary(handle);
        if (error) *error = "Script module registration failed with an unknown exception.";
        return false;
    }
    std::string validationError;
    if (!stagedScripts.Valid(&validationError)
        || !stagedBtScripts.Valid(&validationError)) {
        stagedScripts.Clear();
        stagedBtScripts.Clear();
        FreeLibrary(handle);
        if (error) *error = "Script module registration is invalid: " + validationError;
        return false;
    }
    for (const std::string& name : stagedScripts.Names()) {
        if (!registry.Has(name)) continue;
        stagedScripts.Clear();
        stagedBtScripts.Clear();
        FreeLibrary(handle);
        if (error) *error = "Script module conflicts with registered script class '"
            + name + "'. Use a unique project script name.";
        return false;
    }
    for (const std::string& name : stagedBtScripts.Names()) {
        if (!btRegistry.Has(name)) continue;
        stagedScripts.Clear();
        stagedBtScripts.Clear();
        FreeLibrary(handle);
        if (error) *error = "Script module conflicts with registered behavior script '"
            + name + "'. Use a unique project script name.";
        return false;
    }
    registry.MergeFrom(std::move(stagedScripts));
    btRegistry.MergeFrom(std::move(stagedBtScripts));
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
