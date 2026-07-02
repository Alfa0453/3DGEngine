#include "MaterialMaker/MaterialMakerPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
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

} // namespace

MaterialMakerPanel::MaterialMakerPanel(std::string outputDirectory)
    : m_outputDirectory(std::move(outputDirectory)) {
    Reset();
}

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
    DrawSurfaceControls();
    ImGui::Separator();
    DrawTextureControls();
    ImGui::Separator();
    DrawExportControls();
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
    m_lastSavedPath = path;
    m_outputDirectory = std::filesystem::path(path).parent_path().string();
    SyncBuffersFromMaterial();
    m_status = "Loaded " + path;
    return true;
}

void MaterialMakerPanel::SetOutputDirectory(const std::string& outputDirectory) {
    m_outputDirectory = outputDirectory;
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

void MaterialMakerPanel::DrawSurfaceControls() {
    if (ImGui::InputText("Name", m_nameBuffer, sizeof(m_nameBuffer))) {
        m_material.name = m_nameBuffer;
    }

    ImGui::ColorEdit3("Albedo", m_material.albedo.data());
    ImGui::SliderFloat("Metallic", &m_material.metallic, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Roughness", &m_material.roughness, 0.02f, 1.0f, "%.2f");
    ImGui::SliderFloat("AO", &m_material.ao, 0.0f, 1.0f, "%.2f");
    ImGui::ColorEdit3("Emissive", m_material.emissive.data(), ImGuiColorEditFlags_Float);

    for (float& channel : m_material.albedo) channel = Clamp01(channel);
    m_material.metallic = Clamp01(m_material.metallic);
    m_material.roughness = std::max(0.02f, Clamp01(m_material.roughness));
    m_material.ao = Clamp01(m_material.ao);
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

    if (ImGui::InputText("Albedo Map", m_albedoMapBuffer, sizeof(m_albedoMapBuffer))) {
        m_material.albedoMap = m_albedoMapBuffer;
    }
    ImGui::SameLine();
    if (ImGui::Button("Paste##albedo")) {
        pasteTexturePath(m_material.albedoMap, m_albedoMapBuffer, sizeof(m_albedoMapBuffer), "Albedo");
    }
    if (ImGui::InputText("Normal Map", m_normalMapBuffer, sizeof(m_normalMapBuffer))) {
        m_material.normalMap = m_normalMapBuffer;
    }
    ImGui::SameLine();
    if (ImGui::Button("Paste##normal")) {
        pasteTexturePath(m_material.normalMap, m_normalMapBuffer, sizeof(m_normalMapBuffer), "Normal");
    }
    if (ImGui::InputText("Metal/Rough Map", m_metalRoughMapBuffer, sizeof(m_metalRoughMapBuffer))) {
        m_material.metalRoughMap = m_metalRoughMapBuffer;
    }
    ImGui::SameLine();
    if (ImGui::Button("Paste##metal_rough")) {
        pasteTexturePath(m_material.metalRoughMap, m_metalRoughMapBuffer, sizeof(m_metalRoughMapBuffer), "Metal/Rough");
    }
}

void MaterialMakerPanel::DrawExportControls() {
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
    m_status = "Ready.";
}

void MaterialMakerPanel::SyncBuffersFromMaterial() {
    CopyToBuffer(m_nameBuffer, sizeof(m_nameBuffer), m_material.name);
    CopyToBuffer(m_outputDirectoryBuffer, sizeof(m_outputDirectoryBuffer), m_outputDirectory);
    CopyToBuffer(m_albedoMapBuffer, sizeof(m_albedoMapBuffer), m_material.albedoMap);
    CopyToBuffer(m_normalMapBuffer, sizeof(m_normalMapBuffer), m_material.normalMap);
    CopyToBuffer(m_metalRoughMapBuffer, sizeof(m_metalRoughMapBuffer), m_material.metalRoughMap);
}

void MaterialMakerPanel::DrawPreview() {
    ImGui::TextUnformatted("Preview");

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

    const bool hasAnyMap = !m_material.albedoMap.empty() || !m_material.normalMap.empty() || !m_material.metalRoughMap.empty();
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
        m_savedThisFrame = true;
        return true;
    }
    m_status = "Save failed: " + error;
    return false;
}

} // namespace material_maker
