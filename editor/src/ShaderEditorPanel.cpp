#include "ShaderEditorPanel.h"

#include "EditorAssets.h"

#include <engine/graphics/Primitives.h>
#include <engine/graphics/VertexLayout.h>
#include <engine/assets/ShaderGraphCompiler.h>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace {

struct NodeTemplate {
    const char* category;
    const char* type;
    engine::ShaderValueType output;
    int inputs;
    engine::ShaderValueType inputType;
};

constexpr NodeTemplate kNodeTemplates[] = {
    {"Inputs","UV",engine::ShaderValueType::Vec2,0,engine::ShaderValueType::Float},
    {"Inputs","WorldPosition",engine::ShaderValueType::Vec3,0,engine::ShaderValueType::Float},
    {"Inputs","LocalPosition",engine::ShaderValueType::Vec3,0,engine::ShaderValueType::Float},
    {"Inputs","Normal",engine::ShaderValueType::Vec3,0,engine::ShaderValueType::Float},
    {"Inputs","Tangent",engine::ShaderValueType::Vec3,0,engine::ShaderValueType::Float},
    {"Inputs","ViewDirection",engine::ShaderValueType::Vec3,0,engine::ShaderValueType::Float},
    {"Inputs","CameraPosition",engine::ShaderValueType::Vec3,0,engine::ShaderValueType::Float},
    {"Inputs","ObjectColor",engine::ShaderValueType::Color,0,engine::ShaderValueType::Float},
    {"Inputs","VertexColor",engine::ShaderValueType::Color,0,engine::ShaderValueType::Float},
    {"Inputs","Time",engine::ShaderValueType::Float,0,engine::ShaderValueType::Float},
    {"Inputs","DeltaTime",engine::ShaderValueType::Float,0,engine::ShaderValueType::Float},
    {"Post Process","ScreenUV",engine::ShaderValueType::Vec2,0,engine::ShaderValueType::Float},
    {"Post Process","SceneColor",engine::ShaderValueType::Color,0,engine::ShaderValueType::Float},
    {"Post Process","SceneDepth",engine::ShaderValueType::Float,0,engine::ShaderValueType::Float},
    {"Post Process","SceneNormal",engine::ShaderValueType::Vec3,0,engine::ShaderValueType::Float},
    {"Post Process","SceneVelocity",engine::ShaderValueType::Vec2,0,engine::ShaderValueType::Float},
    {"Post Process","TexelSize",engine::ShaderValueType::Vec2,0,engine::ShaderValueType::Float},
    {"Post Process","Exposure",engine::ShaderValueType::Float,0,engine::ShaderValueType::Float},
    {"Post Process","SceneColorSample",engine::ShaderValueType::Color,1,engine::ShaderValueType::Vec2},
    {"Post Process","SceneDepthSample",engine::ShaderValueType::Float,1,engine::ShaderValueType::Vec2},
    {"Post Process","SceneNormalSample",engine::ShaderValueType::Vec3,1,engine::ShaderValueType::Vec2},
    {"Post Process","SceneVelocitySample",engine::ShaderValueType::Vec2,1,engine::ShaderValueType::Vec2},
    {"Post Process","PixelOffset",engine::ShaderValueType::Vec2,2,engine::ShaderValueType::Vec2},
    {"Particle","ParticleColor",engine::ShaderValueType::Color,0,engine::ShaderValueType::Float},
    {"Particle","ParticleAge",engine::ShaderValueType::Float,0,engine::ShaderValueType::Float},
    {"Particle","NormalizedLifetime",engine::ShaderValueType::Float,0,engine::ShaderValueType::Float},
    {"Particle","ParticleVelocity",engine::ShaderValueType::Vec3,0,engine::ShaderValueType::Float},
    {"Particle","ParticleSize",engine::ShaderValueType::Float,0,engine::ShaderValueType::Float},
    {"Particle","ParticleRotation",engine::ShaderValueType::Float,0,engine::ShaderValueType::Float},
    {"Particle","ParticleFrame",engine::ShaderValueType::Float,0,engine::ShaderValueType::Float},
    {"Particle","TrailCoordinates",engine::ShaderValueType::Vec2,0,engine::ShaderValueType::Float},
    {"Particle","SoftDepth",engine::ShaderValueType::Float,0,engine::ShaderValueType::Float},
    {"Unlit / UI","WidgetUV",engine::ShaderValueType::Vec2,0,engine::ShaderValueType::Float},
    {"Unlit / UI","WidgetColor",engine::ShaderValueType::Color,0,engine::ShaderValueType::Float},
    {"Unlit / UI","WidgetTexture",engine::ShaderValueType::Color,0,engine::ShaderValueType::Float},
    {"Unlit / UI","ClipMask",engine::ShaderValueType::Float,0,engine::ShaderValueType::Float},
    {"Unlit / UI","SignedDistance",engine::ShaderValueType::Float,1,engine::ShaderValueType::Vec2},
    {"Parameters","ParameterFloat",engine::ShaderValueType::Float,0,engine::ShaderValueType::Float},
    {"Parameters","ParameterInt",engine::ShaderValueType::Int,0,engine::ShaderValueType::Float},
    {"Parameters","ParameterBool",engine::ShaderValueType::Bool,0,engine::ShaderValueType::Float},
    {"Parameters","ParameterVec2",engine::ShaderValueType::Vec2,0,engine::ShaderValueType::Float},
    {"Parameters","ParameterVec3",engine::ShaderValueType::Vec3,0,engine::ShaderValueType::Float},
    {"Parameters","ParameterVec4",engine::ShaderValueType::Vec4,0,engine::ShaderValueType::Float},
    {"Parameters","ParameterColor",engine::ShaderValueType::Color,0,engine::ShaderValueType::Float},
    {"Parameters","ParameterTexture2D",engine::ShaderValueType::Texture2D,0,engine::ShaderValueType::Float},
    {"Constants","ConstantFloat",engine::ShaderValueType::Float,0,engine::ShaderValueType::Float},
    {"Constants","ConstantVec2",engine::ShaderValueType::Vec2,0,engine::ShaderValueType::Float},
    {"Constants","ConstantColor",engine::ShaderValueType::Color,0,engine::ShaderValueType::Float},
    {"Math","Add",engine::ShaderValueType::Float,2,engine::ShaderValueType::Float},
    {"Math","Subtract",engine::ShaderValueType::Float,2,engine::ShaderValueType::Float},
    {"Math","Multiply",engine::ShaderValueType::Float,2,engine::ShaderValueType::Float},
    {"Math","Divide",engine::ShaderValueType::Float,2,engine::ShaderValueType::Float},
    {"Math","Min",engine::ShaderValueType::Float,2,engine::ShaderValueType::Float},
    {"Math","Max",engine::ShaderValueType::Float,2,engine::ShaderValueType::Float},
    {"Math","Clamp",engine::ShaderValueType::Float,3,engine::ShaderValueType::Float},
    {"Math","Saturate",engine::ShaderValueType::Float,1,engine::ShaderValueType::Float},
    {"Math","Power",engine::ShaderValueType::Float,2,engine::ShaderValueType::Float},
    {"Math","SquareRoot",engine::ShaderValueType::Float,1,engine::ShaderValueType::Float},
    {"Math","Absolute",engine::ShaderValueType::Float,1,engine::ShaderValueType::Float},
    {"Math","Sign",engine::ShaderValueType::Float,1,engine::ShaderValueType::Float},
    {"Math","Floor",engine::ShaderValueType::Float,1,engine::ShaderValueType::Float},
    {"Math","Fraction",engine::ShaderValueType::Float,1,engine::ShaderValueType::Float},
    {"Math","Modulo",engine::ShaderValueType::Float,2,engine::ShaderValueType::Float},
    {"Vector","Compose",engine::ShaderValueType::Vec3,3,engine::ShaderValueType::Float},
    {"Vector","Split",engine::ShaderValueType::Float,1,engine::ShaderValueType::Vec3},
    {"Vector","Swizzle",engine::ShaderValueType::Vec3,1,engine::ShaderValueType::Vec3},
    {"Vector","Dot",engine::ShaderValueType::Float,2,engine::ShaderValueType::Vec3},
    {"Vector","Cross",engine::ShaderValueType::Vec3,2,engine::ShaderValueType::Vec3},
    {"Vector","Normalize",engine::ShaderValueType::Vec3,1,engine::ShaderValueType::Vec3},
    {"Vector","Length",engine::ShaderValueType::Float,1,engine::ShaderValueType::Vec3},
    {"Vector","Reflect",engine::ShaderValueType::Vec3,2,engine::ShaderValueType::Vec3},
    {"Vector","Lerp",engine::ShaderValueType::Float,3,engine::ShaderValueType::Float},
    {"Texture","SampleTexture2D",engine::ShaderValueType::Color,2,engine::ShaderValueType::Texture2D},
    {"Texture","NormalMapDecode",engine::ShaderValueType::Vec3,1,engine::ShaderValueType::Color},
    {"Texture","UVTransform",engine::ShaderValueType::Vec2,3,engine::ShaderValueType::Vec2},
    {"Texture","ChannelMask",engine::ShaderValueType::Float,1,engine::ShaderValueType::Color},
    {"Utility","OneMinus",engine::ShaderValueType::Float,1,engine::ShaderValueType::Float},
    {"Utility","Remap",engine::ShaderValueType::Float,5,engine::ShaderValueType::Float},
    {"Utility","Smoothstep",engine::ShaderValueType::Float,3,engine::ShaderValueType::Float},
    {"Utility","Fresnel",engine::ShaderValueType::Float,2,engine::ShaderValueType::Vec3},
    {"Utility","Noise",engine::ShaderValueType::Float,1,engine::ShaderValueType::Vec2},
    {"Utility","Comparison",engine::ShaderValueType::Bool,2,engine::ShaderValueType::Float},
    {"Utility","Select",engine::ShaderValueType::Float,3,engine::ShaderValueType::Float},
};

engine::ShaderAsset DefaultAsset()
{
    engine::ShaderAsset asset;
    asset.id = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    if (asset.id == 0) asset.id = 1;
    asset.name = "NewShader";
    asset.domain = engine::ShaderDomain::Surface;
    asset.nodes.push_back({1, "SurfaceOutput", "Surface Output", 420.0f, 180.0f});
    asset.pins.push_back(
        {2, 1, "Base Color", engine::ShaderValueType::Color, true, false});
    asset.pins.push_back(
        {3, 1, "Emissive", engine::ShaderValueType::Color, true, false});
    asset.pins.push_back(
        {4, 1, "Roughness", engine::ShaderValueType::Float, true, false});
    asset.pins.push_back(
        {5, 1, "Metallic", engine::ShaderValueType::Float, true, false});
    asset.pins.push_back(
        {6, 1, "Normal", engine::ShaderValueType::Vec3, true, false});
    asset.pins.push_back(
        {7, 1, "Opacity", engine::ShaderValueType::Float, true, false});
    asset.pins.push_back(
        {8, 1, "Alpha Cutoff", engine::ShaderValueType::Float, true, false});
    asset.pins.push_back(
        {9, 1, "Clearcoat", engine::ShaderValueType::Float, true, false});
    asset.pins.push_back(
        {10, 1, "Transmission", engine::ShaderValueType::Float, true, false});
    asset.pins.push_back(
        {11, 1, "Subsurface", engine::ShaderValueType::Float, true, false});
    asset.pins.push_back(
        {12, 1, "Sheen", engine::ShaderValueType::Float, true, false});
    asset.pins.push_back(
        {13, 1, "Anisotropy", engine::ShaderValueType::Float, true, false});
    asset.pins.push_back(
        {14, 1, "Displacement", engine::ShaderValueType::Float, true, false});
    return asset;
}

std::uint64_t NextId(const engine::ShaderAsset& asset)
{
    std::uint64_t id = asset.id;
    for (const auto& node : asset.nodes) id = std::max(id, node.id);
    for (const auto& pin : asset.pins) id = std::max(id, pin.id);
    for (const auto& link : asset.links) id = std::max(id, link.id);
    for (const auto& parameter : asset.parameters) id = std::max(id, parameter.id);
    return id + 1;
}

const engine::ShaderGraphPin* FindPin(
    const engine::ShaderAsset& asset, std::uint64_t id)
{
    const auto found = std::find_if(asset.pins.begin(), asset.pins.end(),
        [id](const engine::ShaderGraphPin& pin) { return pin.id == id; });
    return found == asset.pins.end() ? nullptr : &*found;
}

std::filesystem::path UniquePath(std::filesystem::path path)
{
    if (!std::filesystem::exists(path)) return path;
    const auto parent = path.parent_path();
    const std::string stem = path.stem().string();
    const std::string extension = path.extension().string();
    for (int suffix = 2; suffix < 10000; ++suffix)
    {
        path = parent / (stem + "_" + std::to_string(suffix) + extension);
        if (!std::filesystem::exists(path)) return path;
    }
    return parent / (stem + "_copy" + extension);
}

const char* DomainOutput(engine::ShaderDomain domain)
{
    switch (domain)
    {
    case engine::ShaderDomain::Surface: return "SurfaceOutput";
    case engine::ShaderDomain::PostProcess: return "PostProcessOutput";
    case engine::ShaderDomain::Particle: return "ParticleOutput";
    case engine::ShaderDomain::Unlit: return "UnlitOutput";
    }
    return "SurfaceOutput";
}

bool TemplateAllowedForDomain(
    const NodeTemplate& item, engine::ShaderDomain domain)
{
    const std::string_view category(item.category);
    if (category == "Post Process")
        return domain == engine::ShaderDomain::PostProcess;
    if (category == "Particle")
        return domain == engine::ShaderDomain::Particle;
    if (category == "Unlit / UI")
        return domain == engine::ShaderDomain::Unlit;
    return true;
}

engine::ShaderValueType TemplateInputType(
    const NodeTemplate& item, int input)
{
    if (std::string_view(item.type) == "SampleTexture2D" && input == 1)
        return engine::ShaderValueType::Vec2;
    if (std::string_view(item.type) == "Select" && input == 0)
        return engine::ShaderValueType::Bool;
    return item.inputType;
}

bool TemplateCompatibleWithPin(
    const NodeTemplate& item, const engine::ShaderGraphPin& source)
{
    if (source.input)
        return engine::ShaderValueTypesCompatible(item.output, source.type);
    for (int input = 0; input < item.inputs; ++input)
        if (engine::ShaderValueTypesCompatible(
                source.type, TemplateInputType(item, input)))
            return true;
    return false;
}

} // namespace

ShaderEditorPanel::~ShaderEditorPanel()
{
    if (m_previewNormalTexture)
        glDeleteTextures(1, &m_previewNormalTexture);
    if (m_previewVelocityTexture)
        glDeleteTextures(1, &m_previewVelocityTexture);
}

void ShaderEditorPanel::NewDocument()
{
    m_asset = DefaultAsset();
    m_savedAsset = m_asset;
    m_path.clear();
    std::snprintf(m_name.data(), m_name.size(), "%s", m_asset.name.c_str());
    m_dirty = true;
    m_applied = false;
    m_error.clear();
    GenerateSources();
    m_compilePending = true;
}

bool ShaderEditorPanel::OpenDocument(const std::string& path)
{
    engine::ShaderAsset loaded;
    std::string error;
    if (!engine::LoadShaderAsset(path, &loaded, &error))
    {
        m_error = error;
        return false;
    }
    m_asset = std::move(loaded);
    m_savedAsset = m_asset;
    m_path = path;
    std::snprintf(m_name.data(), m_name.size(), "%s", m_asset.name.c_str());
    m_dirty = false;
    m_applied = false;
    m_error.clear();
    GenerateSources();
    m_compilePending = true;
    return true;
}

bool ShaderEditorPanel::SaveDocument(EditorAssets& assets, bool saveAs)
{
    m_asset.name = m_name.data();
    std::filesystem::path path = m_path;
    if (saveAs || path.empty())
    {
        std::string filename = m_asset.name.empty() ? "NewShader" : m_asset.name;
        if (std::filesystem::path(filename).extension() != ".3dgshader")
            filename += ".3dgshader";
        path = std::filesystem::path(assets.RootPath()) /
            assets.CurrentFolder() / filename;
        if (saveAs) path = UniquePath(path);
    }
    std::string error;
    if (!engine::SaveShaderAsset(path.string(), m_asset, &error))
    {
        m_error = error;
        return false;
    }
    m_path = path.string();
    m_savedAsset = m_asset;
    m_dirty = false;
    m_status = "Saved " + path.filename().string();
    assets.Refresh(assets.RootPath(), &error);
    if (!error.empty()) m_error = error;
    return true;
}

bool ShaderEditorPanel::DuplicateDocument(EditorAssets& assets)
{
    const std::string oldName = m_asset.name;
    m_asset.id += 1;
    m_asset.name = oldName + " Copy";
    std::snprintf(m_name.data(), m_name.size(), "%s", m_asset.name.c_str());
    m_path.clear();
    m_dirty = true;
    return SaveDocument(assets, true);
}

void ShaderEditorPanel::RequestOpen(const std::string& path)
{
    if (m_dirty)
    {
        m_pendingOpenPath = path;
        ImGui::OpenPopup("Unsaved Shader Asset");
    }
    else OpenDocument(path);
}

void ShaderEditorPanel::RequestNew()
{
    if (m_dirty)
    {
        m_pendingNew = true;
        ImGui::OpenPopup("Unsaved Shader Asset");
    }
    else NewDocument();
}

void ShaderEditorPanel::GenerateSources()
{
    const engine::GeneratedShaderSource generated =
        engine::GenerateShaderSource(m_asset);
    if (generated.success) {
        m_vertexSource = generated.vertex;
        m_fragmentSource = generated.fragment;
        m_fragmentLineNodes = generated.fragmentLineNodes;
    } else {
        const auto fallback = engine::ShaderFallbackSources(m_asset.domain);
        m_vertexSource = fallback.first;
        m_fragmentSource = fallback.second;
        m_fragmentLineNodes.clear();
    }
}

void ShaderEditorPanel::Compile(bool force)
{
    if (!force && !m_compilePending) return;
    GenerateSources();
    const auto begin = std::chrono::steady_clock::now();
    const bool ok = m_runtime.CompileOrReload(
        m_path.empty() ? "__unsaved_shader__" : m_path,
        engine::ShaderDomainName(m_asset.domain),
        m_asset, m_vertexSource, m_fragmentSource,
        m_path.empty() ? std::vector<std::string>{}
                       : std::vector<std::string>{m_path});
    m_compileMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - begin).count();
    if (ok)
    {
        m_lastValidVertex = m_vertexSource;
        m_lastValidFragment = m_fragmentSource;
        m_status = "Compiled successfully";
        m_error.clear();
    }
    else
    {
        const auto* report = m_runtime.LastReport(
            m_path.empty() ? "__unsaved_shader__" : m_path,
            engine::ShaderDomainName(m_asset.domain));
        m_status = report && !report->diagnostics.empty()
            ? report->diagnostics.front().message : "Compilation failed";
    }
    m_compilePending = false;
}

void ShaderEditorPanel::PushUndo()
{
    m_undo.push_back(m_asset);
    if (m_undo.size() > 64) m_undo.erase(m_undo.begin());
    m_redo.clear();
}

void ShaderEditorPanel::UndoGraph()
{
    if (m_undo.empty()) return;
    m_redo.push_back(m_asset);
    m_asset = std::move(m_undo.back());
    m_undo.pop_back();
    m_selectedNodes.clear();
    m_dirty = true;
    m_compilePending = true;
}

void ShaderEditorPanel::RedoGraph()
{
    if (m_redo.empty()) return;
    m_undo.push_back(m_asset);
    m_asset = std::move(m_redo.back());
    m_redo.pop_back();
    m_selectedNodes.clear();
    m_dirty = true;
    m_compilePending = true;
}

void ShaderEditorPanel::AddGraphNode(const std::string& type, float x, float y)
{
    const auto descriptor = std::find_if(std::begin(kNodeTemplates), std::end(kNodeTemplates),
        [&type](const NodeTemplate& item) { return type == item.type; });
    if (descriptor == std::end(kNodeTemplates)) return;
    PushUndo();
    std::uint64_t id = NextId(m_asset);
    engine::ShaderGraphNode node;
    node.id = id++;
    node.type = descriptor->type;
    node.name = descriptor->type;
    node.x = x;
    node.y = y;
    if (type == "ConstantFloat") node.value = "0.5";
    if (type == "ConstantVec2") node.value = "vec2(1.0,0.0)";
    if (type == "ConstantColor") node.value = "vec4(0.8,0.3,0.1,1.0)";
    if (type.rfind("Parameter", 0) == 0) {
        node.name = "Parameter" + std::to_string(m_asset.parameters.size() + 1);
        engine::ShaderParameter parameter;
        parameter.id = id++;
        parameter.name = node.name;
        parameter.type = descriptor->output;
        parameter.defaultValue = engine::ShaderValueTypeName(descriptor->output);
        if (descriptor->output == engine::ShaderValueType::Float) parameter.defaultValue = "0.5";
        else if (descriptor->output == engine::ShaderValueType::Color)
            parameter.defaultValue = "1,1,1,1";
        m_asset.parameters.push_back(std::move(parameter));
    }
    m_asset.nodes.push_back(node);
    for (int input = 0; input < descriptor->inputs; ++input) {
        engine::ShaderValueType typeValue = descriptor->inputType;
        if (node.type == "SampleTexture2D" && input == 1) typeValue = engine::ShaderValueType::Vec2;
        if (node.type == "Select" && input == 0) typeValue = engine::ShaderValueType::Bool;
        m_asset.pins.push_back({id++, node.id, input == 0 ? "A" :
            input == 1 ? "B" : input == 2 ? "C" : "Input",
            typeValue, true, false});
    }
    m_asset.pins.push_back(
        {id++, node.id, "Result", descriptor->output, false, false});
    m_selectedNodes = {node.id};
    m_dirty = true;
    m_compilePending = true;
}

void ShaderEditorPanel::DeleteSelectedNodes()
{
    if (m_selectedNodes.empty()) return;
    std::unordered_set<std::uint64_t> removable;
    for (const auto& node : m_asset.nodes)
        if (m_selectedNodes.count(node.id)
            && node.type.find("Output") == std::string::npos) removable.insert(node.id);
    if (removable.empty()) return;
    PushUndo();
    std::unordered_set<std::uint64_t> removedPins;
    for (const auto& pin : m_asset.pins)
        if (removable.count(pin.nodeId)) removedPins.insert(pin.id);
    m_asset.links.erase(std::remove_if(m_asset.links.begin(), m_asset.links.end(),
        [&removedPins](const engine::ShaderGraphLink& link) {
            return removedPins.count(link.fromPin) || removedPins.count(link.toPin);
        }), m_asset.links.end());
    m_asset.pins.erase(std::remove_if(m_asset.pins.begin(), m_asset.pins.end(),
        [&removable](const engine::ShaderGraphPin& pin) {
            return removable.count(pin.nodeId) != 0;
        }), m_asset.pins.end());
    m_asset.parameters.erase(std::remove_if(
        m_asset.parameters.begin(), m_asset.parameters.end(),
        [&](const engine::ShaderParameter& parameter) {
            return std::any_of(m_asset.nodes.begin(), m_asset.nodes.end(),
                [&](const engine::ShaderGraphNode& node) {
                    return removable.count(node.id) && node.name == parameter.name
                        && node.type.rfind("Parameter", 0) == 0;
                });
        }), m_asset.parameters.end());
    m_asset.nodes.erase(std::remove_if(m_asset.nodes.begin(), m_asset.nodes.end(),
        [&removable](const engine::ShaderGraphNode& node) {
            return removable.count(node.id) != 0;
        }), m_asset.nodes.end());
    m_selectedNodes.clear();
    m_dirty = true;
    m_compilePending = true;
}

void ShaderEditorPanel::DuplicateSelectedNodes()
{
    if (m_selectedNodes.empty()) return;
    PushUndo();
    std::unordered_map<std::uint64_t, std::uint64_t> nodeIds;
    std::unordered_map<std::uint64_t, std::uint64_t> pinIds;
    std::vector<engine::ShaderGraphNode> nodes;
    std::vector<engine::ShaderGraphPin> pins;
    std::vector<engine::ShaderGraphLink> links;
    std::vector<engine::ShaderParameter> parameters;
    std::uint64_t next = NextId(m_asset);
    for (const auto& node : m_asset.nodes) {
        if (!m_selectedNodes.count(node.id)
            || node.type.find("Output") != std::string::npos) continue;
        auto copy = node;
        nodeIds[node.id] = next;
        copy.id = next++;
        copy.x += 35.0f; copy.y += 35.0f;
        copy.name += " Copy";
        if (node.type.rfind("Parameter", 0) == 0) {
            const auto original = std::find_if(
                m_asset.parameters.begin(), m_asset.parameters.end(),
                [&](const engine::ShaderParameter& parameter) {
                    return parameter.name == node.name;
                });
            if (original != m_asset.parameters.end()) {
                auto parameter = *original;
                parameter.id = next++;
                parameter.name = copy.name;
                parameters.push_back(std::move(parameter));
            }
        }
        nodes.push_back(std::move(copy));
    }
    for (const auto& pin : m_asset.pins) {
        const auto mappedNode = nodeIds.find(pin.nodeId);
        if (mappedNode == nodeIds.end()) continue;
        auto copy = pin;
        pinIds[pin.id] = next;
        copy.id = next++;
        copy.nodeId = mappedNode->second;
        pins.push_back(std::move(copy));
    }
    for (const auto& link : m_asset.links) {
        if (!pinIds.count(link.fromPin) || !pinIds.count(link.toPin)) continue;
        links.push_back({next++, pinIds[link.fromPin], pinIds[link.toPin]});
    }
    m_asset.nodes.insert(m_asset.nodes.end(), nodes.begin(), nodes.end());
    m_asset.pins.insert(m_asset.pins.end(), pins.begin(), pins.end());
    m_asset.links.insert(m_asset.links.end(), links.begin(), links.end());
    m_asset.parameters.insert(
        m_asset.parameters.end(), parameters.begin(), parameters.end());
    m_selectedNodes.clear();
    for (const auto& pair : nodeIds) m_selectedNodes.insert(pair.second);
    m_dirty = true;
    m_compilePending = true;
}

void ShaderEditorPanel::CopySelectedNodes()
{
    m_graphClipboard = engine::ShaderAsset{};
    m_graphClipboard.nodes.clear();
    m_graphClipboard.pins.clear();
    m_graphClipboard.links.clear();
    m_graphClipboard.parameters.clear();
    std::unordered_set<std::uint64_t> copiedPins;
    for (const auto& node : m_asset.nodes)
        if (m_selectedNodes.count(node.id)
            && node.type.find("Output") == std::string::npos)
            m_graphClipboard.nodes.push_back(node);
    for (const auto& pin : m_asset.pins) {
        if (std::any_of(m_graphClipboard.nodes.begin(), m_graphClipboard.nodes.end(),
            [&](const engine::ShaderGraphNode& node) { return node.id == pin.nodeId; })) {
            m_graphClipboard.pins.push_back(pin);
            copiedPins.insert(pin.id);
        }
    }
    for (const auto& link : m_asset.links)
        if (copiedPins.count(link.fromPin) && copiedPins.count(link.toPin))
            m_graphClipboard.links.push_back(link);
    for (const auto& parameter : m_asset.parameters)
        if (std::any_of(m_graphClipboard.nodes.begin(), m_graphClipboard.nodes.end(),
            [&](const engine::ShaderGraphNode& node) {
                return node.type.rfind("Parameter", 0) == 0
                    && node.name == parameter.name;
            })) m_graphClipboard.parameters.push_back(parameter);
}

void ShaderEditorPanel::PasteNodes()
{
    if (m_graphClipboard.nodes.empty()) return;
    PushUndo();
    std::uint64_t next = NextId(m_asset);
    std::unordered_map<std::uint64_t, std::uint64_t> nodes;
    std::unordered_map<std::uint64_t, std::uint64_t> pins;
    m_selectedNodes.clear();
    for (const auto& source : m_graphClipboard.nodes) {
        auto copy = source;
        nodes[source.id] = next;
        copy.id = next++;
        copy.x += 45.0f; copy.y += 45.0f;
        if (copy.type.rfind("Parameter", 0) == 0) copy.name += " Copy";
        m_selectedNodes.insert(copy.id);
        m_asset.nodes.push_back(std::move(copy));
    }
    for (const auto& source : m_graphClipboard.pins) {
        auto copy = source;
        pins[source.id] = next;
        copy.id = next++;
        copy.nodeId = nodes[source.nodeId];
        m_asset.pins.push_back(std::move(copy));
    }
    for (const auto& source : m_graphClipboard.links)
        m_asset.links.push_back({next++, pins[source.fromPin], pins[source.toPin]});
    for (const auto& source : m_graphClipboard.parameters) {
        auto copy = source;
        copy.id = next++;
        copy.name += " Copy";
        m_asset.parameters.push_back(std::move(copy));
    }
    m_dirty = true;
    m_compilePending = true;
}

void ShaderEditorPanel::DrawGraphCanvas()
{
    ImGui::TextDisabled("%s  >  %s  >  Domain Output",
                        m_asset.name.c_str(),
                        engine::ShaderDomainName(m_asset.domain));
    if (ImGui::Button("Undo")) UndoGraph();
    ImGui::SameLine(); if (ImGui::Button("Redo")) RedoGraph();
    ImGui::SameLine(); if (ImGui::Button("Delete")) DeleteSelectedNodes();
    ImGui::SameLine(); if (ImGui::Button("Duplicate##Graph")) DuplicateSelectedNodes();
    ImGui::SameLine(); if (ImGui::Button("Copy##Graph")) CopySelectedNodes();
    ImGui::SameLine(); if (ImGui::Button("Paste")) PasteNodes();
    ImGui::SameLine();
    if (ImGui::Button("Frame") && !m_selectedNodes.empty()) {
        PushUndo();
        const std::uint64_t group = NextId(m_asset) + 1000000;
        for (auto& node : m_asset.nodes)
            if (m_selectedNodes.count(node.id)) node.groupId = group;
        m_dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Align Y") && m_selectedNodes.size() > 1) {
        PushUndo();
        float y = 0.0f;
        for (const auto& node : m_asset.nodes)
            if (m_selectedNodes.count(node.id)) y += node.y;
        y /= static_cast<float>(m_selectedNodes.size());
        for (auto& node : m_asset.nodes)
            if (m_selectedNodes.count(node.id)) node.y = y;
        m_dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Auto Layout")) {
        PushUndo();
        float x = 40.0f;
        float y = 50.0f;
        for (auto& node : m_asset.nodes) {
            node.x = x; node.y = y;
            x += 230.0f;
            if (x > 900.0f) { x = 40.0f; y += 180.0f; }
        }
        m_dirty = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Right-click: add | drag pins: connect | middle-drag: pan | wheel: zoom");

    ImGui::BeginChild("ShaderGraphCanvas", ImVec2(0, 380), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    const ImVec2 maximum(origin.x + size.x, origin.y + size.y);
    if (ImGui::IsWindowFocused()) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) UndoGraph();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) RedoGraph();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) DuplicateSelectedNodes();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) CopySelectedNodes();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V)) PasteNodes();
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) DeleteSelectedNodes();
    }
    draw->AddRectFilled(origin, maximum, IM_COL32(24, 26, 31, 255));
    const float grid = 32.0f * m_graphZoom;
    for (float x = std::fmod(m_graphPanX, grid); x < size.x; x += grid)
        draw->AddLine(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x, maximum.y),
                      IM_COL32(46, 49, 57, 255));
    for (float y = std::fmod(m_graphPanY, grid); y < size.y; y += grid)
        draw->AddLine(ImVec2(origin.x, origin.y + y), ImVec2(maximum.x, origin.y + y),
                      IM_COL32(46, 49, 57, 255));

    ImGui::SetCursorScreenPos(origin);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("GraphBackground", size);
    const bool backgroundHovered = ImGui::IsItemHovered();
    if (backgroundHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        m_graphPanX += ImGui::GetIO().MouseDelta.x;
        m_graphPanY += ImGui::GetIO().MouseDelta.y;
    }
    if (backgroundHovered && ImGui::GetIO().MouseWheel != 0.0f)
        m_graphZoom = std::clamp(m_graphZoom + ImGui::GetIO().MouseWheel * 0.08f,
                                 0.45f, 1.8f);
    if (ImGui::IsItemClicked() && !ImGui::GetIO().KeyCtrl) m_selectedNodes.clear();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        m_nodePopupX = (mouse.x - origin.x - m_graphPanX) / m_graphZoom;
        m_nodePopupY = (mouse.y - origin.y - m_graphPanY) / m_graphZoom;
        m_nodePromptPin = 0;
        m_nodeSearch.fill('\0');
        ImGui::OpenPopup("CreateShaderNode");
    }

    auto nodePosition = [&](const engine::ShaderGraphNode& node) {
        return ImVec2(origin.x + m_graphPanX + node.x * m_graphZoom,
                      origin.y + m_graphPanY + node.y * m_graphZoom);
    };
    auto pinsFor = [&](std::uint64_t nodeId, bool input) {
        std::vector<const engine::ShaderGraphPin*> result;
        for (const auto& pin : m_asset.pins)
            if (pin.nodeId == nodeId && pin.input == input) result.push_back(&pin);
        return result;
    };
    auto pinPosition = [&](const engine::ShaderGraphPin& pin) {
        const auto node = std::find_if(m_asset.nodes.begin(), m_asset.nodes.end(),
            [&](const engine::ShaderGraphNode& value) { return value.id == pin.nodeId; });
        if (node == m_asset.nodes.end()) return origin;
        const auto list = pinsFor(pin.nodeId, pin.input);
        const auto item = std::find(list.begin(), list.end(), &pin);
        const float row = static_cast<float>(std::distance(list.begin(), item));
        const ImVec2 pos = nodePosition(*node);
        return ImVec2(pos.x + (pin.input ? 0.0f : 190.0f * m_graphZoom),
                      pos.y + (46.0f + row * 22.0f) * m_graphZoom);
    };

    std::unordered_set<std::uint64_t> drawnGroups;
    for (const auto& node : m_asset.nodes) {
        if (node.groupId == 0 || !drawnGroups.insert(node.groupId).second) continue;
        float left = node.x, top = node.y, right = node.x + 190.0f, bottom = node.y + 110.0f;
        for (const auto& member : m_asset.nodes) {
            if (member.groupId != node.groupId) continue;
            left = std::min(left, member.x); top = std::min(top, member.y);
            right = std::max(right, member.x + 190.0f);
            bottom = std::max(bottom, member.y + 110.0f);
        }
        const ImVec2 groupMin(origin.x + m_graphPanX + (left - 20.0f) * m_graphZoom,
                              origin.y + m_graphPanY + (top - 38.0f) * m_graphZoom);
        const ImVec2 groupMax(origin.x + m_graphPanX + (right + 20.0f) * m_graphZoom,
                              origin.y + m_graphPanY + (bottom + 20.0f) * m_graphZoom);
        draw->AddRectFilled(groupMin, groupMax, IM_COL32(55, 65, 82, 95), 8.0f);
        draw->AddRect(groupMin, groupMax, IM_COL32(90, 120, 160, 180), 8.0f);
        draw->AddText(ImVec2(groupMin.x + 8, groupMin.y + 8),
            IM_COL32(165, 190, 220, 255),
            node.comment.empty() ? "Shader Group" : node.comment.c_str());
    }

    for (const auto& link : m_asset.links) {
        const auto* from = FindPin(m_asset, link.fromPin);
        const auto* to = FindPin(m_asset, link.toPin);
        if (!from || !to) continue;
        const ImVec2 a = pinPosition(*from);
        const ImVec2 b = pinPosition(*to);
        draw->AddBezierCubic(a, ImVec2(a.x + 60, a.y), ImVec2(b.x - 60, b.y), b,
                             IM_COL32(130, 190, 245, 240), 2.5f);
    }

    m_hoveredPin = 0;
    bool openNodeContext = false;
    for (auto& node : m_asset.nodes) {
        const ImVec2 pos = nodePosition(node);
        const auto inputs = pinsFor(node.id, true);
        const auto outputs = pinsFor(node.id, false);
        const float rows = static_cast<float>(std::max(inputs.size(), outputs.size()));
        const float width = 190.0f * m_graphZoom;
        const float height = (62.0f + rows * 22.0f) * m_graphZoom;
        ImGui::PushID(reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(node.id)));
        ImGui::SetCursorScreenPos(pos);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("NodeBody", ImVec2(width, height));
        if (ImGui::IsItemClicked()) {
            if (!ImGui::GetIO().KeyCtrl) m_selectedNodes.clear();
            if (ImGui::GetIO().KeyCtrl && m_selectedNodes.count(node.id))
                m_selectedNodes.erase(node.id);
            else m_selectedNodes.insert(node.id);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            if (m_selectedNodes.count(node.id) == 0) {
                m_selectedNodes.clear();
                m_selectedNodes.insert(node.id);
            }
            m_contextNode = node.id;
            openNodeContext = true;
        }
        if (ImGui::IsItemActivated()) PushUndo();
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)
            && m_linkPin == 0) {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            node.x += delta.x / m_graphZoom;
            node.y += delta.y / m_graphZoom;
            m_dirty = true;
        }
        const bool selected = m_selectedNodes.count(node.id) != 0;
        draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
            selected ? IM_COL32(48, 82, 120, 248) : IM_COL32(42, 46, 55, 245), 6.0f);
        draw->AddRect(pos, ImVec2(pos.x + width, pos.y + height),
            selected ? IM_COL32(90, 180, 255, 255) : IM_COL32(20, 22, 26, 255),
            6.0f, 0, selected ? 2.5f : 1.0f);
        draw->AddText(ImVec2(pos.x + 10, pos.y + 9), IM_COL32(240, 242, 246, 255),
                      node.name.c_str());
        draw->AddText(ImVec2(pos.x + 10, pos.y + 27), IM_COL32(145, 160, 180, 255),
                      node.type.c_str());
        auto drawPins = [&](const std::vector<const engine::ShaderGraphPin*>& list) {
            for (const auto* pin : list) {
                // Pin names repeat frequently (for example most output nodes have
                // several inputs named by their semantic).  Scope every hit target
                // by the serialized pin ID so ImGui's ID stack remains unique.
                ImGui::PushID(reinterpret_cast<const void*>(
                    static_cast<std::uintptr_t>(pin->id)));
                const ImVec2 point = pinPosition(*pin);
                const ImU32 color = pin->type == engine::ShaderValueType::Float
                    ? IM_COL32(160, 210, 120, 255)
                    : pin->type == engine::ShaderValueType::Texture2D
                        ? IM_COL32(210, 130, 230, 255)
                        : IM_COL32(100, 180, 245, 255);
                draw->AddCircleFilled(point, 5.5f * m_graphZoom, color);
                const ImVec2 text = pin->input
                    ? ImVec2(point.x + 9, point.y - 7)
                    : ImVec2(point.x - 80 * m_graphZoom, point.y - 7);
                draw->AddText(text, IM_COL32(205, 212, 222, 255), pin->name.c_str());
                ImGui::SetCursorScreenPos(ImVec2(point.x - 8, point.y - 8));
                ImGui::InvisibleButton(pin->input ? "InputPin" : "OutputPin", ImVec2(16, 16));
                if (ImGui::IsItemHovered()) m_hoveredPin = pin->id;
                if (ImGui::IsItemClicked()) m_linkPin = pin->id;
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    PushUndo();
                    m_asset.links.erase(std::remove_if(
                        m_asset.links.begin(), m_asset.links.end(),
                        [pin](const engine::ShaderGraphLink& link) {
                            return link.fromPin == pin->id || link.toPin == pin->id;
                        }), m_asset.links.end());
                    m_dirty = true;
                    m_compilePending = true;
                }
                ImGui::PopID();
            }
        };
        drawPins(inputs);
        drawPins(outputs);
        ImGui::PopID();
    }

    if (openNodeContext) ImGui::OpenPopup("ShaderNodeContext");
    if (ImGui::BeginPopup("ShaderNodeContext")) {
        const auto contextNode = std::find_if(
            m_asset.nodes.begin(), m_asset.nodes.end(),
            [&](const engine::ShaderGraphNode& node) {
                return node.id == m_contextNode;
            });
        if (contextNode != m_asset.nodes.end()) {
            ImGui::TextUnformatted(contextNode->name.c_str());
            ImGui::TextDisabled("%s", contextNode->type.c_str());
            ImGui::Separator();
            const bool domainOutput =
                contextNode->type.find("Output") != std::string::npos;
            if (domainOutput) ImGui::BeginDisabled();
            if (ImGui::MenuItem("Delete Node", "Delete")) {
                DeleteSelectedNodes();
                m_contextNode = 0;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Duplicate Node", "Ctrl+D")) {
                DuplicateSelectedNodes();
                ImGui::CloseCurrentPopup();
            }
            if (domainOutput) ImGui::EndDisabled();
            if (ImGui::MenuItem("Copy Node", "Ctrl+C")) {
                CopySelectedNodes();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Break All Links")) {
                PushUndo();
                std::unordered_set<std::uint64_t> nodePins;
                for (const auto& pin : m_asset.pins)
                    if (pin.nodeId == m_contextNode) nodePins.insert(pin.id);
                m_asset.links.erase(std::remove_if(
                    m_asset.links.begin(), m_asset.links.end(),
                    [&](const engine::ShaderGraphLink& link) {
                        return nodePins.count(link.fromPin) != 0
                            || nodePins.count(link.toPin) != 0;
                    }), m_asset.links.end());
                m_dirty = true;
                m_compilePending = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    if (m_linkPin != 0) {
        const auto* source = FindPin(m_asset, m_linkPin);
        if (source) {
            const ImVec2 start = pinPosition(*source);
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            draw->AddBezierCubic(start, ImVec2(start.x + (source->input ? -60 : 60), start.y),
                ImVec2(mouse.x + (source->input ? 60 : -60), mouse.y), mouse,
                IM_COL32(250, 205, 90, 255), 2.5f);
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const auto* target = FindPin(m_asset, m_hoveredPin);
                if (target && source->input != target->input) {
                    const auto* output = source->input ? target : source;
                    const auto* input = source->input ? source : target;
                    if (engine::ShaderValueTypesCompatible(output->type, input->type)) {
                        PushUndo();
                        const auto previousLinks = m_asset.links;
                        m_asset.links.erase(std::remove_if(
                            m_asset.links.begin(), m_asset.links.end(),
                            [input](const engine::ShaderGraphLink& link) {
                                return link.toPin == input->id;
                            }), m_asset.links.end());
                        m_asset.links.push_back(
                            {NextId(m_asset), output->id, input->id});
                        const auto issues = engine::ValidateShaderAsset(m_asset);
                        if (std::any_of(issues.begin(), issues.end(),
                            [](const engine::ShaderAssetIssue& issue) {
                                return issue.message.find("cycles") != std::string::npos;
                            })) {
                            m_asset.links = previousLinks;
                            m_status = "Connection rejected: shader graphs cannot contain cycles";
                        } else {
                            m_dirty = true; m_compilePending = true;
                        }
                    } else m_status = "Connection rejected: incompatible pin types";
                } else if (!target) {
                    // Releasing a wire on empty canvas opens a filtered creation
                    // prompt. The selected node is connected automatically below.
                    const ImVec2 dropPosition = ImGui::GetIO().MousePos;
                    m_nodePopupX =
                        (dropPosition.x - origin.x - m_graphPanX) / m_graphZoom;
                    m_nodePopupY =
                        (dropPosition.y - origin.y - m_graphPanY) / m_graphZoom;
                    m_nodePromptPin = source->id;
                    m_nodeSearch.fill('\0');
                    ImGui::OpenPopup("CreateShaderNode");
                }
                m_linkPin = 0;
            }
        } else m_linkPin = 0;
    }

    if (ImGui::BeginPopup("CreateShaderNode")) {
        const auto* promptPin = FindPin(m_asset, m_nodePromptPin);
        if (promptPin) {
            ImGui::TextDisabled("Add and connect a compatible %s node",
                engine::ShaderValueTypeName(promptPin->type));
            ImGui::Separator();
        }
        ImGui::InputTextWithHint("##NodeSearch", "Search nodes...",
                                 m_nodeSearch.data(), m_nodeSearch.size());
        std::string query = m_nodeSearch.data();
        std::transform(query.begin(), query.end(), query.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const char* lastCategory = "";
        for (const auto& item : kNodeTemplates) {
            if (!TemplateAllowedForDomain(item, m_asset.domain)) continue;
            if (promptPin && !TemplateCompatibleWithPin(item, *promptPin)) continue;
            std::string haystack = std::string(item.category) + " " + item.type;
            std::transform(haystack.begin(), haystack.end(), haystack.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (!query.empty() && haystack.find(query) == std::string::npos) continue;
            if (std::string(lastCategory) != item.category) {
                ImGui::SeparatorText(item.category);
                lastCategory = item.category;
            }
            if (ImGui::MenuItem(item.type)) {
                const std::uint64_t sourcePinId = m_nodePromptPin;
                const bool connectAfterCreate = sourcePinId != 0;
                AddGraphNode(item.type, m_nodePopupX, m_nodePopupY);
                if (connectAfterCreate && !m_selectedNodes.empty()) {
                    const std::uint64_t newNodeId = *m_selectedNodes.begin();
                    const auto* sourcePin = FindPin(m_asset, sourcePinId);
                    const engine::ShaderGraphPin* newPin = nullptr;
                    if (sourcePin) {
                        for (const auto& pin : m_asset.pins) {
                            if (pin.nodeId != newNodeId
                                || pin.input == sourcePin->input) continue;
                            const bool compatible = sourcePin->input
                                ? engine::ShaderValueTypesCompatible(
                                    pin.type, sourcePin->type)
                                : engine::ShaderValueTypesCompatible(
                                    sourcePin->type, pin.type);
                            if (compatible) {
                                newPin = &pin;
                                break;
                            }
                        }
                    }
                    if (sourcePin && newPin) {
                        const std::uint64_t outputId =
                            sourcePin->input ? newPin->id : sourcePin->id;
                        const std::uint64_t inputId =
                            sourcePin->input ? sourcePin->id : newPin->id;
                        m_asset.links.erase(std::remove_if(
                            m_asset.links.begin(), m_asset.links.end(),
                            [inputId](const engine::ShaderGraphLink& link) {
                                return link.toPin == inputId;
                            }), m_asset.links.end());
                        m_asset.links.push_back(
                            {NextId(m_asset), outputId, inputId});
                        m_status = "Node added and connected";
                        m_dirty = true;
                        m_compilePending = true;
                    }
                }
                m_nodePromptPin = 0;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    if (!ImGui::IsPopupOpen("CreateShaderNode"))
        m_nodePromptPin = 0;

    // Compact graph overview in the lower-right corner.
    const ImVec2 miniSize(150.0f, 90.0f);
    const ImVec2 miniMin(maximum.x - miniSize.x - 10.0f,
                         maximum.y - miniSize.y - 10.0f);
    draw->AddRectFilled(miniMin, ImVec2(miniMin.x + miniSize.x, miniMin.y + miniSize.y),
                        IM_COL32(15, 17, 21, 220), 4.0f);
    if (!m_asset.nodes.empty()) {
        float minX = m_asset.nodes.front().x, maxX = minX;
        float minY = m_asset.nodes.front().y, maxY = minY;
        for (const auto& node : m_asset.nodes) {
            minX = std::min(minX, node.x); maxX = std::max(maxX, node.x);
            minY = std::min(minY, node.y); maxY = std::max(maxY, node.y);
        }
        const float rangeX = std::max(maxX - minX, 1.0f);
        const float rangeY = std::max(maxY - minY, 1.0f);
        for (const auto& node : m_asset.nodes) {
            const ImVec2 point(
                miniMin.x + 8.0f + (node.x - minX) / rangeX * (miniSize.x - 16.0f),
                miniMin.y + 8.0f + (node.y - minY) / rangeY * (miniSize.y - 16.0f));
            draw->AddCircleFilled(point, 3.0f,
                m_selectedNodes.count(node.id) ? IM_COL32(90, 190, 255, 255)
                                               : IM_COL32(130, 140, 155, 255));
        }
    }
    ImGui::EndChild();
}

void ShaderEditorPanel::DrawGraphInspector()
{
    if (m_selectedNodes.size() != 1) return;
    const std::uint64_t selected = *m_selectedNodes.begin();
    auto node = std::find_if(m_asset.nodes.begin(), m_asset.nodes.end(),
        [selected](const engine::ShaderGraphNode& item) { return item.id == selected; });
    if (node == m_asset.nodes.end()) return;
    ImGui::SeparatorText("Selected Node");
    std::array<char, 128> name{};
    std::array<char, 256> comment{};
    std::array<char, 128> value{};
    std::snprintf(name.data(), name.size(), "%s", node->name.c_str());
    std::snprintf(comment.data(), comment.size(), "%s", node->comment.c_str());
    std::snprintf(value.data(), value.size(), "%s", node->value.c_str());
    if (ImGui::InputText("Node Name", name.data(), name.size())) {
        const std::string previousName = node->name;
        node->name = name.data(); m_dirty = true; m_compilePending = true;
        if (node->type.rfind("Parameter", 0) == 0) {
            const auto parameter = std::find_if(
                m_asset.parameters.begin(), m_asset.parameters.end(),
                [&](const engine::ShaderParameter& item) {
                    return item.name == previousName;
                });
            if (parameter != m_asset.parameters.end()) parameter->name = node->name;
        }
    }
    if (ImGui::InputText("Comment", comment.data(), comment.size())) {
        node->comment = comment.data(); m_dirty = true;
    }
    if (node->type == "ConstantColor") {
        std::string normalized = node->value;
        // The value is stored as a GLSL constructor ("vec4(r,g,b,a)"). Drop the
        // leading "vecN(" wrapper before parsing: otherwise the stream extraction
        // trips on the "vec4" token, fails, and the picker is stuck showing the
        // hard-coded default every frame (so edits appear to do nothing).
        if (const std::size_t open = normalized.find('('); open != std::string::npos) {
            normalized = normalized.substr(open + 1);
        }
        for (char& c : normalized)
            if (c == ',' || c == '(' || c == ')') c = ' ';
        std::istringstream input(normalized);
        float color[4]{0.8f, 0.3f, 0.1f, 1.0f};
        input >> color[0] >> color[1] >> color[2] >> color[3];
        if (ImGui::ColorEdit4("Color", color)) {
            char formatted[96];
            std::snprintf(formatted, sizeof(formatted), "vec4(%.6g,%.6g,%.6g,%.6g)",
                          color[0], color[1], color[2], color[3]);
            node->value = formatted;
            m_dirty = true;
            m_compilePending = true;
        }
    } else if (node->type.rfind("Constant", 0) == 0
        && ImGui::InputText("Value", value.data(), value.size())) {
        node->value = value.data(); m_dirty = true; m_compilePending = true;
    }
    if (node->type == "ParameterColor") {
        const auto parameter = std::find_if(
            m_asset.parameters.begin(), m_asset.parameters.end(),
            [&](const engine::ShaderParameter& item) {
                return item.name == node->name;
            });
        if (parameter != m_asset.parameters.end()) {
            std::string normalized = parameter->defaultValue;
            for (char& c : normalized)
                if (c == ',' || c == '(' || c == ')') c = ' ';
            std::istringstream input(normalized);
            float color[4]{1.0f, 1.0f, 1.0f, 1.0f};
            input >> color[0] >> color[1] >> color[2] >> color[3];
            if (ImGui::ColorEdit4("Default Color", color)) {
                char formatted[96];
                std::snprintf(formatted, sizeof(formatted), "%.6g,%.6g,%.6g,%.6g",
                              color[0], color[1], color[2], color[3]);
                parameter->defaultValue = formatted;
                m_dirty = true;
                m_compilePending = true;
            }
        }
    }
    if (node->type == "ObjectColor") {
        ImGui::ColorEdit4("Preview Object Color", m_previewObjectColor);
        ImGui::TextWrapped(
            "Object Color is supplied by each scene object's Rendering Color. "
            "Use Constant Color for a fixed shader color, or Parameter Color "
            "for a material-editable color.");
    }
    ImGui::Checkbox("Collapsed", &node->collapsed);
}

void ShaderEditorPanel::EnsurePreviewResources()
{
    if (!m_framebuffer) m_framebuffer.emplace(512, 512, GL_RGBA16F, true);
    if (!m_postInput) m_postInput.emplace(512, 512, GL_RGBA16F, true);
    if (!m_sphere) m_sphere.emplace(engine::primitives::Sphere(24));
    if (!m_cube) m_cube.emplace(engine::primitives::Cube());
    if (!m_plane) m_plane.emplace(engine::primitives::Plane(1.8f));
    if (!m_groundMesh) m_groundMesh.emplace(engine::primitives::Plane(8.0f));
    if (!m_fullscreenQuad) {
        const std::vector<float> vertices = {
            -1.0f, -1.0f, 0.0f, 0.0f,
             1.0f, -1.0f, 1.0f, 0.0f,
             1.0f,  1.0f, 1.0f, 1.0f,
            -1.0f,  1.0f, 0.0f, 1.0f
        };
        const std::vector<std::uint32_t> indices = {0, 1, 2, 0, 2, 3};
        m_fullscreenQuad.emplace(
            vertices, indices, engine::VertexLayout{{2}, {2}});
    }
    if (!m_previewSurface) {
        static const char* vertex = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
uniform mat4 uModel;
uniform mat4 uViewProjection;
out vec3 vNormal;
void main(){vec4 world=uModel*vec4(aPosition,1.0);vNormal=mat3(transpose(inverse(uModel)))*aNormal;gl_Position=uViewProjection*world;}
)GLSL";
        static const char* fragment = R"GLSL(
#version 330 core
in vec3 vNormal;
out vec4 FragColor;
uniform vec3 uLightDirection;
uniform float uLightIntensity;
uniform vec3 uBaseColor;
void main(){float n=max(dot(normalize(vNormal),normalize(-uLightDirection)),0.0);FragColor=vec4(uBaseColor*(0.12+n*uLightIntensity),1.0);}
)GLSL";
        engine::ShaderCompileReport report;
        m_previewSurface = engine::Shader::TryCompile(vertex, fragment, report);
    }
    if (!m_previewNormalTexture) {
        const float normal[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        glGenTextures(1, &m_previewNormalTexture);
        glBindTexture(GL_TEXTURE_2D, m_previewNormalTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 1, 1, 0,
                     GL_RGBA, GL_FLOAT, normal);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    if (!m_previewVelocityTexture) {
        const float velocity[2] = {0.0f, 0.0f};
        glGenTextures(1, &m_previewVelocityTexture);
        glBindTexture(GL_TEXTURE_2D, m_previewVelocityTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, 1, 1, 0,
                     GL_RG, GL_FLOAT, velocity);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
}

unsigned int ShaderEditorPanel::RenderPreview(int width, int height)
{
    EnsurePreviewResources();
    if (!m_framebuffer) return 0;
    m_framebuffer->Resize(std::max(width, 32), std::max(height, 32));
    m_postInput->Resize(std::max(width, 32), std::max(height, 32));
    if (m_autoCompile && m_compilePending) Compile();

    const std::string key = m_path.empty() ? "__unsaved_shader__" : m_path;
    const engine::Shader* shader = m_runtime.Find(
        key, engine::ShaderDomainName(m_asset.domain));
    if (!shader) return m_framebuffer->ColorTexture();

    GLint previousFbo = 0;
    GLint previousViewport[4]{};
    GLint previousProgram = 0;
    GLint previousVao = 0;
    GLfloat previousClearColor[4]{};
    GLboolean previousDepthMask = GL_TRUE;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);
    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);

    const bool postProcess = m_asset.domain == engine::ShaderDomain::PostProcess;
    if (postProcess && !m_previewSurface) return m_framebuffer->ColorTexture();
    engine::Framebuffer* geometryTarget =
        postProcess ? &*m_postInput : &*m_framebuffer;
    geometryTarget->Bind();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glClearColor(m_background[0], m_background[1], m_background[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float yaw = glm::radians(m_yaw);
    const float pitch = glm::radians(m_pitch);
    const glm::vec3 eye(
        m_distance * std::cos(pitch) * std::sin(yaw),
        m_distance * std::sin(pitch),
        m_distance * std::cos(pitch) * std::cos(yaw));
    const glm::mat4 view = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0, 1, 0));
    const glm::mat4 projection = glm::perspective(
        glm::radians(45.0f), static_cast<float>(width) / std::max(height, 1),
        0.05f, 100.0f);
    const float lightYaw = glm::radians(m_environmentYaw);
    const float sunHeight = std::sin(m_timeOfDay * 6.2831853f);

    engine::Shader& mutableShader = postProcess
        ? *m_previewSurface
        : const_cast<engine::Shader&>(*shader);
    mutableShader.Bind();
    mutableShader.SetMat4("uViewProjection", projection * view);
    mutableShader.SetVec3("uLightDirection",
        glm::normalize(glm::vec3(std::sin(lightYaw), -0.6f - std::abs(sunHeight),
                                std::cos(lightYaw))));
    mutableShader.SetFloat("uLightIntensity",
        m_lightIntensity * (m_bloom ? 1.12f : 1.0f));
    mutableShader.SetVec3("uBaseColor", glm::vec3(0.68f, 0.32f, 0.12f));
    mutableShader.SetVec4("uObjectColor", glm::vec4(
        m_previewObjectColor[0], m_previewObjectColor[1],
        m_previewObjectColor[2], m_previewObjectColor[3]));
    for (const auto& parameter : m_asset.parameters) {
        if (parameter.type != engine::ShaderValueType::Color
            && parameter.type != engine::ShaderValueType::Vec4) continue;
        std::string normalized = parameter.defaultValue;
        for (char& c : normalized)
            if (c == ',' || c == '(' || c == ')') c = ' ';
        std::istringstream input(normalized);
        glm::vec4 color(0.0f);
        input >> color.x >> color.y >> color.z >> color.w;
        mutableShader.SetVec4("u_" + parameter.name, color);
    }
    glm::mat4 model(1.0f);
    if (m_shape == PreviewShape::Plane)
        model = glm::rotate(model, glm::radians(0.0f), glm::vec3(1, 0, 0));
    mutableShader.SetMat4("uModel", model);
    if (m_shape == PreviewShape::Sphere) m_sphere->Draw();
    else if (m_shape == PreviewShape::Cube) m_cube->Draw();
    else if (m_shape == PreviewShape::Plane) m_plane->Draw();
    else if (m_importedModel)
    {
        const float radius = std::max(m_importedModel->BoundingRadius(), 0.001f);
        const glm::mat4 importedModel = glm::translate(
            glm::scale(glm::mat4(1.0f), glm::vec3(0.8f / radius)),
            -m_importedModel->Center());
        mutableShader.SetMat4("uModel", importedModel);
        for (const engine::SubMesh& submesh : m_importedModel->SubMeshes())
            submesh.mesh.Draw();
    }
    if (m_ground)
    {
        mutableShader.SetVec3("uBaseColor", glm::vec3(0.11f));
        mutableShader.SetVec4("uObjectColor", glm::vec4(0.11f, 0.11f, 0.11f, 1.0f));
        mutableShader.SetMat4("uModel",
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.65f, 0.0f)));
        m_groundMesh->Draw();
    }

    if (postProcess) {
        m_framebuffer->Bind();
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        engine::Shader& effect = const_cast<engine::Shader&>(*shader);
        effect.Bind();
        m_postInput->BindColorTexture(0);
        effect.SetInt("uSceneColor", 0);
        m_postInput->BindDepthTexture(1);
        effect.SetInt("uSceneDepth", 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_previewNormalTexture);
        effect.SetInt("uSceneNormal", 2);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, m_previewVelocityTexture);
        effect.SetInt("uSceneVelocity", 3);
        effect.SetVec2("uTexelSize", glm::vec2(
            1.0f / static_cast<float>(std::max(width, 1)),
            1.0f / static_cast<float>(std::max(height, 1))));
        effect.SetFloat("uExposure", 1.0f);
        effect.SetFloat("uTime", m_timeOfDay * 60.0f);
        effect.SetFloat("uDeltaTime", 1.0f / 60.0f);
        m_fullscreenQuad->Draw();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
    glViewport(previousViewport[0], previousViewport[1],
               previousViewport[2], previousViewport[3]);
    glUseProgram(static_cast<GLuint>(previousProgram));
    glBindVertexArray(static_cast<GLuint>(previousVao));
    glClearColor(previousClearColor[0], previousClearColor[1],
                 previousClearColor[2], previousClearColor[3]);
    glDepthMask(previousDepthMask);
    if (depthEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (cullEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (blendEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    return m_framebuffer->ColorTexture();
}

std::string ShaderEditorPanel::NumberedSource(const std::string& source) const
{
    std::istringstream input(source);
    std::ostringstream output;
    std::string line;
    int number = 1;
    while (std::getline(input, line))
        output << number++ << "  " << line << '\n';
    return output.str();
}

void ShaderEditorPanel::Draw(EditorAssets& assets, bool* open)
{
    if (m_asset.nodes.empty() && m_path.empty() && !m_dirty) NewDocument();
    bool requestedOpen = open ? *open : true;
    if (!ImGui::Begin("Shader Editor", &requestedOpen))
    {
        m_keyboardFocused =
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        ImGui::End();
        if (!requestedOpen && m_dirty)
        {
            requestedOpen = true;
            m_closeAfterPrompt = true;
            m_promptQueued = true;
        }
        if (open) *open = requestedOpen;
        return;
    }
    m_keyboardFocused =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (!requestedOpen && m_dirty)
    {
        requestedOpen = true;
        m_closeAfterPrompt = true;
        ImGui::OpenPopup("Unsaved Shader Asset");
    }
    if (m_promptQueued)
    {
        ImGui::OpenPopup("Unsaved Shader Asset");
        m_promptQueued = false;
    }

    if (ImGui::Button("New")) RequestNew();
    ImGui::SameLine();
    const EditorAssets::Asset* selected = assets.SelectedAsset();
    const bool selectedShader = selected && selected->type == EditorAssets::Type::Shader
        && std::filesystem::path(selected->relativePath).extension() == ".3dgshader";
    if (!selectedShader) ImGui::BeginDisabled();
    if (ImGui::Button("Open Selected")) RequestOpen(assets.SelectedAssetFullPath());
    if (!selectedShader) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Save")) SaveDocument(assets, false);
    ImGui::SameLine();
    if (ImGui::Button("Save As")) SaveDocument(assets, true);
    ImGui::SameLine();
    if (m_path.empty()) ImGui::BeginDisabled();
    if (ImGui::Button("Revert")) RequestOpen(m_path);
    if (m_path.empty()) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Duplicate##Document")) DuplicateDocument(assets);

    ImGui::TextDisabled("%s%s", m_path.empty() ? "Unsaved shader" : m_path.c_str(),
                        m_dirty ? " *" : "");
    if (ImGui::InputText("Name", m_name.data(), m_name.size()))
    {
        m_asset.name = m_name.data();
        m_dirty = true;
    }
    int domain = static_cast<int>(m_asset.domain);
    const char* domains[] = {"Surface", "Post Process", "Particle", "Unlit"};
    if (ImGui::Combo("Domain", &domain, domains, 4))
    {
        PushUndo();
        m_asset.domain = static_cast<engine::ShaderDomain>(domain);
        const auto output = std::find_if(
            m_asset.nodes.begin(), m_asset.nodes.end(),
            [](const engine::ShaderGraphNode& node) {
                return node.type.find("Output") != std::string::npos;
            });
        if (output != m_asset.nodes.end()) {
            output->type = DomainOutput(m_asset.domain);
            output->name = std::string(engine::ShaderDomainName(m_asset.domain))
                + " Output";
            m_asset.pins.erase(std::remove_if(
                m_asset.pins.begin(), m_asset.pins.end(),
                [&](const engine::ShaderGraphPin& pin) {
                    return pin.nodeId == output->id;
                }), m_asset.pins.end());
            const std::uint64_t pin = NextId(m_asset);
            m_asset.pins.push_back({
                pin, output->id,
                m_asset.domain == engine::ShaderDomain::Surface
                    ? "Base Color" : "Color",
                engine::ShaderValueType::Color, true, false
            });
        }
        m_dirty = true;
        m_compilePending = true;
    }
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("3DGEDITOR_ASSET"))
        {
            const char* path = static_cast<const char*>(payload->Data);
            if (path && std::filesystem::path(path).extension() == ".3dgshader")
                RequestOpen(path);
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupModal("Unsaved Shader Asset", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("This shader has unsaved changes.");
        if (ImGui::Button("Save"))
        {
            if (SaveDocument(assets, false))
            {
                if (m_pendingNew) NewDocument();
                else if (!m_pendingOpenPath.empty()) OpenDocument(m_pendingOpenPath);
                else if (m_closeAfterPrompt) requestedOpen = false;
                m_pendingNew = false; m_pendingOpenPath.clear();
                m_closeAfterPrompt = false; ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard"))
        {
            if (m_pendingNew) NewDocument();
            else if (!m_pendingOpenPath.empty()) OpenDocument(m_pendingOpenPath);
            else if (m_closeAfterPrompt) requestedOpen = false;
            m_pendingNew = false; m_pendingOpenPath.clear();
            m_closeAfterPrompt = false; ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            m_pendingNew = false; m_pendingOpenPath.clear();
            m_closeAfterPrompt = false; ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SeparatorText("Compile");
    if (ImGui::Button("Compile")) Compile(true);
    ImGui::SameLine();
    ImGui::Checkbox("Auto Compile", &m_autoCompile);
    ImGui::SameLine();
    if (ImGui::Button("Apply"))
    {
        Compile(true);
        m_applied = m_runtime.Find(m_path.empty() ? "__unsaved_shader__" : m_path,
                                  engine::ShaderDomainName(m_asset.domain)) != nullptr;
    }
    ImGui::SameLine();
    if (m_lastValidVertex.empty()) ImGui::BeginDisabled();
    if (ImGui::Button("Restore Last Valid"))
    {
        m_vertexSource = m_lastValidVertex;
        m_fragmentSource = m_lastValidFragment;
        m_status = "Restored last valid preview program";
    }
    if (m_lastValidVertex.empty()) ImGui::EndDisabled();

    const auto* report = m_runtime.LastReport(
        m_path.empty() ? "__unsaved_shader__" : m_path,
        engine::ShaderDomainName(m_asset.domain));
    ImGui::Text("Status: %s", m_status.c_str());
    ImGui::Text("Compile: %.2f ms | Variant: %s | Parameters: %zu | Textures: %zu",
        m_compileMilliseconds, engine::ShaderDomainName(m_asset.domain),
        m_asset.parameters.size(),
        static_cast<std::size_t>(std::count_if(
            m_asset.parameters.begin(), m_asset.parameters.end(),
            [](const engine::ShaderParameter& parameter) {
                return parameter.type == engine::ShaderValueType::Texture2D;
            })));
    ImGui::Text("Cache: %s%s", m_runtime.Find(
        m_path.empty() ? "__unsaved_shader__" : m_path,
        engine::ShaderDomainName(m_asset.domain)) ? "resident" : "empty",
        m_runtime.IsUsingFallback(
            m_path.empty() ? "__unsaved_shader__" : m_path,
            engine::ShaderDomainName(m_asset.domain)) ? " (fallback)" : "");
    if (report)
    {
        for (std::size_t i = 0; i < report->diagnostics.size(); ++i)
        {
            const auto& diagnostic = report->diagnostics[i];
            if (ImGui::Selectable(diagnostic.message.c_str(),
                                  m_selectedDiagnostic == static_cast<int>(i)))
            {
                m_selectedDiagnostic = static_cast<int>(i);
                m_sourceTab = diagnostic.stage == engine::ShaderStage::Vertex ? 0 : 1;
                const auto mapped = m_fragmentLineNodes.find(diagnostic.line);
                if (mapped != m_fragmentLineNodes.end()) {
                    m_selectedNodes = {mapped->second};
                    const auto node = std::find_if(
                        m_asset.nodes.begin(), m_asset.nodes.end(),
                        [&](const engine::ShaderGraphNode& item) {
                            return item.id == mapped->second;
                        });
                    if (node != m_asset.nodes.end()) {
                        m_graphPanX = 220.0f - node->x * m_graphZoom;
                        m_graphPanY = 120.0f - node->y * m_graphZoom;
                    }
                }
            }
        }
    }
    if (!m_error.empty()) ImGui::TextColored(ImVec4(1, 0.3f, 0.25f, 1), "%s", m_error.c_str());

    ImGui::SeparatorText("Typed Shader Graph");
    DrawGraphCanvas();
    DrawGraphInspector();

    if (ImGui::BeginTable("ShaderWorkspace", 2, ImGuiTableFlags_Resizable))
    {
        ImGui::TableNextColumn();
        ImGui::SeparatorText("Live Preview");
        const char* shapes[] = {"Sphere", "Cube", "Plane", "Imported Model"};
        int shape = static_cast<int>(m_shape);
        ImGui::Combo("Shape", &shape, shapes, 4);
        m_shape = static_cast<PreviewShape>(shape);
        if (selected && (selected->type == EditorAssets::Type::Model
                         || selected->type == EditorAssets::Type::SkeletalModel)
            && ImGui::Button("Use Selected Model"))
        {
            try
            {
                m_importedModel = std::make_unique<engine::Model>(
                    engine::Model::FromFile(assets.SelectedAssetFullPath()));
                m_importedModelPath = assets.SelectedAssetFullPath();
                m_shape = PreviewShape::ImportedModel;
            }
            catch (const std::exception& error) { m_error = error.what(); }
        }
        ImGui::SliderFloat("Orbit Yaw", &m_yaw, -180.0f, 180.0f);
        ImGui::SliderFloat("Orbit Pitch", &m_pitch, -80.0f, 80.0f);
        ImGui::SliderFloat("Distance", &m_distance, 1.2f, 12.0f);
        ImGui::SliderFloat("Time of Day", &m_timeOfDay, 0.0f, 1.0f);
        ImGui::SliderFloat("Environment Rotation", &m_environmentYaw, -180.0f, 180.0f);
        ImGui::SliderFloat("Light Intensity", &m_lightIntensity, 0.0f, 8.0f);
        ImGui::Checkbox("Ground + Shadow", &m_ground);
        ImGui::SameLine(); ImGui::Checkbox("Bloom", &m_bloom);
        ImGui::ColorEdit3("Background", m_background);
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const int previewWidth = std::max(64, static_cast<int>(available.x));
        const int previewHeight = std::max(160, std::min(480, static_cast<int>(available.y)));
        const unsigned int texture = RenderPreview(previewWidth, previewHeight);
        if (texture)
            ImGui::Image((ImTextureID)(std::intptr_t)texture,
                         ImVec2(static_cast<float>(previewWidth),
                                static_cast<float>(previewHeight)),
                         ImVec2(0, 1), ImVec2(1, 0));

        ImGui::TableNextColumn();
        ImGui::SeparatorText("Generated GLSL");
        if (ImGui::Button("Vertex")) m_sourceTab = 0;
        ImGui::SameLine(); if (ImGui::Button("Fragment")) m_sourceTab = 1;
        ImGui::SameLine(); if (ImGui::Button("Copy##Source"))
            ImGui::SetClipboardText(
                (m_sourceTab == 0 ? m_vertexSource : m_fragmentSource).c_str());
        ImGui::InputTextWithHint("Search", "Find in generated source",
                                 m_sourceSearch.data(), m_sourceSearch.size());
        const std::string& rawSource =
            m_sourceTab == 0 ? m_vertexSource : m_fragmentSource;
        if (m_sourceSearch[0] != '\0')
        {
            const std::size_t match = rawSource.find(m_sourceSearch.data());
            if (match == std::string::npos)
                ImGui::TextDisabled("No source match");
            else
            {
                const int line = 1 + static_cast<int>(
                    std::count(rawSource.begin(), rawSource.begin() + match, '\n'));
                ImGui::TextDisabled("Match at generated line %d", line);
            }
        }
        const std::string numbered = NumberedSource(
            rawSource);
        ImGui::InputTextMultiline("##GeneratedSource",
            const_cast<char*>(numbered.c_str()), numbered.size() + 1,
            ImGui::GetContentRegionAvail(),
            ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_AllowTabInput);
        ImGui::EndTable();
    }

    ImGui::End();
    if (open) *open = requestedOpen;
}
