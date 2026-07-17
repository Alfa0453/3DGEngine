#pragma once

#include "engine/assets/ShaderAsset.h"
#include "engine/graphics/Shader.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

// Owns compiled shader programs by asset path and variant. Failed hot reloads
// retain the last valid program; first-time failures receive a visible fallback.
class RuntimeShaderManager {
public:
    bool CompileOrReload(const std::string& assetPath,
                         const std::string& variant,
                         const ShaderAsset& asset,
                         const std::string& vertexSource,
                         const std::string& fragmentSource,
                         const std::vector<std::string>& dependencies = {});

    const Shader* Find(const std::string& assetPath,
                       const std::string& variant = "default") const;
    const ShaderCompileReport* LastReport(const std::string& assetPath,
                                          const std::string& variant = "default") const;
    bool IsUsingFallback(const std::string& assetPath,
                         const std::string& variant = "default") const;
    bool DependenciesChanged(const std::string& assetPath,
                             const std::string& variant = "default") const;
    void Clear();

    static std::uint64_t HashSources(const std::string& vertexSource,
                                     const std::string& fragmentSource);

private:
    struct Entry {
        std::unique_ptr<Shader> program;
        ShaderCompileReport report;
        std::uint64_t assetHash = 0;
        std::uint64_t sourceHash = 0;
        bool fallback = false;
        std::unordered_map<std::string, std::uint64_t> dependencies;
    };

    static std::string Key(const std::string& assetPath, const std::string& variant);
    std::unordered_map<std::string, Entry> m_entries;
};

} // namespace engine
