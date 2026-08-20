#include "EditorGeneratedScriptTools.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return input ? std::string(std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>()) : std::string{};
}

} // namespace

int main(int argc, char** argv) {
    const bool keepFixture = argc > 1;
    const std::filesystem::path root = keepFixture
        ? std::filesystem::absolute(argv[1])
        : std::filesystem::temp_directory_path() / "3dg_project_script_tools_test";
    const std::filesystem::path content = root / "Content";
    const std::filesystem::path scripts = content / "Scripts";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(scripts, ec);

    {
        std::ofstream gameplay(scripts / "TestGameplay.h");
        gameplay << "#pragma once\n#include <engine/gameplay/Script.h>\n"
                    "class TestGameplay final : public engine::Script {};\n";
        std::ofstream task(scripts / "TestTask.h");
        task << "#pragma once\n#include <engine/ai/BtScript.h>\n"
                "class TestTask final : public engine::ai::BtScript {};\n";
    }

    std::string error;
    const std::filesystem::path gameRoot(THREEDG_TEST_GAME_ROOT);
    Check(EditorGeneratedScriptTools::RegisterScript(
              gameRoot, scripts, "TestGameplay", false, &error),
          "gameplay script registration succeeds");
    Check(EditorGeneratedScriptTools::RegisterScript(
              gameRoot, scripts, "TestTask", true, &error),
          "behavior script registration succeeds");

    const std::filesystem::path generated =
        EditorGeneratedScriptTools::GeneratedScriptDirectory(content);
    Check(generated == root / "Intermediate" / "Scripts",
          "generated files live in the project Intermediate folder");
    Check(EditorGeneratedScriptTools::ProjectScriptBinary(content)
              == root / "Binaries" / "game_scripts.dll",
          "compiled module path is project-owned");
    Check(std::filesystem::is_regular_file(generated / "EditorScripts.list"),
          "project registration list is generated");
    Check(std::filesystem::is_regular_file(generated / "ProjectGeneratedScripts.h"),
          "project registration header is generated");
    Check(std::filesystem::is_regular_file(generated / "ProjectScriptModule.cpp"),
          "project module source is generated");

    const std::string header = Read(generated / "ProjectGeneratedScripts.h");
    const std::string module = Read(generated / "ProjectScriptModule.cpp");
    Check(header.find("TestGameplay") != std::string::npos,
          "gameplay factory is emitted");
    Check(header.find("TestTask") != std::string::npos,
          "behavior-tree factory is emitted");
    Check(module.find("engine::ai::BtScriptRegistry& bt") != std::string::npos,
          "module exports both host registries");
    Check(module.find("Get3DGScriptApiVersion") != std::string::npos,
          "module exports the scripting ABI handshake");
    Check(!std::filesystem::exists(root / "game" / "EditorGeneratedScripts.h"),
          "generation does not write into an engine/shared game folder");

    if (!keepFixture) std::filesystem::remove_all(root, ec);
    if (failures == 0) std::cout << "project script tools tests passed\n";
    return failures == 0 ? 0 : 1;
}
