#include "OptimizationAuditReport.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace editor::optimization {
namespace {
std::string JsonEscape(const std::string& input) {
    std::ostringstream out;
    for (const unsigned char c : input) {
        switch (c) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20) out << "\\u" << std::hex << std::setw(4)
                              << std::setfill('0') << static_cast<int>(c)
                              << std::dec << std::setfill(' ');
            else out << static_cast<char>(c);
        }
    }
    return out.str();
}

bool PrepareOutput(const std::string& path, std::ofstream* out,
                   std::string* error) {
    std::error_code ec;
    const std::filesystem::path output(path);
    if (!output.parent_path().empty())
        std::filesystem::create_directories(output.parent_path(), ec);
    if (ec) {
        if (error) *error = "Could not create report folder: " + ec.message();
        return false;
    }
    out->open(output);
    if (!*out) {
        if (error) *error = "Could not open optimization report for writing.";
        return false;
    }
    return true;
}
} // namespace

Severity ClassifyCount(std::size_t value, std::size_t warningAt,
                       std::size_t criticalAt) {
    if (value >= criticalAt) return Severity::Critical;
    if (value >= warningAt) return Severity::Warning;
    return Severity::Info;
}

const char* SeverityName(Severity severity) {
    switch (severity) {
    case Severity::Info: return "Info";
    case Severity::Warning: return "Warning";
    case Severity::Critical: return "Critical";
    }
    return "Info";
}

void SortFindings(std::vector<Finding>* findings) {
    if (!findings) return;
    std::stable_sort(findings->begin(), findings->end(),
        [](const Finding& a, const Finding& b) {
            if (a.severity != b.severity)
                return static_cast<int>(a.severity) > static_cast<int>(b.severity);
            if (a.estimatedImpact != b.estimatedImpact)
                return a.estimatedImpact > b.estimatedImpact;
            if (a.category != b.category) return a.category < b.category;
            return a.objectName < b.objectName;
        });
}

bool WriteTextReport(const std::string& path,
                     const std::vector<Finding>& findings,
                     const std::string& summary, std::string* error) {
    std::ofstream out;
    if (!PrepareOutput(path, &out, error)) return false;
    out << "3DGEngine Optimization Audit\n" << summary << "\n\n";
    for (const Finding& finding : findings) {
        out << '[' << SeverityName(finding.severity) << "] "
            << finding.category << " | " << finding.objectName << "\n"
            << finding.message << "\nFix: " << finding.recommendation << "\n\n";
    }
    return true;
}

bool WriteJsonReport(const std::string& path,
                     const std::vector<Finding>& findings,
                     const std::string& summary, std::string* error) {
    std::ofstream out;
    if (!PrepareOutput(path, &out, error)) return false;
    out << "{\n  \"summary\": \"" << JsonEscape(summary)
        << "\",\n  \"findings\": [\n";
    for (std::size_t i = 0; i < findings.size(); ++i) {
        const Finding& finding = findings[i];
        out << "    {\"severity\": \"" << SeverityName(finding.severity)
            << "\", \"category\": \"" << JsonEscape(finding.category)
            << "\", \"object\": \"" << JsonEscape(finding.objectName)
            << "\", \"objectIndex\": " << finding.objectIndex
            << ", \"message\": \"" << JsonEscape(finding.message)
            << "\", \"recommendation\": \""
            << JsonEscape(finding.recommendation) << "\"}"
            << (i + 1 < findings.size() ? "," : "") << '\n';
    }
    out << "  ]\n}\n";
    return true;
}

} // namespace editor::optimization
