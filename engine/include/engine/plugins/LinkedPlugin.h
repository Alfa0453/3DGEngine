#pragma once

#include <string>
#include <vector>

namespace engine {
class ScriptRegistry;
namespace ai { class BtScriptRegistry; }

namespace plugins {

using ScriptRegistration = void (*)(ScriptRegistry&, ai::BtScriptRegistry&);

// Native Plugin API 1 plugins are linked into the editor/player at build time.
// Their bootstrap translation unit registers one callback here during process
// startup. RegisterLinkedPluginScripts is safe to call repeatedly after the
// script registries are cleared or hot-reloaded.
bool RegisterLinkedPlugin(const std::string& id, ScriptRegistration registration);
void RegisterLinkedPluginScripts(ScriptRegistry& scripts,
                                 ai::BtScriptRegistry& btScripts);
std::vector<std::string> LinkedPluginIds();

} // namespace plugins
} // namespace engine
