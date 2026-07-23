#pragma once

#include <filesystem>
#include <string>

enum class PreferredCodeEditor {
    BuiltIn = 0,
    VisualStudioCode,
    VisualStudio,
    Rider,
    Custom
};

namespace EditorScriptTools {

bool OpenExternalEditor(PreferredCodeEditor editor,
                        const std::string& customExecutable,
                        const std::filesystem::path& scriptPath,
                        const std::filesystem::path& projectRoot,
                        std::string* error = nullptr);

bool LaunchCompileAndRestart(const std::filesystem::path& projectRoot,
                             const std::string& configuration,
                             std::string* error = nullptr);

std::string ReadLastBuildLog(const std::filesystem::path& projectRoot);
std::string ReadLastBuildStatus(const std::filesystem::path& projectRoot);

} // namespace EditorScriptTools
