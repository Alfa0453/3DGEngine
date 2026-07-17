#include "engine/assets/ShaderAsset.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace engine {
namespace {

constexpr std::size_t kMaximumNodes = 512;
constexpr std::size_t kMaximumPins = 4096;
constexpr std::size_t kMaximumLinks = 4096;
constexpr std::size_t kMaximumParameters = 256;
constexpr std::size_t kMaximumTextures = 16;

std::string CanonicalShaderAsset(const ShaderAsset& asset) {
    std::ostringstream out;
    out << asset.version << '|' << asset.id << '|' << asset.name << '|'
        << static_cast<int>(asset.domain) << '|' << asset.blendMode;
    for (const ShaderGraphNode& node : asset.nodes)
        out << "|n:" << node.id << ':' << node.type << ':' << node.name << ':'
            << node.x << ':' << node.y << ':' << node.comment << ':' << node.groupId
            << ':' << node.collapsed << ':' << node.value;
    for (const ShaderGraphPin& pin : asset.pins)
        out << "|p:" << pin.id << ':' << pin.nodeId << ':' << pin.name << ':'
            << static_cast<int>(pin.type) << ':' << pin.input << ':' << pin.required;
    for (const ShaderGraphLink& link : asset.links)
        out << "|l:" << link.id << ':' << link.fromPin << ':' << link.toPin;
    for (const ShaderParameter& parameter : asset.parameters)
        out << "|u:" << parameter.id << ':' << parameter.name << ':'
            << static_cast<int>(parameter.type) << ':' << parameter.defaultValue;
    return out.str();
}

bool IsFinite(float value) {
    return std::isfinite(value);
}

} // namespace

const char* ShaderDomainName(ShaderDomain domain) {
    switch (domain) {
    case ShaderDomain::Surface: return "Surface";
    case ShaderDomain::PostProcess: return "Post Process";
    case ShaderDomain::Particle: return "Particle";
    case ShaderDomain::Unlit: return "Unlit";
    }
    return "Surface";
}

const char* ShaderValueTypeName(ShaderValueType type) {
    switch (type) {
    case ShaderValueType::Float: return "Float";
    case ShaderValueType::Int: return "Int";
    case ShaderValueType::Bool: return "Bool";
    case ShaderValueType::Vec2: return "Vector2";
    case ShaderValueType::Vec3: return "Vector3";
    case ShaderValueType::Vec4: return "Vector4";
    case ShaderValueType::Color: return "Color";
    case ShaderValueType::Texture2D: return "Texture2D";
    }
    return "Float";
}

bool ShaderValueTypesCompatible(ShaderValueType from, ShaderValueType to) {
    if (from == to) return true;
    if ((from == ShaderValueType::Color && to == ShaderValueType::Vec4)
        || (from == ShaderValueType::Vec4 && to == ShaderValueType::Color))
        return true;
    return from == ShaderValueType::Float
        && (to == ShaderValueType::Vec2 || to == ShaderValueType::Vec3
            || to == ShaderValueType::Vec4 || to == ShaderValueType::Color);
}

std::vector<ShaderAssetIssue> ValidateShaderAsset(const ShaderAsset& asset) {
    std::vector<ShaderAssetIssue> issues;
    auto error = [&issues](std::string message, std::uint64_t node = 0) {
        issues.push_back({ShaderAssetIssue::Severity::Error, std::move(message), node});
    };
    auto warning = [&issues](std::string message, std::uint64_t node = 0) {
        issues.push_back({ShaderAssetIssue::Severity::Warning, std::move(message), node});
    };
    if (asset.id == 0) error("Shader asset ID must be non-zero.");
    if (static_cast<int>(asset.domain) < static_cast<int>(ShaderDomain::Surface)
        || static_cast<int>(asset.domain) > static_cast<int>(ShaderDomain::Unlit))
        error("Shader domain is invalid.");
    if (asset.name.empty()) error("Shader name cannot be empty.");
    if (asset.nodes.size() > kMaximumNodes) error("Shader graph exceeds 512 nodes.");
    if (asset.pins.size() > kMaximumPins) error("Shader graph exceeds 4096 pins.");
    if (asset.links.size() > kMaximumLinks) error("Shader graph exceeds 4096 links.");
    if (asset.parameters.size() > kMaximumParameters) error("Shader exposes more than 256 parameters.");
    if (asset.blendMode < 0 || asset.blendMode > 2) error("Shader blend mode is invalid.");

    std::unordered_set<std::uint64_t> nodeIds;
    std::unordered_map<std::uint64_t, const ShaderGraphNode*> nodes;
    const char* requiredOutput = "SurfaceOutput";
    switch (asset.domain) {
    case ShaderDomain::Surface: requiredOutput = "SurfaceOutput"; break;
    case ShaderDomain::PostProcess: requiredOutput = "PostProcessOutput"; break;
    case ShaderDomain::Particle: requiredOutput = "ParticleOutput"; break;
    case ShaderDomain::Unlit: requiredOutput = "UnlitOutput"; break;
    }
    bool hasOutput = false;
    static const std::unordered_set<std::string> particleNodes = {
        "ParticleColor", "ParticleAge", "NormalizedLifetime", "ParticleVelocity",
        "ParticleSize", "ParticleRotation", "ParticleFrame", "TrailCoordinates", "SoftDepth"
    };
    static const std::unordered_set<std::string> postProcessNodes = {
        "SceneColor", "SceneDepth", "SceneNormal", "SceneVelocity",
        "SceneColorSample", "SceneDepthSample", "SceneNormalSample", "SceneVelocitySample",
        "ScreenUV", "PixelSize"
    };
    static const std::unordered_set<std::string> widgetNodes = {
        "WidgetUV", "WidgetColor", "WidgetTexture", "ClipMask", "SignedDistance"
    };
    for (const ShaderGraphNode& node : asset.nodes) {
        if (node.id == 0 || !nodeIds.insert(node.id).second)
            error("Node IDs must be non-zero and unique.", node.id);
        nodes[node.id] = &node;
        if (node.type.empty()) error("Node type cannot be empty.", node.id);
        if (!IsFinite(node.x) || !IsFinite(node.y))
            error("Node position must be finite.", node.id);
        if (node.type == requiredOutput)
            hasOutput = true;
        if (particleNodes.count(node.type) != 0 && asset.domain != ShaderDomain::Particle)
            error("Particle input node is only valid in Particle graphs.", node.id);
        if (postProcessNodes.count(node.type) != 0 && asset.domain != ShaderDomain::PostProcess)
            error("Scene input node is only valid in Post Process graphs.", node.id);
        if (widgetNodes.count(node.type) != 0 && asset.domain != ShaderDomain::Unlit)
            error("Widget input node is only valid in Unlit/UI graphs.", node.id);
    }
    if (!hasOutput) error(std::string(ShaderDomainName(asset.domain)) + " graph has no domain output node.");

    std::unordered_set<std::uint64_t> pinIds;
    std::unordered_map<std::uint64_t, const ShaderGraphPin*> pins;
    for (const ShaderGraphPin& pin : asset.pins) {
        if (pin.id == 0 || !pinIds.insert(pin.id).second)
            error("Pin IDs must be non-zero and unique.", pin.nodeId);
        pins[pin.id] = &pin;
        if (nodes.find(pin.nodeId) == nodes.end())
            error("Pin references a missing node.", pin.nodeId);
        if (static_cast<int>(pin.type) < static_cast<int>(ShaderValueType::Float)
            || static_cast<int>(pin.type) > static_cast<int>(ShaderValueType::Texture2D))
            error("Pin value type is invalid.", pin.nodeId);
        if (pin.name.empty()) warning("Pin has no display name.", pin.nodeId);
    }

    std::unordered_set<std::uint64_t> linkIds;
    std::unordered_set<std::uint64_t> connectedInputs;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> adjacency;
    for (const ShaderGraphLink& link : asset.links) {
        if (link.id == 0 || !linkIds.insert(link.id).second)
            error("Link IDs must be non-zero and unique.");
        const auto from = pins.find(link.fromPin);
        const auto to = pins.find(link.toPin);
        if (from == pins.end() || to == pins.end()) {
            error("Link references a missing pin.");
            continue;
        }
        if (from->second->input || !to->second->input) {
            error("Links must connect an output pin to an input pin.", to->second->nodeId);
            continue;
        }
        if (!ShaderValueTypesCompatible(from->second->type, to->second->type))
            error(std::string("Cannot connect ") + ShaderValueTypeName(from->second->type)
                + " to " + ShaderValueTypeName(to->second->type) + '.', to->second->nodeId);
        if (!connectedInputs.insert(to->second->id).second)
            error("An input pin cannot have more than one connection.", to->second->nodeId);
        adjacency[from->second->nodeId].push_back(to->second->nodeId);
    }
    for (const ShaderGraphPin& pin : asset.pins)
        if (pin.input && pin.required && connectedInputs.find(pin.id) == connectedInputs.end())
            error("Required input '" + pin.name + "' is not connected.", pin.nodeId);

    enum class Visit { None, Active, Complete };
    std::unordered_map<std::uint64_t, Visit> visits;
    bool cycleReported = false;
    const auto visit = [&](const auto& self, std::uint64_t node) -> void {
        if (visits[node] == Visit::Complete || cycleReported) return;
        if (visits[node] == Visit::Active) {
            error("Shader graphs cannot contain cycles.", node);
            cycleReported = true;
            return;
        }
        visits[node] = Visit::Active;
        for (std::uint64_t next : adjacency[node]) self(self, next);
        visits[node] = Visit::Complete;
    };
    for (const auto& entry : nodes) visit(visit, entry.first);

    std::unordered_set<std::uint64_t> parameterIds;
    std::unordered_set<std::string> parameterNames;
    std::size_t textureCount = 0;
    for (const ShaderParameter& parameter : asset.parameters) {
        if (parameter.id == 0 || !parameterIds.insert(parameter.id).second)
            error("Parameter IDs must be non-zero and unique.");
        if (parameter.name.empty() || !parameterNames.insert(parameter.name).second)
            error("Parameter names must be non-empty and unique.");
        if (static_cast<int>(parameter.type) < static_cast<int>(ShaderValueType::Float)
            || static_cast<int>(parameter.type) > static_cast<int>(ShaderValueType::Texture2D))
            error("Parameter value type is invalid.");
        if (parameter.type == ShaderValueType::Texture2D) ++textureCount;
    }
    if (textureCount > kMaximumTextures) error("Shader exposes more than 16 textures.");
    return issues;
}

bool ShaderAssetHasErrors(const std::vector<ShaderAssetIssue>& issues) {
    return std::any_of(issues.begin(), issues.end(), [](const ShaderAssetIssue& issue) {
        return issue.severity == ShaderAssetIssue::Severity::Error;
    });
}

std::uint64_t HashShaderAsset(const ShaderAsset& asset) {
    const std::string canonical = CanonicalShaderAsset(asset);
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char byte : canonical) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

bool SaveShaderAsset(const std::string& path, const ShaderAsset& asset, std::string* error) {
    const auto issues = ValidateShaderAsset(asset);
    if (ShaderAssetHasErrors(issues)) {
        if (error) {
            const auto firstError = std::find_if(
                issues.begin(), issues.end(), [](const ShaderAssetIssue& issue) {
                    return issue.severity == ShaderAssetIssue::Severity::Error;
                });
            *error = firstError == issues.end()
                ? "Shader asset validation failed." : firstError->message;
        }
        return false;
    }
    std::error_code ec;
    const std::filesystem::path target(path);
    if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) {
        if (error) *error = "Could not create shader asset folder: " + ec.message();
        return false;
    }
    std::ofstream out(path);
    if (!out) {
        if (error) *error = "Could not open shader asset for writing.";
        return false;
    }
    out << "3DGShader " << ShaderAsset::CurrentVersion << '\n'
        << "asset " << asset.id << ' ' << std::quoted(asset.name) << ' '
        << static_cast<int>(asset.domain) << ' ' << asset.blendMode << '\n';
    for (const ShaderGraphNode& node : asset.nodes) {
        out << "node " << node.id << ' ' << std::quoted(node.type) << ' '
            << std::quoted(node.name) << ' ' << node.x << ' ' << node.y << '\n';
        out << "node_meta " << node.id << ' ' << std::quoted(node.comment) << ' '
            << node.groupId << ' ' << (node.collapsed ? 1 : 0) << ' '
            << std::quoted(node.value) << '\n';
    }
    for (const ShaderGraphPin& pin : asset.pins)
        out << "pin " << pin.id << ' ' << pin.nodeId << ' ' << std::quoted(pin.name)
            << ' ' << static_cast<int>(pin.type) << ' ' << (pin.input ? 1 : 0)
            << ' ' << (pin.required ? 1 : 0) << '\n';
    for (const ShaderGraphLink& link : asset.links)
        out << "link " << link.id << ' ' << link.fromPin << ' ' << link.toPin << '\n';
    for (const ShaderParameter& parameter : asset.parameters)
        out << "parameter " << parameter.id << ' ' << std::quoted(parameter.name)
            << ' ' << static_cast<int>(parameter.type) << ' '
            << std::quoted(parameter.defaultValue) << '\n';
    if (!out) {
        if (error) *error = "Could not finish writing shader asset.";
        return false;
    }
    if (error) error->clear();
    return true;
}

bool LoadShaderAsset(const std::string& path, ShaderAsset* output, std::string* error) {
    if (!output) {
        if (error) *error = "Shader asset output is null.";
        return false;
    }
    std::ifstream in(path);
    std::string magic;
    int version = 0;
    if (!(in >> magic >> version) || magic != "3DGShader"
        || version < 1 || version > ShaderAsset::CurrentVersion) {
        if (error) *error = "Unsupported or malformed shader asset: " + path;
        return false;
    }
    ShaderAsset asset;
    asset.version = version;
    std::string record;
    while (in >> record) {
        if (record == "asset") {
            int domain = 0;
            in >> asset.id >> std::quoted(asset.name) >> domain >> asset.blendMode;
            asset.domain = static_cast<ShaderDomain>(std::clamp(domain, 0, 3));
        } else if (record == "node") {
            ShaderGraphNode node;
            in >> node.id >> std::quoted(node.type) >> std::quoted(node.name) >> node.x >> node.y;
            asset.nodes.push_back(std::move(node));
        } else if (record == "pin") {
            ShaderGraphPin pin;
            int type = 0, input = 0, required = 0;
            in >> pin.id >> pin.nodeId >> std::quoted(pin.name) >> type >> input >> required;
            pin.type = static_cast<ShaderValueType>(std::clamp(type, 0, 7));
            pin.input = input != 0;
            pin.required = required != 0;
            asset.pins.push_back(std::move(pin));
        } else if (record == "node_meta") {
            std::uint64_t id = 0, groupId = 0;
            int collapsed = 0;
            std::string comment, value;
            in >> id >> std::quoted(comment) >> groupId >> collapsed >> std::quoted(value);
            const auto node = std::find_if(asset.nodes.begin(), asset.nodes.end(),
                [id](const ShaderGraphNode& candidate) { return candidate.id == id; });
            if (node != asset.nodes.end()) {
                node->comment = std::move(comment);
                node->groupId = groupId;
                node->collapsed = collapsed != 0;
                node->value = std::move(value);
            }
        } else if (record == "link") {
            ShaderGraphLink link;
            in >> link.id >> link.fromPin >> link.toPin;
            asset.links.push_back(link);
        } else if (record == "parameter") {
            ShaderParameter parameter;
            int type = 0;
            in >> parameter.id >> std::quoted(parameter.name) >> type
               >> std::quoted(parameter.defaultValue);
            parameter.type = static_cast<ShaderValueType>(std::clamp(type, 0, 7));
            asset.parameters.push_back(std::move(parameter));
        } else {
            std::string ignored;
            std::getline(in, ignored);
        }
    }
    if (!in.eof()) {
        if (error) *error = "Shader asset data is incomplete: " + path;
        return false;
    }
    const auto issues = ValidateShaderAsset(asset);
    if (ShaderAssetHasErrors(issues)) {
        if (error) *error = issues.front().message;
        return false;
    }
    *output = std::move(asset);
    if (error) error->clear();
    return true;
}

std::pair<std::string, std::string> ShaderFallbackSources(ShaderDomain domain) {
    if (domain == ShaderDomain::PostProcess) {
        return {
            "#version 330 core\nlayout(location=0) in vec2 aPos;"
            "out vec2 vUV;void main(){vUV=aPos*0.5+0.5;gl_Position=vec4(aPos,0,1);}",
            "#version 330 core\nin vec2 vUV;out vec4 FragColor;"
            "void main(){FragColor=vec4(1.0,0.0,1.0,1.0);}"
        };
    }
    return {
        "#version 330 core\nlayout(location=0) in vec3 aPos;"
        "uniform mat4 uModel;uniform mat4 uViewProj;"
        "void main(){gl_Position=uViewProj*uModel*vec4(aPos,1.0);}",
        "#version 330 core\nout vec4 FragColor;"
        "void main(){FragColor=vec4(1.0,0.0,1.0,1.0);}"
    };
}

} // namespace engine
