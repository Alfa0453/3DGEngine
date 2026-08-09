#pragma once

#include <string>

namespace engine {

class ScriptRegistry;
namespace ai { class BtScriptRegistry; }

// The entry point a hot-reloadable script DLL must export (extern "C"):
//     extern "C" __declspec(dllexport) void RegisterScriptModule(
//         engine::ScriptRegistry&, engine::ai::BtScriptRegistry&);
// It registers every gameplay and behavior-tree script factory into the host registries.
using RegisterScriptModuleFn = void (*)(ScriptRegistry&, ai::BtScriptRegistry&);

// Loads a shared library of compiled game scripts and registers their factories, enabling
// hot-reload without restarting the host: Unload (after ShutdownScripts + clearing the
// registry), rebuild the DLL, then Load again.
//
// IMPORTANT lifetime rule: because the scripts' code (vtables, factories) lives inside the
// DLL, the caller MUST destroy every live Script instance (ShutdownScripts) and clear the
// ScriptRegistry BEFORE Unload(); otherwise dangling calls into freed code will crash.
class ScriptModule {
public:
    ScriptModule() = default;
    ~ScriptModule();
    ScriptModule(const ScriptModule&) = delete;
    ScriptModule& operator=(const ScriptModule&) = delete;
    ScriptModule(ScriptModule&& other) noexcept;
    ScriptModule& operator=(ScriptModule&& other) noexcept;

    // Load the DLL at `path` and call its RegisterScriptModule registries entry point.
    bool Load(const std::string& path, ScriptRegistry& registry,
              ai::BtScriptRegistry& btRegistry, std::string* error = nullptr);

    // Free the loaded library. See the lifetime rule above — clear the registry and shut
    // down scripts first.
    void Unload();

    bool IsLoaded() const { return m_handle != nullptr; }
    void Swap(ScriptModule& other) noexcept;

private:
    void* m_handle = nullptr;   // HMODULE on Windows
};

}  // namespace engine
