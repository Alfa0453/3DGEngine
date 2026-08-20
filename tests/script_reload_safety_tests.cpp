#include "EditorScriptTools.h"

#include <engine/ai/BtScript.h>
#include <engine/gameplay/Script.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

namespace {
int failures = 0;
void Check(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
class ProbeScript final : public engine::Script {};
class ProbeBtScript final : public engine::ai::BtScript {};
}

int main() {
    const std::filesystem::path root = std::filesystem::temp_directory_path()
        / "3dg_script_reload_safety_test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "Binaries", ec);

    const auto candidate = EditorScriptTools::ProjectScriptCandidatePath(root, 42);
    Check(candidate.filename().string().find("42") != std::string::npos,
          "candidate path carries its generation");
    std::filesystem::create_directories(candidate.parent_path(), ec);
    std::ofstream(candidate, std::ios::binary) << "candidate";
    const auto buildProduct = EditorScriptTools::ProjectScriptBinary(root);
    std::ofstream(buildProduct, std::ios::binary) << "product";

    EditorScriptTools::ScriptModuleLoadMarker marker;
    marker.candidateDll = candidate;
    marker.buildProductDll = buildProduct;
    marker.generation = 42;
    std::string error;
    Check(EditorScriptTools::WriteScriptModuleLoadMarker(root, marker, &error),
          "persistent load marker can be written");
    EditorScriptTools::ScriptModuleLoadMarker restored;
    Check(EditorScriptTools::ReadScriptModuleLoadMarker(root, &restored, &error)
              && restored.generation == 42
              && restored.candidateDll.lexically_normal() == candidate.lexically_normal(),
          "persistent load marker round-trips candidate and generation");
    std::filesystem::path quarantined;
    Check(EditorScriptTools::QuarantineScriptModule(
              root, candidate, 42, &quarantined, &error)
              && std::filesystem::is_regular_file(quarantined)
              && !std::filesystem::exists(candidate),
          "suspect candidates move into the project quarantine");
    Check(EditorScriptTools::ClearScriptModuleLoadMarker(root, &error)
              && !std::filesystem::exists(
                  EditorScriptTools::ProjectScriptLoadMarkerPath(root)),
          "clean completion removes the load marker");

    engine::ScriptRegistry scripts;
    scripts.SetStrictValidation(true);
    scripts.Register("Probe", [] { return std::make_unique<ProbeScript>(); });
    scripts.Register("Probe", [] { return std::make_unique<ProbeScript>(); });
    Check(!scripts.Valid(&error), "duplicate native factories fail candidate validation");
    scripts.Clear();
    scripts.Register("Probe", [] { return std::make_unique<ProbeScript>(); });
    Check(scripts.Valid(&error), "constructible native factories pass validation");

    engine::ai::BtScriptRegistry btScripts;
    btScripts.Register("Probe", [] { return std::make_unique<ProbeBtScript>(); });
    Check(btScripts.Valid(&error), "constructible behavior factories pass validation");

    std::filesystem::remove_all(root, ec);
    if (failures == 0) std::cout << "script reload safety tests passed\n";
    return failures == 0 ? 0 : 1;
}
