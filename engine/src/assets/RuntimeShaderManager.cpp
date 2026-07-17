#include "engine/assets/RuntimeShaderManager.h"

#include <filesystem>
#include <limits>
#include <utility>

namespace engine {
namespace {

std::uint64_t Fnv1a(std::uint64_t hash, const std::string& text)
{
    constexpr std::uint64_t prime = 1099511628211ull;
    for (const unsigned char byte : text)
    {
        hash ^= byte;
        hash *= prime;
    }
    return hash;
}

ShaderCompileReport ValidationReport(const std::vector<ShaderAssetIssue>& issues)
{
    ShaderCompileReport report;
    report.success = !ShaderAssetHasErrors(issues);
    for (const ShaderAssetIssue& issue : issues)
    {
        report.diagnostics.push_back({
            issue.severity == ShaderAssetIssue::Severity::Error
                ? ShaderDiagnostic::Severity::Error
                : ShaderDiagnostic::Severity::Warning,
            ShaderStage::Unknown,
            0,
            issue.nodeId == 0
                ? issue.message
                : "Node " + std::to_string(issue.nodeId) + ": " + issue.message
        });
    }
    return report;
}

std::uint64_t FileStamp(const std::string& path)
{
    std::error_code error;
    const auto time = std::filesystem::last_write_time(path, error);
    if (error) return std::numeric_limits<std::uint64_t>::max();
    return static_cast<std::uint64_t>(time.time_since_epoch().count());
}

std::unordered_map<std::string, std::uint64_t> Snapshot(
    const std::vector<std::string>& dependencies)
{
    std::unordered_map<std::string, std::uint64_t> result;
    for (const std::string& path : dependencies)
        result[path] = FileStamp(path);
    return result;
}

} // namespace

std::string RuntimeShaderManager::Key(
    const std::string& assetPath, const std::string& variant)
{
    return assetPath + '\x1f' + variant;
}

std::uint64_t RuntimeShaderManager::HashSources(
    const std::string& vertexSource, const std::string& fragmentSource)
{
    std::uint64_t hash = 14695981039346656037ull;
    hash = Fnv1a(hash, vertexSource);
    hash ^= 0xffu;
    hash *= 1099511628211ull;
    return Fnv1a(hash, fragmentSource);
}

bool RuntimeShaderManager::CompileOrReload(
    const std::string& assetPath,
    const std::string& variant,
    const ShaderAsset& asset,
    const std::string& vertexSource,
    const std::string& fragmentSource,
    const std::vector<std::string>& dependencies)
{
    const std::string key = Key(assetPath, variant);
    const auto issues = ValidateShaderAsset(asset);
    ShaderCompileReport report = ValidationReport(issues);
    if (!report.success)
    {
        auto it = m_entries.find(key);
        if (it != m_entries.end()) it->second.report = std::move(report);
        else
        {
            Entry entry;
            entry.report = std::move(report);
            m_entries.emplace(key, std::move(entry));
        }
        return false;
    }

    const std::uint64_t assetHash = HashShaderAsset(asset);
    const std::uint64_t sourceHash = HashSources(vertexSource, fragmentSource);
    auto found = m_entries.find(key);
    if (found != m_entries.end() && found->second.program
        && !found->second.fallback
        && found->second.assetHash == assetHash
        && found->second.sourceHash == sourceHash)
    {
        return true;
    }

    ShaderCompileReport compileReport;
    std::unique_ptr<Shader> program =
        Shader::TryCompile(vertexSource, fragmentSource, compileReport);
    if (program)
    {
        Entry entry;
        entry.program = std::move(program);
        entry.report = std::move(compileReport);
        entry.assetHash = assetHash;
        entry.sourceHash = sourceHash;
        entry.dependencies = Snapshot(dependencies);
        m_entries[key] = std::move(entry);
        return true;
    }

    // Preserve a last-known-good program during a failed edit.
    if (found != m_entries.end() && found->second.program && !found->second.fallback)
    {
        found->second.report = std::move(compileReport);
        return false;
    }

    const auto fallbackSources = ShaderFallbackSources(asset.domain);
    ShaderCompileReport fallbackReport;
    std::unique_ptr<Shader> fallback = Shader::TryCompile(
        fallbackSources.first, fallbackSources.second, fallbackReport);

    Entry entry;
    entry.program = std::move(fallback);
    entry.report = std::move(compileReport);
    entry.assetHash = assetHash;
    entry.sourceHash = sourceHash;
    entry.fallback = entry.program != nullptr;
    entry.dependencies = Snapshot(dependencies);
    m_entries[key] = std::move(entry);
    return false;
}

const Shader* RuntimeShaderManager::Find(
    const std::string& assetPath, const std::string& variant) const
{
    const auto found = m_entries.find(Key(assetPath, variant));
    return found == m_entries.end() ? nullptr : found->second.program.get();
}

const ShaderCompileReport* RuntimeShaderManager::LastReport(
    const std::string& assetPath, const std::string& variant) const
{
    const auto found = m_entries.find(Key(assetPath, variant));
    return found == m_entries.end() ? nullptr : &found->second.report;
}

bool RuntimeShaderManager::IsUsingFallback(
    const std::string& assetPath, const std::string& variant) const
{
    const auto found = m_entries.find(Key(assetPath, variant));
    return found != m_entries.end() && found->second.fallback;
}

bool RuntimeShaderManager::DependenciesChanged(
    const std::string& assetPath, const std::string& variant) const
{
    const auto found = m_entries.find(Key(assetPath, variant));
    if (found == m_entries.end()) return true;
    for (const auto& dependency : found->second.dependencies)
        if (FileStamp(dependency.first) != dependency.second) return true;
    return false;
}

void RuntimeShaderManager::Clear()
{
    m_entries.clear();
}

} // namespace engine
