#include "engine/plugins/LinkedPlugin.h"

#include "engine/ai/BtScript.h"
#include "engine/gameplay/Script.h"

#include <algorithm>
#include <mutex>
#include <utility>

namespace engine::plugins {
namespace {

struct Entry {
    std::string id;
    ScriptRegistration registration = nullptr;
};

std::vector<Entry>& Entries() {
    static std::vector<Entry> entries;
    return entries;
}

std::mutex& EntriesMutex() {
    static std::mutex mutex;
    return mutex;
}

} // namespace

bool RegisterLinkedPlugin(const std::string& id,
                          ScriptRegistration registration) {
    if (id.empty() || !registration) return false;

    std::lock_guard<std::mutex> lock(EntriesMutex());
    auto& entries = Entries();
    const auto existing = std::find_if(entries.begin(), entries.end(),
        [&](const Entry& entry) { return entry.id == id; });
    if (existing != entries.end()) {
        existing->registration = registration;
        return false;
    }
    entries.push_back({id, registration});
    return true;
}

void RegisterLinkedPluginScripts(ScriptRegistry& scripts,
                                 ai::BtScriptRegistry& btScripts) {
    std::vector<Entry> snapshot;
    {
        std::lock_guard<std::mutex> lock(EntriesMutex());
        snapshot = Entries();
    }
    for (const Entry& entry : snapshot) {
        if (entry.registration) entry.registration(scripts, btScripts);
    }
}

std::vector<std::string> LinkedPluginIds() {
    std::lock_guard<std::mutex> lock(EntriesMutex());
    std::vector<std::string> ids;
    ids.reserve(Entries().size());
    for (const Entry& entry : Entries()) ids.push_back(entry.id);
    return ids;
}

} // namespace engine::plugins
