#pragma once

#include <filesystem>
#include <string>

namespace EditorGeneratedScriptTools {

enum class BehaviorTreeTemplate {
    Task,
    Decorator,
    Service
};

bool RegisterScript(const std::filesystem::path& gameRoot,
                    const std::filesystem::path& scriptRoot,
                    const std::string& className,
                    bool behaviorTree,
                    std::string* error = nullptr);

bool CreateBehaviorTreeScript(const std::filesystem::path& contentRoot,
                              const std::string& className,
                              BehaviorTreeTemplate scriptTemplate,
                              std::string* createdPath = nullptr,
                              std::string* error = nullptr);

// Rewrites the shared game module's EditorGeneratedScripts.h from ONLY the given
// project's script list (stored next to its .project, above Content/Scripts). Call this
// when opening or creating a project so scripts from other projects are not compiled in.
// contentRoot is the project's Content/asset root. An empty/absent list yields a header
// that registers nothing.
bool RegenerateGeneratedScripts(const std::filesystem::path& contentRoot,
                                std::string* error = nullptr);

} // namespace EditorGeneratedScriptTools
