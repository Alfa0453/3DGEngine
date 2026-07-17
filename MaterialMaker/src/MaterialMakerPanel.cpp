#include "MaterialMaker/MaterialMakerPanel.h"

#include "MaterialMaker/MaterialPreview.h"
#include "MaterialMaker/TexturePacker.h"
#include "MaterialMaker/ModelMaterialImport.h"
#include <engine/assets/ShaderAsset.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <utility>

namespace material_maker {
namespace {

void CopyToBuffer(char* destination, std::size_t size, const std::string& source) {
    if (size == 0) {
        return;
    }
    std::snprintf(destination, size, "%s", source.c_str());
}

float Clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

ImU32 ColorU32(float r, float g, float b, float a = 1.0f) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(Clamp01(r), Clamp01(g), Clamp01(b), Clamp01(a)));
}

ImU32 MaterialColor(const MaterialDocument& material, float light, float alpha = 1.0f) {
    const float metalLift = material.metallic * 0.18f;
    return ColorU32((material.albedo[0] * light + metalLift) * material.ao + material.emissive[0],
                    (material.albedo[1] * light + metalLift) * material.ao + material.emissive[1],
                    (material.albedo[2] * light + metalLift) * material.ao + material.emissive[2],
                    alpha);
}

std::string TrimClipboardPath(std::string value) {
    const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(),
        [&](char c) { return !isSpace(static_cast<unsigned char>(c)); }));
    value.erase(std::find_if(value.rbegin(), value.rend(),
        [&](char c) { return !isSpace(static_cast<unsigned char>(c)); }).base(), value.end());
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

std::string LowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

bool IsSupportedTexturePath(const std::filesystem::path& path) {
    const std::string extension = LowerExtension(path);
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga";
}

// Starter presets. Metal entries use measured base reflectances (F0), which is the
// physically-correct "albedo" for a metallic-workflow material.
struct Preset {
    const char* name;
    float albedo[3];
    float metallic;
    float roughness;
};
const Preset kPresets[] = {
    {"Gold",            {1.000f, 0.766f, 0.336f}, 1.0f, 0.22f},
    {"Silver",          {0.972f, 0.960f, 0.915f}, 1.0f, 0.18f},
    {"Copper",          {0.955f, 0.638f, 0.538f}, 1.0f, 0.25f},
    {"Aluminium",       {0.913f, 0.922f, 0.924f}, 1.0f, 0.30f},
    {"Iron",            {0.560f, 0.570f, 0.580f}, 1.0f, 0.35f},
    {"Chrome",          {0.550f, 0.556f, 0.554f}, 1.0f, 0.05f},
    {"Plastic (white)", {0.900f, 0.900f, 0.900f}, 0.0f, 0.35f},
    {"Rubber",          {0.180f, 0.180f, 0.180f}, 0.0f, 0.90f},
    {"Painted wood",    {0.350f, 0.220f, 0.120f}, 0.0f, 0.70f},
    {"Ceramic",         {0.850f, 0.830f, 0.800f}, 0.0f, 0.25f},
};
constexpr int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

// Field-wise equality, for the unsaved-changes indicator.
bool SameMaterial(const MaterialDocument& a, const MaterialDocument& b) {
    return ToJson(a) == ToJson(b);
}

} // namespace

MaterialMakerPanel::MaterialMakerPanel(std::string outputDirectory)
    : m_outputDirectory(std::move(outputDirectory)),
      m_preview(std::make_unique<MaterialPreview>()) {
    Reset();
}

MaterialMakerPanel::~MaterialMakerPanel() = default;

bool MaterialMakerPanel::Draw() {
    return Draw(nullptr);
}

bool MaterialMakerPanel::Draw(bool* open) {
    m_savedThisFrame = false;
    ImGui::SetNextWindowSize(ImVec2(440.0f, 720.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Material Maker", open, ImGuiWindowFlags_NoCollapse)) {
        DrawContent();
    }
    ImGui::End();
    return m_savedThisFrame;
}

bool MaterialMakerPanel::DrawContent() {
    DrawPreview();
    ImGui::Separator();
    DrawPresetControls();
    DrawSurfaceControls();
    DrawAdvancedControls();
    DrawValidation();
    ImGui::Separator();
    DrawTextureControls();
    DrawShaderControls();
    ImGui::Separator();
    DrawExportControls();
    ImGui::Separator();
    DrawModelImport();
    ImGui::Separator();
    DrawLibraryControls();
    return m_savedThisFrame;
}

bool MaterialMakerPanel::SaveCurrent() {
    return Save();
}

bool MaterialMakerPanel::LoadFromFile(const std::string& path) {
    std::string error;
    MaterialDocument loaded;
    if (!LoadMaterialFile(path, &loaded, &error)) {
        m_status = "Load failed: " + error;
        return false;
    }

    m_material = loaded;
    m_savedSnapshot = m_material;
    m_hasSavedSnapshot = true;
    m_lastSavedPath = path;
    m_outputDirectory = std::filesystem::path(path).parent_path().string();
    m_libraryScanned = false;
    SyncBuffersFromMaterial();
    m_status = "Loaded " + path;
    return true;
}

void MaterialMakerPanel::SetOutputDirectory(const std::string& outputDirectory) {
    if (m_outputDirectory == outputDirectory) return;
    m_outputDirectory = outputDirectory;
    m_libraryScanned = false;
    CopyToBuffer(m_outputDirectoryBuffer, sizeof(m_outputDirectoryBuffer), m_outputDirectory);
}

void MaterialMakerPanel::SetAlbedoMap(const std::string& path) {
    m_material.albedoMap = path;
    CopyToBuffer(m_albedoMapBuffer, sizeof(m_albedoMapBuffer), m_material.albedoMap);
}

void MaterialMakerPanel::SetNormalMap(const std::string& path) {
    m_material.normalMap = path;
    CopyToBuffer(m_normalMapBuffer, sizeof(m_normalMapBuffer), m_material.normalMap);
}

void MaterialMakerPanel::SetMetalRoughMap(const std::string& path) {
    m_material.metalRoughMap = path;
    CopyToBuffer(m_metalRoughMapBuffer, sizeof(m_metalRoughMapBuffer), m_material.metalRoughMap);
}

void MaterialMakerPanel::SetHeightMap(const std::string& path) {
    m_material.heightMap = path;
    CopyToBuffer(m_heightMapBuffer, sizeof(m_heightMapBuffer), m_material.heightMap);
}

bool MaterialMakerPanel::SetShaderAsset(const std::string& path) {
    engine::ShaderAsset shader;
    std::string error;
    if (!engine::LoadShaderAsset(path, &shader, &error)) {
        m_status = "Shader load failed: " + error;
        return false;
    }
    m_material.shaderPath = path;
    m_material.shaderParameters.clear();
    for (const auto& source : shader.parameters) {
        ShaderParameterDocument parameter;
        parameter.name = source.name;
        parameter.type = static_cast<int>(source.type);
        parameter.value = source.defaultValue;
        m_material.shaderParameters.push_back(std::move(parameter));
    }
    CopyToBuffer(m_shaderPathBuffer, sizeof(m_shaderPathBuffer), path);
    m_status = "Loaded shader parameters from " + path;
    return true;
}

void MaterialMakerPanel::DrawShaderControls() {
    if (!ImGui::CollapsingHeader("Custom Shader", ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (ImGui::InputText("Shader Asset", m_shaderPathBuffer, sizeof(m_shaderPathBuffer)))
        m_material.shaderPath = m_shaderPathBuffer;
    ImGui::SameLine();
    if (ImGui::SmallButton("Load"))
        SetShaderAsset(m_shaderPathBuffer);
    ImGui::TextDisabled("Exposed graph parameters become material-instance values.");
    for (auto& parameter : m_material.shaderParameters) {
        ImGui::PushID(parameter.name.c_str());
        std::array<char, 256> value{};
        std::snprintf(value.data(), value.size(), "%s", parameter.value.c_str());
        if (parameter.type == static_cast<int>(engine::ShaderValueType::Bool)) {
            bool enabled = parameter.value == "1" || parameter.value == "true";
            if (ImGui::Checkbox(parameter.name.c_str(), &enabled))
                parameter.value = enabled ? "true" : "false";
        } else if (ImGui::InputText(parameter.name.c_str(), value.data(), value.size())) {
            parameter.value = value.data();
        }
        ImGui::PopID();
    }
    if (!m_material.shaderPath.empty() && ImGui::Button("Clear Custom Shader")) {
        m_material.shaderPath.clear();
        m_material.shaderParameters.clear();
        m_shaderPathBuffer[0] = '\0';
    }
}

void MaterialMakerPanel::ApplyPreset(int index) {
    if (index < 0 || index >= kPresetCount) {
        return;
    }
    const Preset& p = kPresets[index];
    m_material.albedo    = {p.albedo[0], p.albedo[1], p.albedo[2]};
    m_material.metallic  = p.metallic;
    m_material.roughness = p.roughness;
    m_material.ao        = 1.0f;
    m_status = std::string("Applied preset: ") + p.name;
}

void MaterialMakerPanel::DrawPresetControls() {
    // A menu-style combo: picking an entry applies it, then the combo closes.
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("Preset", "Load a preset...")) {
        for (int i = 0; i < kPresetCount; ++i) {
            if (ImGui::Selectable(kPresets[i].name)) {
                ApplyPreset(i);
            }
        }
        ImGui::EndCombo();
    }
}

void MaterialMakerPanel::DrawValidation() {
    const float maxC = std::max(m_material.albedo[0], std::max(m_material.albedo[1], m_material.albedo[2]));
    const float minC = std::min(m_material.albedo[0], std::min(m_material.albedo[1], m_material.albedo[2]));
    const bool  dielectric = m_material.metallic < 0.1f;

    bool any = false;
    auto warn = [&](const char* msg) {
        any = true;
        ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.35f, 1.0f), "! %s", msg);
    };
    if (dielectric && maxC < 0.05f) warn("Albedo very dark for a non-metal (below ~sRGB 30).");
    if (dielectric && minC > 0.94f) warn("Albedo very bright for a non-metal (above ~sRGB 240).");
    if (m_material.metallic > 0.1f && m_material.metallic < 0.9f) warn("Partial metallic: real surfaces are usually 0 or 1.");
    if (m_material.roughness < 0.04f) warn("Roughness near 0 can alias/sparkle; keep a small floor.");
    if (m_material.metallic > 0.9f && maxC < 0.30f) warn("Metal base colour is dark; metal reflectance (F0) is usually bright.");
    if (m_material.transmission > 0.0f && m_material.blendMode != 2)
        warn("Transmission is most useful with Transparent blend mode.");
    if (!m_material.heightMap.empty() && m_material.heightScale <= 0.0f)
        warn("Height map is assigned but parallax strength is zero.");
    if (!any) {
        ImGui::TextColored(ImVec4(0.45f, 0.82f, 0.58f, 1.0f), "PBR: looks physically plausible.");
    }
}

void MaterialMakerPanel::DrawSurfaceControls() {
    if (ImGui::InputText("Name", m_nameBuffer, sizeof(m_nameBuffer))) {
        m_material.name = m_nameBuffer;
    }

    ImGui::ColorEdit3("Albedo", m_material.albedo.data());
    ImGui::SliderFloat("Metallic", &m_material.metallic, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Roughness", &m_material.roughness, 0.02f, 1.0f, "%.2f");
    ImGui::SliderFloat("AO", &m_material.ao, 0.0f, 1.0f, "%.2f");
    ImGui::ColorEdit3("Emissive", m_material.emissive.data(),
                      ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::SliderFloat("Emissive Strength", &m_material.emissiveStrength, 0.0f, 20.0f, "%.2fx");

    for (float& channel : m_material.emissive) channel = std::max(0.0f, channel);
    m_material.emissiveStrength = std::max(0.0f, m_material.emissiveStrength);
    for (float& channel : m_material.albedo) channel = Clamp01(channel);
    m_material.metallic = Clamp01(m_material.metallic);
    m_material.roughness = std::max(0.02f, Clamp01(m_material.roughness));
    m_material.ao = Clamp01(m_material.ao);
}

void MaterialMakerPanel::DrawAdvancedControls() {
    if (!ImGui::CollapsingHeader("Advanced Surface")) return;
    ImGui::TextDisabled("Quick setups:");
    if (ImGui::Button("Glass")) {
        m_material.blendMode = 2; m_material.opacity = 0.18f; m_material.transmission = 1.0f;
        m_material.ior = 1.5f; m_material.roughness = 0.08f; m_material.metallic = 0.0f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Car Paint")) {
        m_material.blendMode = 0; m_material.clearcoat = 1.0f;
        m_material.clearcoatRoughness = 0.08f; m_material.metallic = 0.0f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Velvet")) {
        m_material.sheenColor = m_material.albedo; m_material.sheenRoughness = 0.7f;
        m_material.roughness = 0.8f; m_material.metallic = 0.0f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Skin/Wax")) {
        m_material.subsurface = 0.65f; m_material.subsurfaceColor = {1.0f, 0.25f, 0.18f};
        m_material.roughness = 0.48f; m_material.metallic = 0.0f;
    }
    const char* modes[] = {"Opaque", "Masked", "Transparent"};
    ImGui::Combo("Blend Mode", &m_material.blendMode, modes, IM_ARRAYSIZE(modes));
    if (m_material.blendMode != 0)
        ImGui::TextDisabled("Opacity is multiplied by the albedo texture's alpha channel.");
    ImGui::SliderFloat("Opacity", &m_material.opacity, 0.0f, 1.0f, "%.2f");
    if (m_material.blendMode == 1)
        ImGui::SliderFloat("Alpha Cutoff", &m_material.alphaCutoff, 0.0f, 1.0f, "%.2f");

    if (ImGui::TreeNode("UV Transform")) {
        ImGui::DragFloat2("Tiling", m_material.uvScale.data(), 0.02f, 0.01f, 100.0f, "%.2f");
        ImGui::DragFloat2("Offset", m_material.uvOffset.data(), 0.01f, -100.0f, 100.0f, "%.2f");
        ImGui::SliderFloat("Rotation", &m_material.uvRotation, -180.0f, 180.0f, "%.0f deg");
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Surface Detail")) {
        ImGui::SliderFloat("Normal Strength", &m_material.normalStrength, 0.0f, 4.0f, "%.2f");
        ImGui::SliderFloat("Height / Parallax", &m_material.heightScale, 0.0f, 0.12f, "%.3f");
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Coating and Specular")) {
        ImGui::SliderFloat("Specular Level", &m_material.specularLevel, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Clearcoat", &m_material.clearcoat, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Clearcoat Roughness", &m_material.clearcoatRoughness, 0.02f, 1.0f, "%.2f");
        ImGui::SliderFloat("Anisotropy", &m_material.anisotropy, -1.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Anisotropy Rotation", &m_material.anisotropyRotation, -180.0f, 180.0f, "%.0f deg");
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Transmission")) {
        ImGui::TextDisabled("Uses environment refraction; choose Transparent for glass-like blending.");
        ImGui::SliderFloat("Transmission", &m_material.transmission, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Index of Refraction", &m_material.ior, 1.0f, 2.5f, "%.2f");
        ImGui::SliderFloat("Thickness", &m_material.thickness, 0.0f, 10.0f, "%.2f");
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Sheen and Subsurface")) {
        ImGui::ColorEdit3("Sheen Color", m_material.sheenColor.data());
        ImGui::SliderFloat("Sheen Roughness", &m_material.sheenRoughness, 0.02f, 1.0f, "%.2f");
        ImGui::SliderFloat("Subsurface", &m_material.subsurface, 0.0f, 1.0f, "%.2f");
        ImGui::ColorEdit3("Subsurface Color", m_material.subsurfaceColor.data());
        ImGui::TreePop();
    }
    m_material.blendMode = std::max(0, std::min(2, m_material.blendMode));
    m_material.opacity = Clamp01(m_material.opacity); m_material.alphaCutoff = Clamp01(m_material.alphaCutoff);
    m_material.normalStrength = std::max(0.0f, m_material.normalStrength);
    m_material.heightScale = std::max(0.0f, m_material.heightScale);
    m_material.clearcoat = Clamp01(m_material.clearcoat);
    m_material.clearcoatRoughness = std::max(0.02f, Clamp01(m_material.clearcoatRoughness));
    m_material.transmission = Clamp01(m_material.transmission);
    m_material.ior = std::max(1.0f, m_material.ior); m_material.thickness = std::max(0.0f, m_material.thickness);
    m_material.anisotropy = std::max(-1.0f, std::min(1.0f, m_material.anisotropy));
    m_material.sheenRoughness = std::max(0.02f, Clamp01(m_material.sheenRoughness));
    m_material.specularLevel = Clamp01(m_material.specularLevel); m_material.subsurface = Clamp01(m_material.subsurface);
}

void MaterialMakerPanel::DrawTextureControls() {
    ImGui::TextUnformatted("Texture Paths");

    auto pasteTexturePath = [&](std::string& target, char* buffer, std::size_t bufferSize, const char* label) {
        const char* text = ImGui::GetClipboardText();
        const std::string pathText = TrimClipboardPath(text ? text : "");
        if (pathText.empty()) {
            m_status = std::string(label) + " paste failed: clipboard is empty.";
            return;
        }

        const std::filesystem::path path(pathText);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) {
            m_status = std::string(label) + " paste failed: path is not a file.";
            return;
        }
        if (!IsSupportedTexturePath(path)) {
            m_status = std::string(label) + " paste failed: file is not a supported texture.";
            return;
        }

        target = pathText;
        CopyToBuffer(buffer, bufferSize, target);
        m_status = std::string(label) + " texture path pasted.";
    };

    // Accept a texture dragged from the editor's content browser (which emits the
    // "3DGEDITOR_ASSET" payload = the file path) onto the preceding widget.
    auto acceptTextureDrop = [&](std::string& target, char* buffer, std::size_t bufferSize) {
        if (!ImGui::BeginDragDropTarget()) {
            return;
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("3DGEDITOR_ASSET")) {
            const char* dropped = static_cast<const char*>(payload->Data);
            if (dropped) {
                const std::filesystem::path path(dropped);
                if (IsSupportedTexturePath(path)) {
                    target = dropped;
                    CopyToBuffer(buffer, bufferSize, target);
                    m_status = "Assigned dropped texture.";
                } else {
                    m_status = "Dropped file is not a supported texture.";
                }
            }
        }
        ImGui::EndDragDropTarget();
    };

    // Load (cached) and show a thumbnail + status for one map slot.
    auto showThumbnail = [&](const std::string& path) {
        if (path.empty() || !m_preview) {
            return;
        }
        const MaterialPreview::MapInfo info = m_preview->AcquireMap(path);
        if (info.ok && info.textureId != 0) {
            ImGui::Image((ImTextureID)(std::intptr_t)info.textureId, ImVec2(48.0f, 48.0f),
                         ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
            ImGui::SameLine();
            ImGui::Text("%d x %d", info.width, info.height);
        } else {
            ImGui::TextColored(ImVec4(0.90f, 0.40f, 0.35f, 1.0f), "could not decode image");
            if (!info.error.empty()) ImGui::TextWrapped("%s", info.error.c_str());
        }
    };

    ImGui::TextDisabled("Type/paste a path, or drag a texture from the Content browser.");

    if (ImGui::InputText("Albedo Map", m_albedoMapBuffer, sizeof(m_albedoMapBuffer))) {
        m_material.albedoMap = m_albedoMapBuffer;
    }
    acceptTextureDrop(m_material.albedoMap, m_albedoMapBuffer, sizeof(m_albedoMapBuffer));
    ImGui::SameLine();
    if (ImGui::Button("Paste##albedo")) {
        pasteTexturePath(m_material.albedoMap, m_albedoMapBuffer, sizeof(m_albedoMapBuffer), "Albedo");
    }
    showThumbnail(m_material.albedoMap);

    if (ImGui::InputText("Normal Map", m_normalMapBuffer, sizeof(m_normalMapBuffer))) {
        m_material.normalMap = m_normalMapBuffer;
    }
    acceptTextureDrop(m_material.normalMap, m_normalMapBuffer, sizeof(m_normalMapBuffer));
    ImGui::SameLine();
    if (ImGui::Button("Paste##normal")) {
        pasteTexturePath(m_material.normalMap, m_normalMapBuffer, sizeof(m_normalMapBuffer), "Normal");
    }
    showThumbnail(m_material.normalMap);

    if (ImGui::InputText("Metal/Rough Map", m_metalRoughMapBuffer, sizeof(m_metalRoughMapBuffer))) {
        m_material.metalRoughMap = m_metalRoughMapBuffer;
    }
    acceptTextureDrop(m_material.metalRoughMap, m_metalRoughMapBuffer, sizeof(m_metalRoughMapBuffer));
    ImGui::SameLine();
    if (ImGui::Button("Paste##metal_rough")) {
        pasteTexturePath(m_material.metalRoughMap, m_metalRoughMapBuffer, sizeof(m_metalRoughMapBuffer), "Metal/Rough");
    }
    showThumbnail(m_material.metalRoughMap);

    if (ImGui::InputText("Height Map", m_heightMapBuffer, sizeof(m_heightMapBuffer))) {
        m_material.heightMap = m_heightMapBuffer;
    }
    acceptTextureDrop(m_material.heightMap, m_heightMapBuffer, sizeof(m_heightMapBuffer));
    ImGui::SameLine();
    if (ImGui::Button("Paste##height")) {
        pasteTexturePath(m_material.heightMap, m_heightMapBuffer, sizeof(m_heightMapBuffer), "Height");
    }
    showThumbnail(m_material.heightMap);

    DrawOrmPacker();
}

void MaterialMakerPanel::DrawOrmPacker() {
    if (!ImGui::TreeNode("Pack Metal/Rough (ORM)")) {
        return;
    }
    ImGui::TextWrapped("Combine separate grayscale maps into one ORM texture "
                       "(R = AO, G = roughness, B = metallic). PNG/JPG sources.");

    auto sourceRow = [&](const char* label, std::string& target, const char* pasteId) {
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        if (ImGui::Button(pasteId)) {
            const char* text = ImGui::GetClipboardText();
            const std::string picked = TrimClipboardPath(text ? text : "");
            std::error_code ec;
            if (!picked.empty() && std::filesystem::is_regular_file(picked, ec)) {
                target = picked;
            } else {
                m_status = "ORM: clipboard is not a valid file path.";
            }
        }
        ImGui::SameLine();
        ImGui::TextWrapped("%s", target.empty() ? "(none)" : target.c_str());
    };
    sourceRow("Metallic :", m_packMetallic,  "Paste##orm_metal");
    sourceRow("Roughness:", m_packRoughness, "Paste##orm_rough");
    sourceRow("AO (opt) :", m_packAO,        "Paste##orm_ao");

    if (ImGui::Button("Pack ORM -> Metal/Rough")) {
        const std::string stem = SanitizeFileStem(m_material.name);
        const std::string outPath =
            (std::filesystem::path(m_outputDirectory) / (stem + "_ORM.tga")).string();
        const PackResult result = PackMetalRoughAO(m_packMetallic, m_packRoughness, m_packAO, outPath);
        if (result.ok) {
            m_material.metalRoughMap = result.outputPath;
            CopyToBuffer(m_metalRoughMapBuffer, sizeof(m_metalRoughMapBuffer), m_material.metalRoughMap);
            m_status = "Packed ORM: " + result.outputPath;
        } else {
            m_status = "ORM pack failed: " + result.error;
        }
    }
    ImGui::TreePop();
}

void MaterialMakerPanel::DrawModelImport() {
    if (!ImGui::CollapsingHeader("Import from Model")) {
        return;
    }
    ImGui::TextWrapped("Read a material (colours + external texture maps) from a "
                       "model file (glTF / OBJ / FBX). Embedded textures are skipped.");

    ImGui::TextUnformatted("Model:");
    ImGui::SameLine();
    if (ImGui::Button("Paste##model_path")) {
        const char* text = ImGui::GetClipboardText();
        const std::string picked = TrimClipboardPath(text ? text : "");
        std::error_code ec;
        if (!picked.empty() && std::filesystem::is_regular_file(picked, ec)) {
            m_importModelPath = picked;
            m_importMaterialIndex = 0;
            std::string countError;
            m_importMaterialCount = CountModelMaterials(picked, &countError);
            if (m_importMaterialCount == 0) m_status = "Import: " + countError;
        } else {
            m_status = "Import: clipboard is not a valid file path.";
        }
    }
    ImGui::SameLine();
    ImGui::TextWrapped("%s", m_importModelPath.empty() ? "(none)" : m_importModelPath.c_str());

    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderInt("Material index", &m_importMaterialIndex, 0,
                     std::max(0, m_importMaterialCount - 1));
    if (m_importMaterialIndex < 0) {
        m_importMaterialIndex = 0;
    }
    if (m_importMaterialCount > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("%d material(s)", m_importMaterialCount);
    }

    if (ImGui::Button("Import Material") && !m_importModelPath.empty()) {
        const ModelImportResult result =
            ImportMaterialFromModel(m_importModelPath, m_importMaterialIndex, &m_material);
        if (result.ok) {
            m_material.emissiveStrength = 1.0f;
            m_importMaterialCount = result.materialCount;
            bool packFailed = false;
            if (!result.combinedMetalRoughMap.empty()) {
                const std::string outPath =
                    (std::filesystem::path(m_outputDirectory) /
                     (SanitizeFileStem(m_material.name) + "_ORM.tga")).string();
                const PackResult packed = PackCombinedMetalRoughAO(
                    result.combinedMetalRoughMap, result.aoMap, outPath);
                if (packed.ok) m_material.metalRoughMap = packed.outputPath;
                else {
                    packFailed = true;
                    m_status = "Imported values, but ORM normalization failed: " + packed.error;
                }
            } else if (m_material.metalRoughMap.empty() &&
                (!result.metallicMap.empty() || !result.roughnessMap.empty() || !result.aoMap.empty())) {
                const std::string outPath =
                    (std::filesystem::path(m_outputDirectory) /
                     (SanitizeFileStem(m_material.name) + "_ORM.tga")).string();
                const PackResult packed = PackMetalRoughAO(
                    result.metallicMap, result.roughnessMap, result.aoMap, outPath);
                if (packed.ok) {
                    m_material.metalRoughMap = packed.outputPath;
                } else {
                    packFailed = true;
                    m_status = "Imported values, but ORM packing failed: " + packed.error;
                }
            }
            SyncBuffersFromMaterial();
            char line[96];
            std::snprintf(line, sizeof(line), "Imported material %d of %d.",
                          m_importMaterialIndex, result.materialCount);
            if (!packFailed) m_status = line;
        } else {
            m_status = "Import failed: " + result.error;
        }
    }
}

void MaterialMakerPanel::RefreshLibrary() {
    m_libraryFiles.clear();
    m_libraryScanned = true;
    std::error_code ec;
    const std::filesystem::path dir(m_outputDirectory);
    if (!std::filesystem::is_directory(dir, ec)) {
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        std::error_code fec;
        if (std::filesystem::is_regular_file(entry.path(), fec) &&
            LowerExtension(entry.path()) == ".3dgmat") {
            m_libraryFiles.push_back(entry.path().string());
        }
    }
    std::sort(m_libraryFiles.begin(), m_libraryFiles.end());
}

void MaterialMakerPanel::DrawLibraryControls() {
    if (!ImGui::CollapsingHeader("Library")) {
        return;
    }
    if (!m_libraryScanned) {
        RefreshLibrary();
    }

    if (ImGui::Button("Refresh")) {
        RefreshLibrary();
    }
    ImGui::SameLine();
    if (ImGui::Button("New")) {
        Reset();
        m_lastSavedPath.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate")) {
        m_material.name += "_copy";
        SyncBuffersFromMaterial();
        if (Save()) {
            RefreshLibrary();
        }
    }

    ImGui::BeginChild("##material_library", ImVec2(-1.0f, 150.0f), true);
    if (m_libraryFiles.empty()) {
        ImGui::TextDisabled("No .3dgmat files in the output folder.");
    }
    for (const std::string& file : m_libraryFiles) {
        const std::string label = std::filesystem::path(file).filename().string();
        const bool selected = (file == m_lastSavedPath);
        if (ImGui::Selectable(label.c_str(), selected)) {
            LoadFromFile(file);
            RefreshLibrary();
        }
    }
    ImGui::EndChild();
    ImGui::Text("%zu material(s)", m_libraryFiles.size());
}

void MaterialMakerPanel::DrawExportControls() {
    if (m_hasSavedSnapshot && SameMaterial(m_material, m_savedSnapshot)) {
        ImGui::TextColored(ImVec4(0.45f, 0.82f, 0.58f, 1.0f), "No unsaved changes");
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.35f, 1.0f), "* Unsaved changes");
    }

    if (ImGui::Button("Save .3dgmat")) {
        Save();
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy C++")) {
        const std::string cpp = ToCppInitializer(m_material);
        ImGui::SetClipboardText(cpp.c_str());
        m_status = "Copied C++ initializer.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        Reset();
    }

    if (ImGui::InputText("Output Folder", m_outputDirectoryBuffer, sizeof(m_outputDirectoryBuffer))) {
        m_outputDirectory = m_outputDirectoryBuffer;
        m_libraryScanned = false;
    }

    if (!m_status.empty()) {
        ImGui::TextWrapped("%s", m_status.c_str());
    }

    if (ImGui::TreeNode("JSON Preview")) {
        const std::string json = ToJson(m_material);
        ImGui::BeginChild("##material_json", ImVec2(-1.0f, 180.0f), true);
        ImGui::TextUnformatted(json.c_str());
        ImGui::EndChild();
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("C++ Initializer")) {
        const std::string cpp = ToCppInitializer(m_material);
        ImGui::BeginChild("##material_cpp", ImVec2(-1.0f, 120.0f), true);
        ImGui::TextUnformatted(cpp.c_str());
        ImGui::EndChild();
        ImGui::TreePop();
    }
}

void MaterialMakerPanel::Reset() {
    m_material = MaterialDocument{};
    m_material.name = "CopperSoft";
    m_material.albedo = {0.95f, 0.64f, 0.52f};
    m_material.metallic = 0.7f;
    m_material.roughness = 0.38f;
    m_material.ao = 1.0f;

    SyncBuffersFromMaterial();
    m_savedSnapshot = m_material;
    m_hasSavedSnapshot = false;
    m_lastSavedPath.clear();
    m_status = "Ready.";
}

void MaterialMakerPanel::SyncBuffersFromMaterial() {
    CopyToBuffer(m_nameBuffer, sizeof(m_nameBuffer), m_material.name);
    CopyToBuffer(m_outputDirectoryBuffer, sizeof(m_outputDirectoryBuffer), m_outputDirectory);
    CopyToBuffer(m_albedoMapBuffer, sizeof(m_albedoMapBuffer), m_material.albedoMap);
    CopyToBuffer(m_normalMapBuffer, sizeof(m_normalMapBuffer), m_material.normalMap);
    CopyToBuffer(m_metalRoughMapBuffer, sizeof(m_metalRoughMapBuffer), m_material.metalRoughMap);
    CopyToBuffer(m_heightMapBuffer, sizeof(m_heightMapBuffer), m_material.heightMap);
    CopyToBuffer(m_shaderPathBuffer, sizeof(m_shaderPathBuffer), m_material.shaderPath);
}

void MaterialMakerPanel::DrawPreview() {
    ImGui::TextUnformatted("Preview");
    if (m_preview) {
        ImGui::SameLine();
        ImGui::Checkbox("Live", &m_useLivePreview);   // always toggleable
    }

    // Try the live, engine-rendered PBR preview first; fall back to the hand-drawn
    // approximation if it is unavailable (no GL context / creation failed).
    unsigned int texture = 0;
    int side = 0;
    if (m_useLivePreview && m_preview) {
        side = static_cast<int>(std::min(ImGui::GetContentRegionAvail().x, 260.0f));
        if (side < 64) side = 64;

        // Build the same scalar material and texture bindings used by the engine.
        engine::ecs::PbrMaterial pbr;
        pbr.albedo    = glm::vec3(m_material.albedo[0], m_material.albedo[1], m_material.albedo[2]);
        pbr.metallic  = m_material.metallic;
        pbr.roughness = m_material.roughness;
        pbr.ao        = m_material.ao;
        const float es = m_material.emissiveStrength;
        pbr.emissive  = glm::vec3(m_material.emissive[0] * es, m_material.emissive[1] * es,
                                  m_material.emissive[2] * es);
        pbr.blendMode = static_cast<engine::ecs::PbrMaterial::BlendMode>(m_material.blendMode);
        pbr.opacity = m_material.opacity; pbr.alphaCutoff = m_material.alphaCutoff;
        pbr.uvScale = glm::vec2(m_material.uvScale[0], m_material.uvScale[1]);
        pbr.uvOffset = glm::vec2(m_material.uvOffset[0], m_material.uvOffset[1]);
        pbr.uvRotation = m_material.uvRotation; pbr.normalStrength = m_material.normalStrength;
        pbr.heightScale = m_material.heightScale; pbr.clearcoat = m_material.clearcoat;
        pbr.clearcoatRoughness = m_material.clearcoatRoughness; pbr.transmission = m_material.transmission;
        pbr.ior = m_material.ior; pbr.thickness = m_material.thickness;
        pbr.anisotropy = m_material.anisotropy; pbr.anisotropyRotation = m_material.anisotropyRotation;
        pbr.sheenColor = glm::vec3(m_material.sheenColor[0], m_material.sheenColor[1], m_material.sheenColor[2]);
        pbr.sheenRoughness = m_material.sheenRoughness; pbr.specularLevel = m_material.specularLevel;
        pbr.subsurface = m_material.subsurface;
        pbr.subsurfaceColor = glm::vec3(m_material.subsurfaceColor[0], m_material.subsurfaceColor[1], m_material.subsurfaceColor[2]);

        MaterialPreview::Settings s;
        s.size           = side;
        s.yawDeg         = m_previewYaw;
        s.pitchDeg       = m_previewPitch;
        s.shape          = static_cast<MaterialPreview::Shape>(m_previewShape);
        s.channel        = static_cast<MaterialPreview::Channel>(m_previewChannel);
        s.envTime        = m_previewEnvApplied;
        s.envYawDeg      = m_previewEnvYawApplied;
        s.lightIntensity = m_previewLight;
        s.groundPlane    = m_previewGround;
        s.background     = glm::vec3(m_previewBg[0], m_previewBg[1], m_previewBg[2]);
        s.albedoMapPath     = m_material.albedoMap;
        s.normalMapPath     = m_material.normalMap;
        s.metalRoughMapPath = m_material.metalRoughMap;
        s.heightMapPath     = m_material.heightMap;

        texture = m_preview->Render(pbr, s);
    }

    if (texture != 0) {
        const ImVec2 imageSize(static_cast<float>(side), static_cast<float>(side));
        // GL textures have a bottom-left origin, so flip V to display upright.
        ImGui::Image((ImTextureID)(std::intptr_t)texture,
                     imageSize, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));

        // Drag on the image to orbit the camera.
        if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            m_previewYaw += delta.x * 0.4f;
            m_previewPitch = std::max(-85.0f, std::min(85.0f, m_previewPitch - delta.y * 0.4f));
        }

        const char* shapes[]   = {"Sphere", "Cube", "Plane"};
        const char* channels[] = {"Full (PBR)", "Albedo", "Metallic", "Roughness", "Normals", "AO"};
        ImGui::SetNextItemWidth(110.0f);
        ImGui::Combo("Shape", &m_previewShape, shapes, IM_ARRAYSIZE(shapes));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::Combo("View", &m_previewChannel, channels, IM_ARRAYSIZE(channels));

        if (ImGui::TreeNode("Environment")) {
            ImGui::SliderFloat("Time of day", &m_previewEnv, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemDeactivatedAfterEdit()) m_previewEnvApplied = m_previewEnv;
            ImGui::SliderFloat("Rotate", &m_previewEnvYaw, -180.0f, 180.0f, "%.0f deg");
            if (ImGui::IsItemDeactivatedAfterEdit()) m_previewEnvYawApplied = m_previewEnvYaw;
            ImGui::SliderFloat("Light", &m_previewLight, 0.0f, 3.0f, "%.2f");
            ImGui::Checkbox("Ground + shadow", &m_previewGround);
            ImGui::ColorEdit3("Background", m_previewBg, ImGuiColorEditFlags_NoInputs);
            ImGui::TreePop();
        }
        return;
    }

    if (m_preview && !m_preview->LastError().empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f),
                           "Live preview unavailable: %s", m_preview->LastError().c_str());
        if (ImGui::Button("Retry Live Preview")) m_preview->Retry();
    }
    DrawApproxPreview();
}

void MaterialMakerPanel::DrawApproxPreview() {
    const ImVec2 canvasSize(ImGui::GetContentRegionAvail().x, 230.0f);
    ImGui::BeginChild("##material_preview", canvasSize, true, ImGuiWindowFlags_NoScrollbar);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    const ImVec2 max(origin.x + size.x, origin.y + size.y);
    drawList->AddRectFilled(origin, max, ColorU32(0.075f, 0.08f, 0.09f));

    constexpr float checker = 18.0f;
    for (float y = origin.y; y < max.y; y += checker) {
        for (float x = origin.x; x < max.x; x += checker) {
            const int ix = static_cast<int>((x - origin.x) / checker);
            const int iy = static_cast<int>((y - origin.y) / checker);
            if (((ix + iy) & 1) == 0) {
                drawList->AddRectFilled(ImVec2(x, y),
                    ImVec2(std::min(x + checker, max.x), std::min(y + checker, max.y)),
                    ColorU32(0.095f, 0.10f, 0.115f));
            }
        }
    }

    const float radius = std::min(size.x, size.y) * 0.32f;
    const ImVec2 center(origin.x + size.x * 0.42f, origin.y + size.y * 0.52f);
    drawList->AddCircleFilled(ImVec2(center.x + radius * 0.10f, center.y + radius * 0.86f),
        radius * 0.62f,
        ColorU32(0.0f, 0.0f, 0.0f, 0.18f),
        64);

    for (int i = 36; i >= 0; --i) {
        const float t = static_cast<float>(i) / 36.0f;
        const float r = radius * t;
        const float edgeShade = 0.30f + (1.0f - t) * 0.82f;
        const float metallicShade = 1.0f + m_material.metallic * (1.0f - t) * 0.22f;
        drawList->AddCircleFilled(center, r, MaterialColor(m_material, edgeShade * metallicShade));
    }

    const float highlightRadius = radius * (0.12f + (1.0f - m_material.roughness) * 0.18f);
    const float highlightAlpha = 0.28f + (1.0f - m_material.roughness) * 0.50f + m_material.metallic * 0.16f;
    const ImVec2 highlight(center.x - radius * 0.34f, center.y - radius * 0.38f);
    drawList->AddCircleFilled(highlight, highlightRadius * 1.8f, ColorU32(1.0f, 1.0f, 1.0f, highlightAlpha * 0.12f));
    drawList->AddCircleFilled(highlight, highlightRadius, ColorU32(1.0f, 1.0f, 1.0f, highlightAlpha));

    if (m_material.emissive[0] > 0.0f || m_material.emissive[1] > 0.0f || m_material.emissive[2] > 0.0f) {
        drawList->AddCircle(center,
            radius + 4.0f,
            ColorU32(m_material.emissive[0], m_material.emissive[1], m_material.emissive[2], 0.92f),
            96,
            4.0f);
    }

    const ImVec2 info(origin.x + size.x * 0.68f, origin.y + 28.0f);
    drawList->AddText(info, ColorU32(0.86f, 0.89f, 0.94f), "PBR");
    char line[96];
    std::snprintf(line, sizeof(line), "Metallic %.2f", m_material.metallic);
    drawList->AddText(ImVec2(info.x, info.y + 28.0f), ColorU32(0.70f, 0.75f, 0.82f), line);
    std::snprintf(line, sizeof(line), "Roughness %.2f", m_material.roughness);
    drawList->AddText(ImVec2(info.x, info.y + 50.0f), ColorU32(0.70f, 0.75f, 0.82f), line);
    std::snprintf(line, sizeof(line), "AO %.2f", m_material.ao);
    drawList->AddText(ImVec2(info.x, info.y + 72.0f), ColorU32(0.70f, 0.75f, 0.82f), line);

    const bool hasAnyMap = !m_material.albedoMap.empty() || !m_material.normalMap.empty() ||
                           !m_material.metalRoughMap.empty() || !m_material.heightMap.empty();
    drawList->AddText(ImVec2(info.x, info.y + 108.0f),
        hasAnyMap ? ColorU32(0.45f, 0.82f, 0.58f) : ColorU32(0.60f, 0.64f, 0.70f),
        hasAnyMap ? "Maps assigned" : "No maps");

    ImGui::Dummy(size);
    ImGui::EndChild();
}

bool MaterialMakerPanel::Save() {
    std::string error;
    if (SaveMaterialFile(m_material, m_outputDirectory, &m_lastSavedPath, &error)) {
        m_status = "Saved " + m_lastSavedPath;
        m_savedSnapshot = m_material;
        m_hasSavedSnapshot = true;
        m_savedThisFrame = true;
        RefreshLibrary();
        return true;
    }
    m_status = "Save failed: " + error;
    return false;
}

} // namespace material_maker
