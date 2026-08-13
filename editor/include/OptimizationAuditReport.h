#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace editor::optimization {

enum class Severity { Info = 0, Warning = 1, Critical = 2 };

struct Finding {
    Severity severity = Severity::Info;
    std::string category;
    int objectIndex = -1;
    std::string objectName = "Scene";
    std::string message;
    std::string recommendation;
    double estimatedImpact = 0.0;
    int quickFix = 0;
};

Severity ClassifyCount(std::size_t value, std::size_t warningAt,
                       std::size_t criticalAt);
void SortFindings(std::vector<Finding>* findings);
bool WriteTextReport(const std::string& path,
                     const std::vector<Finding>& findings,
                     const std::string& summary, std::string* error = nullptr);
bool WriteJsonReport(const std::string& path,
                     const std::vector<Finding>& findings,
                     const std::string& summary, std::string* error = nullptr);
const char* SeverityName(Severity severity);

} // namespace editor::optimization
