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

// Generates the active project's native-script module sources under
// <Project>/Intermediate/Scripts. No generated file is written into the engine source
// tree, so projects can be opened and compiled independently.
bool RegenerateGeneratedScripts(const std::filesystem::path& contentRoot,
                                std::string* error = nullptr);

std::filesystem::path GeneratedScriptDirectory(
    const std::filesystem::path& contentRoot);
std::filesystem::path ProjectScriptBinary(
    const std::filesystem::path& contentRoot);

} // namespace EditorGeneratedScriptTools
