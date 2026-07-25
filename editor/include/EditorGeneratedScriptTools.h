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

} // namespace EditorGeneratedScriptTools
