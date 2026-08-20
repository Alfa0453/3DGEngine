#pragma once

#include <filesystem>
#include <cstdint>
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
                        std::string* error = nullptr,
                        int line = 0,
                        int column = 0);

bool LaunchCompileAndRestart(const std::filesystem::path& projectRoot,
                             const std::string& configuration,
                             std::string* error = nullptr);

// Generates a project-local CMake IDE tree containing the real engine and
// game_scripts targets. No engine files are copied into the game project.
bool GenerateScriptIdeProject(const std::filesystem::path& projectRoot,
                              std::filesystem::path* solutionPath = nullptr,
                              std::string* error = nullptr);
bool OpenScriptIdeProject(PreferredCodeEditor editor,
                          const std::string& customExecutable,
                          const std::filesystem::path& projectRoot,
                          std::string* error = nullptr);

// Synchronously builds a single CMake target (e.g. "player") and waits for it to finish.
// Output goes to <projectRoot>/build/target_build.log. Used at cook time so the packaged
// player has current scripts even though the iterate loop only rebuilds the editor.
bool BuildTarget(const std::filesystem::path& projectRoot,
                 const std::string& configuration,
                 const std::string& target,
                 std::string* error = nullptr);

// Builds and stages a standalone player around an already cooked project. The
// package uses its own project-local CMake build folder, so changing Release or
// static-runtime settings never reconfigures the editor's active build tree.
// When createZip is true, outputArtifact receives the zip path; otherwise it
// receives the staged runnable folder.
bool PackageProject(const std::filesystem::path& projectRoot,
                    const std::filesystem::path& cookedRoot,
                    const std::filesystem::path& outputRoot,
                    const std::string& projectName,
                    const std::string& configuration,
                    bool staticRuntime,
                    bool cleanOutput,
                    bool createZip,
                    std::filesystem::path* outputArtifact = nullptr,
                    std::string* error = nullptr);

// Directory of the running editor executable (where game_scripts.dll is emitted).
std::filesystem::path ExecutableDirectory();
std::filesystem::path EngineSourceDirectory();
std::filesystem::path EngineBuildDirectory();
std::filesystem::path ProjectScriptBinary(const std::filesystem::path& projectRoot);
// Legacy two-slot path retained for compatibility with old projects. New builds use a
// unique generation path so no loaded image is ever overwritten.
std::filesystem::path ProjectScriptStagingPath(const std::filesystem::path& projectRoot,
                                               int slot = 0);
std::filesystem::path ProjectScriptCandidatePath(const std::filesystem::path& projectRoot,
                                                 std::uint64_t generation);
std::filesystem::path ProjectScriptLoadMarkerPath(const std::filesystem::path& projectRoot);

struct ScriptModuleLoadMarker {
    std::filesystem::path candidateDll;
    std::filesystem::path buildProductDll;
    std::uint64_t generation = 0;
};

bool WriteScriptModuleLoadMarker(const std::filesystem::path& projectRoot,
                                 const ScriptModuleLoadMarker& marker,
                                 std::string* error = nullptr);
bool ReadScriptModuleLoadMarker(const std::filesystem::path& projectRoot,
                                ScriptModuleLoadMarker* marker,
                                std::string* error = nullptr);
bool ClearScriptModuleLoadMarker(const std::filesystem::path& projectRoot,
                                 std::string* error = nullptr);
bool QuarantineScriptModule(const std::filesystem::path& projectRoot,
                            const std::filesystem::path& module,
                            std::uint64_t generation,
                            std::filesystem::path* quarantined = nullptr,
                            std::string* error = nullptr);
void CleanupScriptCandidates(const std::filesystem::path& projectRoot,
                             const std::filesystem::path& keep = {});

std::string ReadLastBuildLog(const std::filesystem::path& projectRoot);
std::string ReadLastBuildStatus(const std::filesystem::path& projectRoot);

} // namespace EditorScriptTools
