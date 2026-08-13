#include "OptimizationAuditReport.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {
int failures = 0;
void Check(bool condition, const char* message) {
    if (condition) return;
    ++failures;
    std::cerr << "FAILED: " << message << '\n';
}
}

int main() {
    using namespace editor::optimization;
    Check(ClassifyCount(99999, 100000, 500000) == Severity::Info,
          "geometry below warning threshold stays informational");
    Check(ClassifyCount(100000, 100000, 500000) == Severity::Warning,
          "warning threshold is inclusive");
    Check(ClassifyCount(500000, 100000, 500000) == Severity::Critical,
          "critical threshold is inclusive");

    std::vector<Finding> findings{
        {Severity::Warning, "Textures", 1, "Wall", "4K texture", "Reduce", 4096.0},
        {Severity::Critical, "Geometry", 2, "Boss", "Heavy mesh", "Add LOD", 600000.0},
        {Severity::Critical, "Particles", 3, "Fire", "Large pool", "Cap", 50000.0}
    };
    SortFindings(&findings);
    Check(findings[0].category == "Geometry"
              && findings[1].category == "Particles"
              && findings[2].severity == Severity::Warning,
          "findings sort by severity and estimated impact");

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "3dg_optimization_audit_test";
    const std::filesystem::path textPath = root / "audit.txt";
    const std::filesystem::path jsonPath = root / "audit.json";
    std::string error;
    Check(WriteTextReport(textPath.string(), findings, "test summary", &error),
          "text report is generated");
    Check(WriteJsonReport(jsonPath.string(), findings, "test summary", &error),
          "json report is generated");
    std::ifstream jsonFile(jsonPath);
    std::stringstream json;
    json << jsonFile.rdbuf();
    Check(json.str().find("\"severity\": \"Critical\"") != std::string::npos
              && json.str().find("Heavy mesh") != std::string::npos,
          "json report contains structured findings");
    jsonFile.close();
    std::error_code removeError;
    std::filesystem::remove_all(root, removeError);

    if (failures) return 1;
    std::cout << "optimization auditor tests passed\n";
    return 0;
}
