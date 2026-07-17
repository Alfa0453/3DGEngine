#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace engine {

enum class ShaderDomain : std::uint8_t {
    Surface = 0,
    PostProcess,
    Particle,
    Unlit
};

enum class ShaderValueType : std::uint8_t {
    Float = 0,
    Int,
    Bool,
    Vec2,
    Vec3,
    Vec4,
    Color,
    Texture2D
};

struct ShaderGraphNode {
    std::uint64_t id = 0;
    std::string type;
    std::string name;
    float x = 0.0f;
    float y = 0.0f;
    std::string comment;
    std::uint64_t groupId = 0;
    bool collapsed = false;
    std::string value;
};

struct ShaderGraphPin {
    std::uint64_t id = 0;
    std::uint64_t nodeId = 0;
    std::string name;
    ShaderValueType type = ShaderValueType::Float;
    bool input = true;
    bool required = false;
};

struct ShaderGraphLink {
    std::uint64_t id = 0;
    std::uint64_t fromPin = 0;
    std::uint64_t toPin = 0;
};

struct ShaderParameter {
    std::uint64_t id = 0;
    std::string name;
    ShaderValueType type = ShaderValueType::Float;
    std::string defaultValue = "0";
};

struct ShaderAsset {
    static constexpr int CurrentVersion = 1;
    int version = CurrentVersion;
    std::uint64_t id = 1;
    std::string name = "New Shader";
    ShaderDomain domain = ShaderDomain::Surface;
    int blendMode = 0;
    std::vector<ShaderGraphNode> nodes;
    std::vector<ShaderGraphPin> pins;
    std::vector<ShaderGraphLink> links;
    std::vector<ShaderParameter> parameters;
};

struct ShaderAssetIssue {
    enum class Severity { Warning, Error };
    Severity severity = Severity::Error;
    std::string message;
    std::uint64_t nodeId = 0;
};

const char* ShaderDomainName(ShaderDomain domain);
const char* ShaderValueTypeName(ShaderValueType type);
bool ShaderValueTypesCompatible(ShaderValueType from, ShaderValueType to);

std::vector<ShaderAssetIssue> ValidateShaderAsset(const ShaderAsset& asset);
bool ShaderAssetHasErrors(const std::vector<ShaderAssetIssue>& issues);
std::uint64_t HashShaderAsset(const ShaderAsset& asset);

bool SaveShaderAsset(const std::string& path, const ShaderAsset& asset,
                     std::string* error = nullptr);
bool LoadShaderAsset(const std::string& path, ShaderAsset* asset,
                     std::string* error = nullptr);

// Minimal visible fallbacks used when no valid program exists for an asset.
std::pair<std::string, std::string> ShaderFallbackSources(ShaderDomain domain);

} // namespace engine
