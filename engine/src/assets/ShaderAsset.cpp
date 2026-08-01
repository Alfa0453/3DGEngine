#include "engine/assets/ShaderAsset.h"

#include "engine/assets/AssetReference.h"
#include "engine/assets/AssetRegistry.h"

#include <algorithm>
#include <cctype>
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
            << static_cast<int>(parameter.type) << ':' << parameter.defaultValue
            << ':' << parameter.group << ':' << parameter.tooltip << ':'
            << parameter.useRange << ':' << parameter.minValue << ':'
            << parameter.maxValue << ':' << parameter.step << ':'
            << parameter.materialVisible;
    return out.str();
}

bool IsFinite(float value) {
    return std::isfinite(value);
}

std::size_t NumericComponentCount(std::string value) {
    if (const std::size_t open = value.find('('); open != std::string::npos)
        value.erase(0, open + 1);
    for (char& c : value)
        if (c == ',' || c == '(' || c == ')') c = ' ';
    std::istringstream input(value);
    std::size_t count = 0;
    float number = 0.0f;
    while (input >> number) ++count;
    return count;
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

std::string ShaderParameterUniformName(const std::string& parameterName) {
    std::string safe = parameterName;
    for (char& c : safe)
        if (!(c == '_' || (c >= '0' && c <= '9')
              || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')))
            c = '_';
    if (safe.empty() || (safe.front() >= '0' && safe.front() <= '9'))
        safe = "p_" + safe;
    return "u_" + safe;
}

const char* ShaderDomainOutputNodeType(ShaderDomain domain) {
    switch (domain) {
    case ShaderDomain::Surface: return "SurfaceOutput";
    case ShaderDomain::PostProcess: return "PostProcessOutput";
    case ShaderDomain::Particle: return "ParticleOutput";
    case ShaderDomain::Unlit: return "UnlitOutput";
    }
    return "SurfaceOutput";
}

bool ConvertShaderAssetDomain(ShaderAsset& asset, ShaderDomain domain,
                              ShaderDomainConversionReport* outputReport) {
    ShaderDomainConversionReport report;
    report.from = asset.domain;
    report.to = domain;
    if (domain == asset.domain) {
        report.success = true;
        if (outputReport) *outputReport = report;
        return true;
    }

    auto output = std::find_if(
        asset.nodes.begin(), asset.nodes.end(),
        [&](const ShaderGraphNode& node) {
            return node.type == ShaderDomainOutputNodeType(asset.domain);
        });
    if (output == asset.nodes.end()) {
        output = std::find_if(
            asset.nodes.begin(), asset.nodes.end(),
            [](const ShaderGraphNode& node) {
                return node.type.find("Output") != std::string::npos;
            });
    }
    if (output == asset.nodes.end()) {
        report.error = "The shader graph has no output node.";
        if (outputReport) *outputReport = report;
        return false;
    }

    const std::uint64_t outputId = output->id;
    std::unordered_map<std::string, std::uint64_t> oldConnections;
    std::unordered_set<std::uint64_t> removedPins;
    for (const ShaderGraphPin& pin : asset.pins) {
        if (pin.nodeId != outputId) continue;
        removedPins.insert(pin.id);
        const auto link = std::find_if(
            asset.links.begin(), asset.links.end(),
            [&](const ShaderGraphLink& item) { return item.toPin == pin.id; });
        if (link != asset.links.end()) oldConnections[pin.name] = link->fromPin;
    }

    static const std::unordered_set<std::string> particleNodes = {
        "ParticleColor", "ParticleAge", "NormalizedLifetime",
        "ParticleVelocity", "ParticleSize", "ParticleRotation",
        "ParticleFrame", "TrailCoordinates", "SoftDepth", "ParticleOutput"
    };
    static const std::unordered_set<std::string> postNodes = {
        "ScreenUV", "SceneColor", "SceneDepth", "SceneNormal",
        "SceneVelocity", "TexelSize", "Exposure", "SceneColorSample",
        "SceneDepthSample", "SceneNormalSample", "SceneVelocitySample",
        "PixelOffset", "PostProcessOutput"
    };
    static const std::unordered_set<std::string> unlitNodes = {
        "WidgetUV", "WidgetColor", "WidgetTexture", "ClipMask",
        "SignedDistance", "UnlitOutput"
    };
    const auto exclusiveDomain = [&](const std::string& type) {
        if (particleNodes.count(type)) return static_cast<int>(ShaderDomain::Particle);
        if (postNodes.count(type)) return static_cast<int>(ShaderDomain::PostProcess);
        if (unlitNodes.count(type)) return static_cast<int>(ShaderDomain::Unlit);
        if (type == "SurfaceOutput") return static_cast<int>(ShaderDomain::Surface);
        return -1;
    };

    std::unordered_set<std::uint64_t> removedNodes;
    for (const ShaderGraphNode& node : asset.nodes) {
        if (node.id == outputId) continue;
        const int exclusive = exclusiveDomain(node.type);
        if ((exclusive >= 0 && exclusive != static_cast<int>(domain))
            || node.type.find("Output") != std::string::npos)
            removedNodes.insert(node.id);
    }
    for (const ShaderGraphPin& pin : asset.pins)
        if (removedNodes.count(pin.nodeId)) removedPins.insert(pin.id);

    const std::size_t linksBefore = asset.links.size();
    asset.links.erase(std::remove_if(
        asset.links.begin(), asset.links.end(),
        [&](const ShaderGraphLink& link) {
            return removedPins.count(link.fromPin) || removedPins.count(link.toPin);
        }), asset.links.end());
    asset.pins.erase(std::remove_if(
        asset.pins.begin(), asset.pins.end(),
        [&](const ShaderGraphPin& pin) { return removedPins.count(pin.id); }),
        asset.pins.end());
    asset.nodes.erase(std::remove_if(
        asset.nodes.begin(), asset.nodes.end(),
        [&](const ShaderGraphNode& node) { return removedNodes.count(node.id); }),
        asset.nodes.end());

    output = std::find_if(
        asset.nodes.begin(), asset.nodes.end(),
        [&](const ShaderGraphNode& node) { return node.id == outputId; });
    output->type = ShaderDomainOutputNodeType(domain);
    output->name = std::string(ShaderDomainName(domain)) + " Output";
    asset.domain = domain;

    std::uint64_t next = asset.id + 1;
    for (const auto& node : asset.nodes) next = std::max(next, node.id + 1);
    for (const auto& pin : asset.pins) next = std::max(next, pin.id + 1);
    for (const auto& link : asset.links) next = std::max(next, link.id + 1);
    for (const auto& parameter : asset.parameters)
        next = std::max(next, parameter.id + 1);

    struct PinDefinition { const char* name; ShaderValueType type; };
    static const std::vector<PinDefinition> surfacePins = {
        {"Base Color", ShaderValueType::Color}, {"Emissive", ShaderValueType::Color},
        {"Roughness", ShaderValueType::Float}, {"Metallic", ShaderValueType::Float},
        {"Normal", ShaderValueType::Vec3}, {"Opacity", ShaderValueType::Float},
        {"Alpha Cutoff", ShaderValueType::Float}, {"Clearcoat", ShaderValueType::Float},
        {"Transmission", ShaderValueType::Float}, {"Subsurface", ShaderValueType::Float},
        {"Sheen", ShaderValueType::Float}, {"Anisotropy", ShaderValueType::Float},
        {"Displacement", ShaderValueType::Float}
    };
    static const std::vector<PinDefinition> colorPins = {
        {"Color", ShaderValueType::Color}
    };
    const auto& definitions = domain == ShaderDomain::Surface
        ? surfacePins : colorPins;
    for (const PinDefinition& definition : definitions) {
        const std::uint64_t pinId = next++;
        asset.pins.push_back({pinId, outputId, definition.name,
                              definition.type, true, false});
        std::string oldName = definition.name;
        if (definition.name == std::string("Base Color")
            && report.from != ShaderDomain::Surface)
            oldName = "Color";
        else if (definition.name == std::string("Color")
                 && report.from == ShaderDomain::Surface)
            oldName = "Base Color";
        const auto oldConnection = oldConnections.find(oldName);
        if (oldConnection == oldConnections.end()) continue;
        const auto source = std::find_if(
            asset.pins.begin(), asset.pins.end(),
            [&](const ShaderGraphPin& pin) {
                return pin.id == oldConnection->second;
            });
        if (source == asset.pins.end()
            || !ShaderValueTypesCompatible(source->type, definition.type))
            continue;
        asset.links.push_back({next++, source->id, pinId});
        ++report.preservedOutputLinks;
    }
    report.removedNodes = removedNodes.size();
    // A semantically preserved output is rebuilt with a new pin/link ID, so it
    // must not be reported as lost merely because the old serialized link was
    // replaced.
    report.removedLinks = linksBefore >= asset.links.size()
        ? linksBefore - asset.links.size() : 0;
    report.success = true;
    if (outputReport) *outputReport = report;
    return true;
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
    std::unordered_set<std::string> parameterUniforms;
    static const std::unordered_set<std::string> reservedUniforms = {
        "u_ViewProjection", "u_Model", "u_Bones", "u_CameraPosition",
        "u_Time", "u_DeltaTime", "u_LightDirection", "u_LightIntensity",
        "u_ObjectColor"
    };
    std::size_t textureCount = 0;
    for (const ShaderParameter& parameter : asset.parameters) {
        if (parameter.id == 0 || !parameterIds.insert(parameter.id).second)
            error("Parameter IDs must be non-zero and unique.");
        if (parameter.name.empty() || !parameterNames.insert(parameter.name).second)
            error("Parameter names must be non-empty and unique.");
        if (!parameter.name.empty()) {
            const std::string uniform =
                ShaderParameterUniformName(parameter.name);
            if (!parameterUniforms.insert(uniform).second)
                error("Parameter names must map to unique shader uniforms.");
            if (reservedUniforms.count(uniform))
                error("Parameter name conflicts with an engine shader uniform.");
        }
        if (static_cast<int>(parameter.type) < static_cast<int>(ShaderValueType::Float)
            || static_cast<int>(parameter.type) > static_cast<int>(ShaderValueType::Texture2D))
            error("Parameter value type is invalid.");
        if (parameter.type == ShaderValueType::Texture2D) ++textureCount;
        if (parameter.type == ShaderValueType::Bool) {
            std::string value = parameter.defaultValue;
            std::transform(value.begin(), value.end(), value.begin(),
                [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
            if (value != "true" && value != "false" && value != "0"
                && value != "1")
                warning("Boolean parameter default should be true or false.");
        } else if (parameter.type != ShaderValueType::Texture2D) {
            const std::size_t required =
                parameter.type == ShaderValueType::Vec2 ? 2
                : parameter.type == ShaderValueType::Vec3 ? 3
                : parameter.type == ShaderValueType::Vec4
                    || parameter.type == ShaderValueType::Color ? 4 : 1;
            if (NumericComponentCount(parameter.defaultValue) < required)
                warning("Parameter default has fewer numeric components than its type.");
        }
        if (parameter.useRange) {
            if (!IsFinite(parameter.minValue) || !IsFinite(parameter.maxValue)
                || !IsFinite(parameter.step))
                error("Parameter range values must be finite.");
            else if (parameter.minValue > parameter.maxValue)
                error("Parameter range minimum cannot exceed its maximum.");
            else if (parameter.step <= 0.0f)
                error("Parameter range step must be greater than zero.");
        }
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

bool SaveShaderAsset(const std::string& path, ShaderAsset& asset, std::string* error) {
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
    if (!asset.assetId.Valid() && std::filesystem::is_regular_file(target, ec)) {
        ShaderAsset existing;
        std::string ignored;
        if (LoadShaderAsset(path, &existing, &ignored)
            && existing.assetId.Valid())
            asset.assetId = existing.assetId;
    }
    if (!asset.assetId.Valid()) asset.assetId = AssetHandle::Generate();
    asset.version = ShaderAsset::CurrentVersion;

    const std::string contentRoot = FindContentRootForAsset(path);
    AssetRegistry registry;
    std::string registryError;
    const std::string registryPath = contentRoot.empty()
        ? std::string()
        : AssetRegistry::DefaultRegistryPath(contentRoot);
    if (!registryPath.empty() && std::filesystem::exists(registryPath, ec)
        && !registry.Load(registryPath, &registryError)) {
        if (error) *error = "The asset registry could not be loaded: "
            + registryError;
        return false;
    }
    std::vector<AssetHandle> dependencies;
    for (ShaderParameter& parameter : asset.parameters) {
        if (parameter.type != ShaderValueType::Texture2D
            || parameter.defaultValue.empty()
            || parameter.defaultValue == "0") {
            parameter.assetId = {};
            continue;
        }
        const AssetReference reference = MakeAssetReference(
            &registry, contentRoot, parameter.defaultValue, AssetType::Texture);
        if (reference.id.Valid()) parameter.assetId = reference.id;
        if (parameter.assetId.Valid()
            && std::find(dependencies.begin(), dependencies.end(),
                         parameter.assetId) == dependencies.end())
            dependencies.push_back(parameter.assetId);
    }

    const std::filesystem::path temporary = target.string() + ".tmp";
    std::ofstream out(temporary);
    if (!out) {
        if (error) *error = "Could not open shader asset for writing.";
        return false;
    }
    out << "3DG_SHADER " << ShaderAsset::CurrentVersion << ' '
        << asset.assetId.ToString() << '\n'
        << "graph " << asset.id << ' ' << std::quoted(asset.name) << ' '
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
    for (const ShaderParameter& parameter : asset.parameters) {
        out << "parameter " << parameter.id << ' ' << std::quoted(parameter.name)
            << ' ' << static_cast<int>(parameter.type) << ' '
            << std::quoted(parameter.defaultValue) << ' '
            << (parameter.assetId.Valid()
                    ? parameter.assetId.ToString() : std::string("-"))
            << '\n';
        out << "parameter_meta " << parameter.id << ' '
            << std::quoted(parameter.group) << ' '
            << std::quoted(parameter.tooltip) << ' '
            << (parameter.useRange ? 1 : 0) << ' '
            << parameter.minValue << ' ' << parameter.maxValue << ' '
            << parameter.step << ' '
            << (parameter.materialVisible ? 1 : 0) << '\n';
    }
    out << "ASSET_DEPS " << dependencies.size();
    for (AssetHandle dependency : dependencies)
        out << ' ' << dependency.ToString();
    out << '\n';
    if (!out) {
        out.close();
        std::filesystem::remove(temporary, ec);
        if (error) *error = "Could not finish writing shader asset.";
        return false;
    }
    out.close();

    const std::filesystem::path backup = target.string() + ".bak";
    std::filesystem::remove(backup, ec);
    ec.clear();
    if (std::filesystem::exists(target, ec)) {
        std::filesystem::rename(target, backup, ec);
        if (ec) {
            std::filesystem::remove(temporary);
            if (error) *error = "Could not replace shader asset: " + ec.message();
            return false;
        }
    }
    ec.clear();
    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        std::error_code rollback;
        if (std::filesystem::exists(backup, rollback))
            std::filesystem::rename(backup, target, rollback);
        if (error) *error = "Could not commit shader asset: " + ec.message();
        return false;
    }
    std::filesystem::remove(backup, ec);

    if (!contentRoot.empty()) {
        ec.clear();
        AssetRegistryEntry entry;
        entry.id = asset.assetId;
        entry.type = AssetType::Shader;
        entry.virtualPath = AssetRegistry::NormalizeVirtualPath(
            std::filesystem::relative(target, contentRoot, ec).generic_string());
        entry.sourceHash = HashShaderAsset(asset);
        entry.importerVersion = 1;
        entry.dependencies = dependencies;
        if (ec || !registry.Register(std::move(entry), &registryError)
            || !registry.Save(registryPath, &registryError)) {
            if (error) *error = "Shader saved, but registration failed: "
                + (ec ? ec.message() : registryError);
            return false;
        }
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
    if (!(in >> magic >> version)
        || (magic != "3DGShader" && magic != "3DG_SHADER")
        || version < 1 || version > ShaderAsset::CurrentVersion) {
        if (error) *error = "Unsupported or malformed shader asset: " + path;
        return false;
    }
    ShaderAsset asset;
    asset.version = version;
    if (magic == "3DG_SHADER") {
        std::string assetId;
        if (!(in >> assetId) || !AssetHandle::Parse(assetId, &asset.assetId)) {
            if (error) *error = "Shader asset has an invalid stable ID: " + path;
            return false;
        }
    }
    std::string record;
    while (in >> record) {
        if (record == "asset" || record == "graph") {
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
            if (version >= 3) {
                std::string assetId;
                in >> assetId;
                if (assetId != "-"
                    && !AssetHandle::Parse(assetId, &parameter.assetId)) {
                    if (error) *error =
                        "Shader texture parameter has an invalid asset ID.";
                    return false;
                }
            }
            asset.parameters.push_back(std::move(parameter));
        } else if (record == "parameter_meta") {
            std::uint64_t id = 0;
            int useRange = 0, materialVisible = 1;
            std::string group, tooltip;
            float minValue = 0.0f, maxValue = 1.0f, step = 0.01f;
            in >> id >> std::quoted(group) >> std::quoted(tooltip)
               >> useRange >> minValue >> maxValue >> step >> materialVisible;
            const auto parameter = std::find_if(
                asset.parameters.begin(), asset.parameters.end(),
                [id](const ShaderParameter& candidate) {
                    return candidate.id == id;
                });
            if (parameter != asset.parameters.end()) {
                parameter->group = group.empty() ? "General" : std::move(group);
                parameter->tooltip = std::move(tooltip);
                parameter->useRange = useRange != 0;
                parameter->minValue = minValue;
                parameter->maxValue = maxValue;
                parameter->step = step;
                parameter->materialVisible = materialVisible != 0;
            }
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
    const std::string contentRoot = FindContentRootForAsset(path);
    if (!contentRoot.empty()) {
        AssetRegistry registry;
        std::string ignored;
        registry.Load(AssetRegistry::DefaultRegistryPath(contentRoot), &ignored);
        for (ShaderParameter& parameter : asset.parameters) {
            if (parameter.type != ShaderValueType::Texture2D) continue;
            const std::string resolved = ResolveAssetReference(
                &registry, contentRoot,
                {parameter.assetId, parameter.defaultValue},
                AssetType::Texture);
            if (!resolved.empty()) parameter.defaultValue = resolved;
        }
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
