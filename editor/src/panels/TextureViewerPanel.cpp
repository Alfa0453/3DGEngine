#include "TextureViewerPanel.h"

#include "EditorAssets.h"
#include "EditorPanels.h"

#include <engine/assets/RuntimeAssetManager.h>
#include <engine/assets/TextureAsset.h>
#include <engine/graphics/Texture.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>

namespace {

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool ContainsInsensitive(const std::string& value, const char* filter) {
    if (!filter || !*filter) return true;
    return Lower(value).find(Lower(filter)) != std::string::npos;
}

std::string FullPath(const EditorAssets& assets, const std::string& path) {
    const std::filesystem::path input(path);
    return input.is_absolute() ? input.lexically_normal().string()
        : (std::filesystem::path(assets.RootPath()) / input).lexically_normal().string();
}

void DrawCheckerboard(ImDrawList* draw, const ImVec2& topLeft,
                      const ImVec2& size, float cell = 16.0f) {
    const ImU32 dark = IM_COL32(54, 58, 66, 255);
    const ImU32 light = IM_COL32(91, 96, 107, 255);
    const int columns = std::max(1, static_cast<int>(std::ceil(size.x / cell)));
    const int rows = std::max(1, static_cast<int>(std::ceil(size.y / cell)));
    const ImVec2 windowMin = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    const ImVec2 windowMax(windowMin.x + windowSize.x, windowMin.y + windowSize.y);
    const int firstColumn = std::clamp(
        static_cast<int>(std::floor((windowMin.x - topLeft.x) / cell)), 0, columns);
    const int lastColumn = std::clamp(
        static_cast<int>(std::ceil((windowMax.x - topLeft.x) / cell)), 0, columns);
    const int firstRow = std::clamp(
        static_cast<int>(std::floor((windowMin.y - topLeft.y) / cell)), 0, rows);
    const int lastRow = std::clamp(
        static_cast<int>(std::ceil((windowMax.y - topLeft.y) / cell)), 0, rows);
    for (int y = firstRow; y < lastRow; ++y) {
        for (int x = firstColumn; x < lastColumn; ++x) {
            const ImVec2 a(topLeft.x + x * cell, topLeft.y + y * cell);
            const ImVec2 b(std::min(topLeft.x + size.x, a.x + cell),
                           std::min(topLeft.y + size.y, a.y + cell));
            draw->AddRectFilled(a, b, ((x + y) & 1) ? dark : light);
        }
    }
}

} // namespace

void TextureViewerPanel::QueueOpen(const std::string& path) {
    m_pendingOpen = path;
}

void TextureViewerPanel::Refresh(EditorAssets& assets) {
    m_root = assets.RootPath();
    m_textures = assets.ContentAssetPaths(EditorAssets::Type::Texture);
    if (!m_path.empty()) {
        const std::string selected = FullPath(assets, m_path);
        const bool stillPresent = std::any_of(m_textures.begin(), m_textures.end(),
            [&](const std::string& candidate) {
                return FullPath(assets, candidate) == selected;
            });
        if (!stillPresent && !std::filesystem::exists(selected)) m_path.clear();
    }
}

void TextureViewerPanel::Select(const std::string& path) {
    m_path = path;
    m_zoom = 1.0f;
    m_fit = true;
}

void TextureViewerPanel::Draw(EditorAssets& assets,
                              engine::RuntimeAssetManager& runtimeAssets,
                              bool* open) {
    if (m_root != assets.RootPath()) Refresh(assets);
    if (!m_pendingOpen.empty()) {
        Select(m_pendingOpen);
        m_pendingOpen.clear();
        if (m_textures.empty()) Refresh(assets);
    }

    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::TextureViewer), open,
                      ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginMenuBar()) {
        if (ImGui::MenuItem("Refresh")) Refresh(assets);
        if (ImGui::MenuItem("Fit", nullptr, m_fit)) m_fit = !m_fit;
        if (ImGui::MenuItem("Checkerboard", nullptr, m_checkerboard))
            m_checkerboard = !m_checkerboard;
        ImGui::EndMenuBar();
    }

    const float browserWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.25f,
                                          220.0f, 340.0f);
    if (ImGui::BeginChild("##TextureThumbnails", ImVec2(browserWidth, 0.0f), true)) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##TextureFilter", "Search textures...",
                                 m_filter, sizeof(m_filter));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##ThumbnailSize", &m_thumbnailSize, 36.0f, 96.0f,
                           "Thumbnail %.0f px");
        ImGui::Separator();

        std::vector<const std::string*> visibleTextures;
        visibleTextures.reserve(m_textures.size());
        for (const std::string& relative : m_textures) {
            if (ContainsInsensitive(relative, m_filter)) visibleTextures.push_back(&relative);
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visibleTextures.size()),
                      m_thumbnailSize + ImGui::GetStyle().ItemSpacing.y);
        while (clipper.Step()) {
          for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
            const std::string& relative = *visibleTextures[static_cast<std::size_t>(index)];
            ImGui::PushID(relative.c_str());
            const std::string full = FullPath(assets, relative);
            std::string error;
            const engine::Texture* thumbnail = runtimeAssets.LoadTexture(full, &error);
            const bool selected = !m_path.empty() && FullPath(assets, m_path) == full;

            const ImVec2 rowStart = ImGui::GetCursorScreenPos();
            const float rowWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
            if (ImGui::InvisibleButton("##TextureRow", ImVec2(rowWidth, m_thumbnailSize)))
                Select(full);
            const bool hovered = ImGui::IsItemHovered();
            ImDrawList* list = ImGui::GetWindowDrawList();
            if (selected || hovered) {
                list->AddRectFilled(rowStart,
                    ImVec2(rowStart.x + rowWidth, rowStart.y + m_thumbnailSize),
                    ImGui::GetColorU32(selected ? ImGuiCol_Header : ImGuiCol_HeaderHovered));
            }
            if (thumbnail) {
                const float aspect = static_cast<float>(std::max(thumbnail->Width(), 1))
                    / static_cast<float>(std::max(thumbnail->Height(), 1));
                ImVec2 imageSize(m_thumbnailSize, m_thumbnailSize);
                if (aspect > 1.0f) imageSize.y /= aspect;
                else imageSize.x *= aspect;
                const ImVec2 imageMin(
                    rowStart.x + (m_thumbnailSize - imageSize.x) * 0.5f,
                    rowStart.y + (m_thumbnailSize - imageSize.y) * 0.5f);
                list->AddImage((ImTextureID)(std::intptr_t)thumbnail->ID(), imageMin,
                    ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y),
                    ImVec2(0, 1), ImVec2(1, 0));
            } else {
                list->AddRect(rowStart,
                    ImVec2(rowStart.x + m_thumbnailSize, rowStart.y + m_thumbnailSize),
                    ImGui::GetColorU32(ImGuiCol_Border));
            }
            const std::string fileName = std::filesystem::path(relative).filename().string();
            list->AddText(ImVec2(rowStart.x + m_thumbnailSize + 8.0f,
                                 rowStart.y + (m_thumbnailSize - ImGui::GetTextLineHeight()) * 0.5f),
                          ImGui::GetColorU32(ImGuiCol_Text), fileName.c_str());
            if (hovered) {
                ImGui::SetTooltip("Content/%s%s%s", relative.c_str(),
                    error.empty() ? "" : "\n", error.c_str());
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) Select(full);
            }
            ImGui::PopID();
          }
        }
        if (visibleTextures.empty()) ImGui::TextDisabled("No matching texture assets.");
    }
    ImGui::EndChild();

    ImGui::SameLine();
    if (ImGui::BeginChild("##TexturePreview", ImVec2(0.0f, 0.0f), true,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        if (m_path.empty()) {
            ImGui::TextDisabled("Double-click a texture in Assets or choose a thumbnail.");
        } else {
            const std::string full = FullPath(assets, m_path);
            std::string error;
            const engine::Texture* texture = runtimeAssets.LoadTexture(full, &error);
            if (!texture) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                   "Could not preview texture: %s", error.c_str());
            } else {
                const int width = texture->Width();
                const int height = texture->Height();
                engine::TextureAssetData native;
                const bool nativeAsset = Lower(std::filesystem::path(full).extension().string())
                        == ".3dgtex"
                    && engine::LoadTextureAsset(full, &native, nullptr);

                ImGui::TextUnformatted(std::filesystem::path(full).filename().string().c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("%d x %d | %.2f MB", width, height,
                    static_cast<double>(width) * static_cast<double>(height) * 4.0
                        / (1024.0 * 1024.0));
                if (nativeAsset) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("| %s | %s | %zu mip(s)",
                        native.srgb ? "sRGB" : "Linear",
                        native.smooth ? "Smooth" : "Nearest",
                        native.mipmaps.size() + 1u);
                }
                ImGui::SameLine();
                ImGui::Checkbox("Fit", &m_fit);
                ImGui::SameLine();
                ImGui::Checkbox("Checkerboard", &m_checkerboard);
                if (!m_fit) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(150.0f);
                    ImGui::SliderFloat("Zoom", &m_zoom, 0.05f, 16.0f, "%.2fx",
                                       ImGuiSliderFlags_Logarithmic);
                }
                ImGui::TextDisabled("%s", full.c_str());
                ImGui::Separator();

                ImVec2 available = ImGui::GetContentRegionAvail();
                available.x = std::max(available.x, 32.0f);
                available.y = std::max(available.y, 32.0f);
                float scale = m_zoom;
                if (m_fit) {
                    scale = std::min(available.x / static_cast<float>(std::max(width, 1)),
                                     available.y / static_cast<float>(std::max(height, 1)));
                    scale = std::min(scale, 1.0f);
                }
                const ImVec2 imageSize(std::max(1.0f, width * scale),
                                       std::max(1.0f, height * scale));
                if (m_fit) {
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX()
                        + std::max(0.0f, (available.x - imageSize.x) * 0.5f));
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY()
                        + std::max(0.0f, (available.y - imageSize.y) * 0.5f));
                }
                const ImVec2 position = ImGui::GetCursorScreenPos();
                if (m_checkerboard)
                    DrawCheckerboard(ImGui::GetWindowDrawList(), position, imageSize);
                ImGui::Image((ImTextureID)(std::intptr_t)texture->ID(), imageSize,
                             ImVec2(0, 1), ImVec2(1, 0));
                if (ImGui::IsItemHovered() && !m_fit && ImGui::GetIO().MouseWheel != 0.0f)
                    m_zoom = std::clamp(m_zoom * std::pow(1.15f, ImGui::GetIO().MouseWheel),
                                        0.05f, 16.0f);
            }
        }
    }
    ImGui::EndChild();
    ImGui::End();
}
