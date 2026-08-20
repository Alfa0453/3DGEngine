#include "TerrainCreatorPanel.h"

#include "EditorAssets.h"
#include "EditorScene.h"
#include "EditorPanels.h"

#include <engine/graphics/Terrain.h>
#include <engine/assets/MaterialAssetLoader.h>
#include <engine/assets/TextureAsset.h>
#include <engine/graphics/ImageDecode.h>
#include <engine/graphics/Camera.h>
#include <engine/graphics/DayNightCycle.h>
#include <engine/graphics/GrassField.h>
#include <engine/graphics/ProceduralSky.h>
#include <engine/graphics/Shader.h>

#include <imgui.h>
#include <glad/glad.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <vector>

namespace {

const char* ToolName(TerrainCreatorPanel::Tool tool) {
    switch (tool) {
        case TerrainCreatorPanel::Tool::Raise: return "Raise";
        case TerrainCreatorPanel::Tool::Lower: return "Lower";
        case TerrainCreatorPanel::Tool::Smooth: return "Smooth";
        case TerrainCreatorPanel::Tool::Flatten: return "Flatten";
        case TerrainCreatorPanel::Tool::Paint: return "Paint";
        case TerrainCreatorPanel::Tool::ErasePaint: return "Erase Paint";
    }
    return "Raise";
}

float SampleHeight(const engine::TerrainAssetData& asset, float u, float v) {
    const int res = asset.resolution;
    if (res < 2 || asset.heights.size() != static_cast<std::size_t>(res) * res)
        return 0.0f;
    const float gx = glm::clamp(u, 0.0f, 1.0f) * static_cast<float>(res - 1);
    const float gz = glm::clamp(v, 0.0f, 1.0f) * static_cast<float>(res - 1);
    const int x0 = static_cast<int>(std::floor(gx));
    const int z0 = static_cast<int>(std::floor(gz));
    const int x1 = std::min(x0 + 1, res - 1);
    const int z1 = std::min(z0 + 1, res - 1);
    const float tx = gx - static_cast<float>(x0);
    const float tz = gz - static_cast<float>(z0);
    const auto at = [&](int x, int z) {
        return asset.heights[static_cast<std::size_t>(z) * res + x];
    };
    return glm::mix(glm::mix(at(x0, z0), at(x1, z0), tx),
                    glm::mix(at(x0, z1), at(x1, z1), tx), tz);
}

std::uint8_t SamplePaint(const engine::TerrainAssetData& asset, float u, float v) {
    const int res = asset.resolution;
    if (res < 2 || asset.paint.size() != static_cast<std::size_t>(res) * res)
        return 0;
    const int x = std::clamp(static_cast<int>(std::round(glm::clamp(u, 0.0f, 1.0f)
        * static_cast<float>(res - 1))), 0, res - 1);
    const int z = std::clamp(static_cast<int>(std::round(glm::clamp(v, 0.0f, 1.0f)
        * static_cast<float>(res - 1))), 0, res - 1);
    return asset.paint[static_cast<std::size_t>(z) * res + x];
}

ImU32 LayerColor(std::uint8_t layer, float heightFraction,
                 const glm::vec3 previewColors[6], float surfaceLight,
                 float fogAmount, const glm::vec3& fogColor) {
    glm::vec3 base = layer > 0 ? previewColors[std::min<int>(layer, 5)]
                               : glm::vec3(0.20f, 0.24f, 0.17f);
    const glm::vec3 lit = glm::clamp(base * (0.55f + heightFraction * 0.45f)
                                     * surfaceLight, glm::vec3(0.0f), glm::vec3(1.0f));
    const glm::vec3 result = glm::mix(lit, fogColor, glm::clamp(fogAmount, 0.0f, 1.0f));
    return ImGui::ColorConvertFloat4ToU32({result.r, result.g, result.b, 1.0f});
}

} // namespace

TerrainCreatorPanel::~TerrainCreatorPanel() = default;

void TerrainCreatorPanel::QueueOpen(const std::string& path) {
    m_pendingOpen = path;
}

void TerrainCreatorPanel::NewLandscape() {
    m_asset = engine::TerrainAssetData{};
    m_path.clear();
    m_name = "Landscape";
    Generate();
    m_dirty = false;
}

void TerrainCreatorPanel::Generate() {
    const engine::Heightmap generated = engine::GenerateFbmHeightmap(
        m_asset.resolution, m_asset.size, glm::vec3(0.0f), m_asset.maxHeight,
        m_asset.seed, m_asset.octaves, m_asset.frequency);
    m_asset.heights = generated.h;
    m_asset.paint.assign(m_asset.heights.size(), 0);
    m_previewTerrainDirty = true;
    m_previewGrassDirty = true;
    m_dirty = true;
}

void TerrainCreatorPanel::RefreshMaterialPreview(const std::string& contentRoot) {
    std::string signature;
    for (int layer = 1; layer <= 5; ++layer)
        signature += m_asset.layerMaterials[layer] + '\n';
    if (signature == m_materialPreviewSignature) return;
    m_materialPreviewSignature = std::move(signature);
    glm::vec3 defaults[6];
    engine::DefaultTerrainLayerColors(defaults);
    std::copy(std::begin(defaults), std::end(defaults), std::begin(m_previewLayerColors));

    auto averagePixels = [](const std::vector<std::uint8_t>& pixels) {
        glm::vec3 sum(0.0f);
        std::size_t samples = 0;
        const std::size_t pixelCount = pixels.size() / 4;
        const std::size_t stride = std::max<std::size_t>(1, pixelCount / 2048);
        for (std::size_t p = 0; p < pixelCount; p += stride) {
            const std::size_t offset = p * 4;
            sum += glm::vec3(pixels[offset], pixels[offset + 1], pixels[offset + 2]);
            ++samples;
        }
        return samples ? sum / (255.0f * static_cast<float>(samples)) : glm::vec3(1.0f);
    };
    for (int layer = 1; layer <= 5; ++layer) {
        const std::string& relative = m_asset.layerMaterials[layer];
        if (relative.empty()) continue;
        const std::filesystem::path materialFile = std::filesystem::path(relative).is_absolute()
            ? std::filesystem::path(relative)
            : std::filesystem::path(contentRoot) / relative;
        engine::RuntimeMaterialAsset material;
        std::string ignored;
        if (!engine::LoadMaterialAssetFile(materialFile.string(), &material, &ignored)) continue;
        glm::vec3 color = material.material.albedo;
        if (!material.albedoMapPath.empty()) {
            const std::filesystem::path textureFile =
                std::filesystem::path(material.albedoMapPath).is_absolute()
                    ? std::filesystem::path(material.albedoMapPath)
                    : std::filesystem::path(contentRoot) / material.albedoMapPath;
            std::string extension = textureFile.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            try {
                std::vector<std::uint8_t> pixels;
                if (extension == ".3dgtex") {
                    engine::TextureAssetData texture;
                    if (engine::LoadTextureAsset(textureFile.string(), &texture, &ignored))
                        pixels = std::move(texture.rgba);
                } else {
                    engine::image::Image image;
                    if (extension == ".png") image = engine::image::DecodePNG(textureFile.string());
                    else if (extension == ".jpg" || extension == ".jpeg")
                        image = engine::image::DecodeJPEG(textureFile.string());
                    pixels = std::move(image.rgba);
                }
                if (!pixels.empty()) color *= averagePixels(pixels);
            } catch (...) {}
        }
        m_previewLayerColors[layer] = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
    }
    m_previewTerrainDirty = true;
    m_previewGrassDirty = true;
}

bool TerrainCreatorPanel::Load(const std::string& path, std::string* error) {
    engine::TerrainAssetData loaded;
    if (!engine::LoadTerrainAsset(path, &loaded, error)) return false;
    m_asset = std::move(loaded);
    m_path = path;
    m_name = m_asset.name;
    m_previewTerrainDirty = true;
    m_previewGrassDirty = true;
    m_dirty = false;
    return true;
}

bool TerrainCreatorPanel::Save(const std::string& contentRoot, std::string* error) {
    m_asset.name = m_name.empty() ? "Landscape" : m_name;
    if (m_path.empty()) {
        std::filesystem::path directory =
            std::filesystem::path(contentRoot) / "GameAssets" / "Terrains";
        m_path = (directory / (m_asset.name + ".3dgterrain")).string();
    }
    if (std::filesystem::path(m_path).extension() != ".3dgterrain")
        m_path += ".3dgterrain";
    if (!engine::SaveTerrainAsset(m_path, m_asset, error)) return false;
    m_dirty = false;
    return true;
}

void TerrainCreatorPanel::ApplyBrush(float localX, float localZ, float dt) {
    const int res = m_asset.resolution;
    if (res < 2 || m_asset.heights.size() != static_cast<std::size_t>(res) * res)
        return;
    if (m_asset.paint.size() != m_asset.heights.size())
        m_asset.paint.assign(m_asset.heights.size(), 0);
    const float spacing = m_asset.size / static_cast<float>(res - 1);
    const int centerX = static_cast<int>(std::round(localX / spacing));
    const int centerZ = static_cast<int>(std::round(localZ / spacing));
    const int cells = std::max(1, static_cast<int>(std::ceil(m_brushRadius / spacing)));
    std::vector<float> original;
    if (m_tool == Tool::Smooth) original = m_asset.heights;
    const float rate = m_brushStrength * std::max(dt, 1.0f / 120.0f);
    if (m_tool == Tool::Paint && m_paintLayer == 1 && !m_asset.grassEnabled) {
        m_asset.grassEnabled = true;
        m_previewGrassDirty = true;
    }
    for (int z = std::max(0, centerZ - cells); z <= std::min(res - 1, centerZ + cells); ++z) {
        for (int x = std::max(0, centerX - cells); x <= std::min(res - 1, centerX + cells); ++x) {
            const float dx = (x - centerX) * spacing;
            const float dz = (z - centerZ) * spacing;
            const float distance = std::sqrt(dx * dx + dz * dz);
            if (distance > m_brushRadius) continue;
            const float falloff = 1.0f - distance / std::max(m_brushRadius, 0.001f);
            const std::size_t index = static_cast<std::size_t>(z) * res + x;
            if (m_tool == Tool::Paint) {
                m_asset.paint[index] = static_cast<std::uint8_t>(m_paintLayer);
            } else if (m_tool == Tool::ErasePaint) {
                m_asset.paint[index] = 0;
            } else if (m_tool == Tool::Raise) {
                m_asset.heights[index] = std::min(m_asset.maxHeight,
                    m_asset.heights[index] + rate * falloff);
            } else if (m_tool == Tool::Lower) {
                m_asset.heights[index] = std::max(0.0f,
                    m_asset.heights[index] - rate * falloff);
            } else if (m_tool == Tool::Flatten) {
                m_asset.heights[index] += (m_flattenHeight - m_asset.heights[index])
                    * std::clamp(rate * falloff, 0.0f, 1.0f);
            } else if (m_tool == Tool::Smooth) {
                float sum = 0.0f;
                int count = 0;
                for (int oz = -1; oz <= 1; ++oz)
                    for (int ox = -1; ox <= 1; ++ox) {
                        const int sx = std::clamp(x + ox, 0, res - 1);
                        const int sz = std::clamp(z + oz, 0, res - 1);
                        sum += original[static_cast<std::size_t>(sz) * res + sx];
                        ++count;
                    }
                const float average = sum / static_cast<float>(count);
                m_asset.heights[index] += (average - m_asset.heights[index])
                    * std::clamp(rate * falloff, 0.0f, 1.0f);
            }
        }
    }
    RefreshPreviewBrushRegion(localX, localZ, m_brushRadius,
        m_tool == Tool::Paint || m_tool == Tool::ErasePaint);
    m_previewGrassDirty = true;
    m_dirty = true;
}

void TerrainCreatorPanel::RefreshPreviewBrushRegion(float localX, float localZ,
                                                     float radius, bool paintChanged) {
    engine::Heightmap& preview = m_previewTerrain.MutableMap();
    const int res = preview.res;
    if (preview.Empty() || res < 2 || m_asset.size <= 0.0f
        || preview.h.size() != static_cast<std::size_t>(res) * res) {
        m_previewTerrainDirty = true;
        return;
    }

    const float spacing = m_asset.size / static_cast<float>(res - 1);
    const int centerX = static_cast<int>(std::round(localX / spacing));
    const int centerZ = static_cast<int>(std::round(localZ / spacing));
    // Include one extra preview vertex so bilinear samples along the brush edge
    // cannot remain stale after editing the full-resolution height field.
    const int cells = std::max(1, static_cast<int>(std::ceil(radius / spacing)) + 1);
    const int minX = std::max(0, centerX - cells);
    const int minZ = std::max(0, centerZ - cells);
    const int maxX = std::min(res - 1, centerX + cells);
    const int maxZ = std::min(res - 1, centerZ + cells);

    if (paintChanged) {
        std::vector<std::uint8_t> paint = m_previewTerrain.Paint();
        if (paint.size() != preview.h.size()) paint.assign(preview.h.size(), 0);
        for (int z = minZ; z <= maxZ; ++z) {
            const float v = static_cast<float>(z) / static_cast<float>(res - 1);
            for (int x = minX; x <= maxX; ++x) {
                const float u = static_cast<float>(x) / static_cast<float>(res - 1);
                paint[static_cast<std::size_t>(z) * res + x] = SamplePaint(m_asset, u, v);
            }
        }
        m_previewTerrain.SetPaint(paint);
        return;
    }

    for (int z = minZ; z <= maxZ; ++z) {
        const float v = static_cast<float>(z) / static_cast<float>(res - 1);
        for (int x = minX; x <= maxX; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(res - 1);
            preview.h[static_cast<std::size_t>(z) * res + x] = SampleHeight(m_asset, u, v);
        }
    }
    m_previewTerrain.CommitHeightRegion(minX, minZ, maxX, maxZ);
}

void TerrainCreatorPanel::DrawLegacyViewport(float dt) {
    ImGui::TextUnformatted("LANDSCAPE SCENE");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset View")) {
        m_viewYaw = -0.75f;
        m_viewPitch = 0.55f;
        m_viewZoom = 1.0f;
        m_viewPan = glm::vec2(0.0f);
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.x = std::max(size.x, 320.0f);
    size.y = std::max(size.y, 320.0f);
    ImGui::InvisibleButton("##TerrainCreatorViewport", size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight
        | ImGuiButtonFlags_MouseButtonMiddle);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    draw->PushClipRect(origin, end, true);

    const float solarAngle = (m_atmosphereTimeOfDay - 0.25f) * glm::two_pi<float>();
    const float sunHeight = std::sin(solarAngle);
    const float daylight = m_atmosphereEnabled
        ? glm::smoothstep(-0.18f, 0.22f, sunHeight) : 0.72f;
    const float sunrise = m_atmosphereEnabled
        ? std::exp(-std::abs(sunHeight) * 7.0f) : 0.0f;
    const glm::vec3 nightTop(0.015f, 0.025f, 0.075f);
    const glm::vec3 nightHorizon(0.055f, 0.075f, 0.13f);
    const glm::vec3 dayTop = m_atmosphereSkyTint * glm::vec3(0.55f, 0.70f, 0.96f);
    const glm::vec3 dayHorizon = glm::mix(glm::vec3(0.68f, 0.78f, 0.92f),
                                          glm::vec3(1.0f, 0.48f, 0.19f), sunrise * 0.65f);
    const glm::vec3 skyTop = glm::mix(nightTop, dayTop, daylight);
    const glm::vec3 skyHorizon = glm::mix(nightHorizon, dayHorizon, daylight);
    auto packedColor = [](const glm::vec3& color, int alpha = 255) {
        const glm::vec3 c = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
        return IM_COL32(static_cast<int>(c.r * 255.0f), static_cast<int>(c.g * 255.0f),
                        static_cast<int>(c.b * 255.0f), alpha);
    };
    draw->AddRectFilledMultiColor(origin, end, packedColor(skyTop), packedColor(skyTop),
                                   packedColor(skyHorizon), packedColor(skyHorizon));
    if (m_atmosphereEnabled && daylight > 0.02f) {
        const float sunX = origin.x + size.x * (0.5f + 0.38f * std::cos(solarAngle));
        const float sunY = origin.y + size.y * (0.50f - 0.40f * sunHeight);
        const ImVec2 sun(sunX, sunY);
        const ImU32 glow = IM_COL32(255, 188, 86, static_cast<int>(70.0f * daylight));
        draw->AddCircleFilled(sun, 28.0f, glow, 32);
        draw->AddCircleFilled(sun, 12.0f, IM_COL32(255, 232, 168, 220), 32);
    }
    if (m_atmosphereEnabled && m_atmosphereHaze > 0.001f) {
        const int hazeAlpha = static_cast<int>(105.0f * m_atmosphereHaze);
        const ImVec2 hazeTop(origin.x, origin.y + size.y * 0.43f);
        const ImVec2 hazeBottom(end.x, origin.y + size.y * 0.75f);
        draw->AddRectFilledMultiColor(hazeTop, hazeBottom,
            IM_COL32(220, 226, 225, 0), IM_COL32(220, 226, 225, 0),
            IM_COL32(220, 226, 225, hazeAlpha), IM_COL32(220, 226, 225, hazeAlpha));
    }
    draw->AddRect(origin, end, IM_COL32(62, 76, 92, 255));

    const int res = m_asset.resolution;
    const float worldSize = std::max(m_asset.size, 0.01f);
    const glm::vec3 target(m_viewPan.x, m_asset.maxHeight * 0.22f, m_viewPan.y);
    // Fit the complete square landscape (its diagonal, not just one edge) in
    // the vertical FOV. The previous 1.15x distance placed the camera inside
    // that footprint, making the terrain fill the image like a flat backdrop.
    const float distance = std::max(worldSize * 1.85f, 10.0f) * m_viewZoom;
    const glm::vec3 orbit(std::cos(m_viewPitch) * std::cos(m_viewYaw),
                          std::sin(m_viewPitch),
                          std::cos(m_viewPitch) * std::sin(m_viewYaw));
    const glm::vec3 camera = target + orbit * distance;
    const glm::vec3 forward = glm::normalize(target - camera);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));
    const glm::vec3 lightDirection = glm::normalize(glm::vec3(
        std::cos(solarAngle) * 0.55f, std::max(sunHeight, 0.08f),
        std::sin(solarAngle) * 0.55f));
    const glm::vec3 fogColor = glm::mix(skyHorizon, glm::vec3(0.72f),
                                        m_atmosphereHaze * 0.35f);
    constexpr float fov = 52.0f;
    const float tanHalfFov = std::tan(glm::radians(fov) * 0.5f);
    const float focal = size.y * 0.5f / tanHalfFov;
    auto project = [&](const glm::vec3& point, ImVec2* screen, float* depth = nullptr) {
        const glm::vec3 relative = point - camera;
        const float z = glm::dot(relative, forward);
        if (z <= 0.05f) return false;
        if (screen) {
            screen->x = origin.x + size.x * 0.5f + glm::dot(relative, right) / z * focal;
            screen->y = origin.y + size.y * 0.5f - glm::dot(relative, up) / z * focal;
        }
        if (depth) *depth = z;
        return true;
    };
    auto heightAt = [&](float x, float z) {
        if (res < 2 || m_asset.heights.size() != static_cast<std::size_t>(res) * res)
            return 0.0f;
        const float gx = std::clamp(x / worldSize, 0.0f, 1.0f) * (res - 1);
        const float gz = std::clamp(z / worldSize, 0.0f, 1.0f) * (res - 1);
        const int x0 = static_cast<int>(std::floor(gx));
        const int z0 = static_cast<int>(std::floor(gz));
        const int x1 = std::min(x0 + 1, res - 1), z1 = std::min(z0 + 1, res - 1);
        const float fx = gx - x0, fz = gz - z0;
        auto sample = [&](int sx, int sz) {
            return m_asset.heights[static_cast<std::size_t>(sz) * res + sx];
        };
        return glm::mix(glm::mix(sample(x0, z0), sample(x1, z0), fx),
                        glm::mix(sample(x0, z1), sample(x1, z1), fx), fz);
    };

    // Editor floor grid gives the isolated landscape viewport a scene context.
    const int gridLines = 20;
    for (int i = -5; i <= gridLines + 5; ++i) {
        const float coordinate = worldSize * static_cast<float>(i) / gridLines;
        ImVec2 a, b;
        if (project({coordinate, -0.02f, -worldSize * 0.25f}, &a)
            && project({coordinate, -0.02f, worldSize * 1.25f}, &b))
            draw->AddLine(a, b, IM_COL32(116, 126, 139, 75));
        if (project({-worldSize * 0.25f, -0.02f, coordinate}, &a)
            && project({worldSize * 1.25f, -0.02f, coordinate}, &b))
            draw->AddLine(a, b, IM_COL32(116, 126, 139, 75));
    }

    if (res >= 2 && m_asset.heights.size() == static_cast<std::size_t>(res) * res) {
        struct PreviewTriangle { ImVec2 a, b, c; float depth; ImU32 color; };
        std::vector<PreviewTriangle> triangles;
        const int preview = std::min(res - 1, 72);
        triangles.reserve(static_cast<std::size_t>(preview) * preview * 2);
        for (int z = 0; z < preview; ++z) {
            const float z0 = worldSize * z / preview;
            const float z1 = worldSize * (z + 1) / preview;
            for (int x = 0; x < preview; ++x) {
                const float x0 = worldSize * x / preview;
                const float x1 = worldSize * (x + 1) / preview;
                const glm::vec3 p[4] = {{x0, heightAt(x0, z0), z0},
                                        {x1, heightAt(x1, z0), z0},
                                        {x0, heightAt(x0, z1), z1},
                                        {x1, heightAt(x1, z1), z1}};
                ImVec2 s[4]; float d[4];
                if (!project(p[0], &s[0], &d[0]) || !project(p[1], &s[1], &d[1])
                    || !project(p[2], &s[2], &d[2]) || !project(p[3], &s[3], &d[3]))
                    continue;
                const int sourceX = x * (res - 1) / preview;
                const int sourceZ = z * (res - 1) / preview;
                const std::size_t source = static_cast<std::size_t>(sourceZ) * res + sourceX;
                const std::uint8_t layer = m_showPaint
                    && m_asset.paint.size() == m_asset.heights.size() ? m_asset.paint[source] : 0;
                const float heightFraction = m_asset.maxHeight > 0.0f
                    ? m_asset.heights[source] / m_asset.maxHeight : 0.0f;
                const glm::vec3 normal = glm::normalize(glm::cross(p[2] - p[0], p[1] - p[0]));
                const float direct = std::max(glm::dot(normal, lightDirection), 0.0f);
                const float atmosphereLight = m_atmosphereEnabled
                    ? (0.22f + daylight * 0.30f
                       + direct * daylight * m_atmosphereSunIntensity * 0.68f)
                    : 1.0f;
                const float averageDepth = (d[0] + d[1] + d[2] + d[3]) * 0.25f;
                const float fog = m_atmosphereEnabled
                    ? 1.0f - std::exp(-m_atmosphereFogDensity
                                      * averageDepth / std::max(worldSize, 0.01f) * 10.0f)
                    : 0.0f;
                const ImU32 color = LayerColor(layer, heightFraction, m_previewLayerColors,
                                                atmosphereLight, fog, fogColor);
                triangles.push_back({s[0], s[2], s[1], (d[0] + d[2] + d[1]) / 3.0f, color});
                triangles.push_back({s[1], s[2], s[3], (d[1] + d[2] + d[3]) / 3.0f, color});
            }
        }
        std::sort(triangles.begin(), triangles.end(),
            [](const PreviewTriangle& a, const PreviewTriangle& b) { return a.depth > b.depth; });
        for (const PreviewTriangle& triangle : triangles) {
            draw->AddTriangleFilled(triangle.a, triangle.b, triangle.c, triangle.color);
            draw->AddTriangle(triangle.a, triangle.b, triangle.c, IM_COL32(15, 20, 24, 32));
        }
    }

    bool brushHit = false;
    glm::vec3 brushPoint(0.0f);
    if (ImGui::IsItemHovered()) {
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            m_viewYaw -= io.MouseDelta.x * 0.008f;
            m_viewPitch = std::clamp(m_viewPitch + io.MouseDelta.y * 0.008f,
                                     0.08f, 1.45f);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            const float panScale = worldSize * m_viewZoom / std::max(size.y, 1.0f);
            m_viewPan.x -= io.MouseDelta.x * panScale;
            m_viewPan.y -= io.MouseDelta.y * panScale;
        }
        if (io.MouseWheel != 0.0f)
            m_viewZoom = std::clamp(m_viewZoom * std::pow(0.88f, io.MouseWheel),
                                    0.18f, 4.0f);

        const ImVec2 mouse = ImGui::GetMousePos();
        const float ndcX = (mouse.x - (origin.x + size.x * 0.5f)) / (size.x * 0.5f);
        const float ndcY = -(mouse.y - (origin.y + size.y * 0.5f)) / (size.y * 0.5f);
        const glm::vec3 ray = glm::normalize(forward + right * ndcX
            * (size.x / size.y) * tanHalfFov + up * ndcY * tanHalfFov);
        const float step = std::max(worldSize / 300.0f, 0.04f);
        float previousT = 0.0f, previousDelta = 0.0f;
        bool hadPrevious = false;
        for (float t = 0.0f; t <= distance * 3.0f; t += step) {
            const glm::vec3 point = camera + ray * t;
            if (point.x < 0.0f || point.z < 0.0f
                || point.x > worldSize || point.z > worldSize) {
                hadPrevious = false;
                continue;
            }
            const float delta = point.y - heightAt(point.x, point.z);
            if (hadPrevious && previousDelta > 0.0f && delta <= 0.0f) {
                float low = previousT, high = t;
                for (int refine = 0; refine < 8; ++refine) {
                    const float middle = (low + high) * 0.5f;
                    const glm::vec3 candidate = camera + ray * middle;
                    if (candidate.y > heightAt(candidate.x, candidate.z)) low = middle;
                    else high = middle;
                }
                brushPoint = camera + ray * ((low + high) * 0.5f);
                brushPoint.y = heightAt(brushPoint.x, brushPoint.z);
                brushHit = true;
                break;
            }
            previousT = t;
            previousDelta = delta;
            hadPrevious = true;
        }
        if (brushHit && ImGui::IsMouseDown(ImGuiMouseButton_Left)
            && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
            ApplyBrush(brushPoint.x, brushPoint.z, dt);
    }

    if (brushHit) {
        constexpr int segments = 64;
        for (int i = 0; i < segments; ++i) {
            const float a0 = glm::two_pi<float>() * i / segments;
            const float a1 = glm::two_pi<float>() * (i + 1) / segments;
            glm::vec3 p0(brushPoint.x + std::cos(a0) * m_brushRadius, 0.0f,
                         brushPoint.z + std::sin(a0) * m_brushRadius);
            glm::vec3 p1(brushPoint.x + std::cos(a1) * m_brushRadius, 0.0f,
                         brushPoint.z + std::sin(a1) * m_brushRadius);
            if (p0.x < 0 || p0.z < 0 || p0.x > worldSize || p0.z > worldSize
                || p1.x < 0 || p1.z < 0 || p1.x > worldSize || p1.z > worldSize)
                continue;
            p0.y = heightAt(p0.x, p0.z) + 0.04f;
            p1.y = heightAt(p1.x, p1.z) + 0.04f;
            ImVec2 s0, s1;
            if (project(p0, &s0) && project(p1, &s1))
                draw->AddLine(s0, s1, IM_COL32(255, 196, 45, 255), 2.5f);
        }
    }
    draw->AddText(ImVec2(origin.x + 8.0f, origin.y + 8.0f),
                  IM_COL32(235, 239, 245, 255),
                  "LMB sculpt/paint | RMB orbit | MMB pan | Wheel zoom");
    draw->PopClipRect();
}

void TerrainCreatorPanel::RebuildPreviewTerrain(const std::string& contentRoot) {
    if (m_asset.resolution < 2
        || m_asset.heights.size() != static_cast<std::size_t>(m_asset.resolution) * m_asset.resolution)
        return;

    engine::Heightmap map;
    // The editor preview is deliberately decoupled from the authored heightmap.
    // Saving and applying still use m_asset at full resolution.
    map.res = std::min(m_asset.resolution, m_previewResolutionLimit);
    map.size = m_asset.size;
    map.origin = glm::vec3(0.0f);
    map.maxHeight = m_asset.maxHeight;
    map.h.resize(static_cast<std::size_t>(map.res) * map.res);
    std::vector<std::uint8_t> previewPaint(map.h.size(), 0);
    for (int z = 0; z < map.res; ++z) {
        const float v = static_cast<float>(z) / static_cast<float>(map.res - 1);
        for (int x = 0; x < map.res; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(map.res - 1);
            const std::size_t index = static_cast<std::size_t>(z) * map.res + x;
            map.h[index] = SampleHeight(m_asset, u, v);
            previewPaint[index] = SamplePaint(m_asset, u, v);
        }
    }
    m_previewTerrain.SetHeightmap(map);
    m_previewTerrain.SetPaint(previewPaint);

    engine::TerrainLayerSurface surfaces[6];
    engine::TerrainLayerTexture textures[6];
    engine::DefaultTerrainLayerSurfaces(surfaces);
    for (int layer = 1; layer <= 5; ++layer) {
        const std::string& path = m_asset.layerMaterials[layer];
        if (path.empty()) continue;
        const std::filesystem::path materialFile = std::filesystem::path(path).is_absolute()
            ? std::filesystem::path(path) : std::filesystem::path(contentRoot) / path;
        engine::RuntimeMaterialAsset material;
        std::string error;
        if (!engine::LoadMaterialAssetFile(materialFile.string(), &material, &error)) continue;
        surfaces[layer].albedo = glm::clamp(material.material.albedo,
                                             glm::vec3(0.0f), glm::vec3(1.0f));
        surfaces[layer].ao = glm::clamp(material.material.ao, 0.0f, 1.0f);
        surfaces[layer].roughness = glm::clamp(material.material.roughness, 0.04f, 1.0f);
        surfaces[layer].metallic = glm::clamp(material.material.metallic, 0.0f, 1.0f);
        textures[layer].tiling = glm::max(material.material.uvScale * 8.0f,
                                           glm::vec2(0.001f));
        auto loadPixels = [&](const std::string& texturePath,
                              std::vector<std::uint8_t>& pixels, int& width, int& height) {
            if (texturePath.empty()) return;
            const std::filesystem::path file = std::filesystem::path(texturePath).is_absolute()
                ? std::filesystem::path(texturePath)
                : std::filesystem::path(contentRoot) / texturePath;
            std::string extension = file.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (extension == ".3dgtex") {
                engine::TextureAssetData texture;
                std::string ignored;
                if (engine::LoadTextureAsset(file.string(), &texture, &ignored)) {
                    width = static_cast<int>(texture.width);
                    height = static_cast<int>(texture.height);
                    pixels = std::move(texture.rgba);
                }
                return;
            }
            try {
                engine::image::Image image;
                if (extension == ".png") image = engine::image::DecodePNG(file.string());
                else if (extension == ".jpg" || extension == ".jpeg")
                    image = engine::image::DecodeJPEG(file.string());
                width = image.width;
                height = image.height;
                pixels = std::move(image.rgba);
            } catch (...) {}
        };
        loadPixels(material.albedoMapPath, textures[layer].albedoRgba,
                   textures[layer].albedoWidth, textures[layer].albedoHeight);
        loadPixels(material.metalRoughMapPath, textures[layer].ormRgba,
                   textures[layer].ormWidth, textures[layer].ormHeight);
    }
    m_previewTerrain.SetLayerSurfaces(surfaces);
    m_previewTerrain.SetLayerTextures(textures);
    m_previewTerrainDirty = false;
    m_previewGrassDirty = true;
    m_previewContentRoot = contentRoot;
}

unsigned int TerrainCreatorPanel::RenderEngineViewport(const std::string& contentRoot,
                                                        int width, int height) {
    width = std::max(width, 64);
    height = std::max(height, 64);
    if (!m_previewFramebuffer) m_previewFramebuffer.emplace(width, height, GL_RGBA8, true);
    else if (m_previewFramebuffer->Width() != width || m_previewFramebuffer->Height() != height)
        m_previewFramebuffer->Resize(width, height);
    if (!m_previewSky) m_previewSky = std::make_unique<engine::ProceduralSky>();
    if (m_previewTerrainDirty || m_previewContentRoot != contentRoot)
        RebuildPreviewTerrain(contentRoot);

    GLint oldFbo = 0, oldViewport[4]{};
    GLfloat oldClear[4]{};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
    glGetIntegerv(GL_VIEWPORT, oldViewport);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, oldClear);
    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    const GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);

    m_previewFramebuffer->Bind();
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.03f, 0.04f, 0.065f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float worldSize = std::max(m_asset.size, 0.01f);
    const glm::vec3 target(m_viewPan.x,
                           m_asset.maxHeight * 0.22f,
                           m_viewPan.y);
    const float distance = std::max(worldSize * 1.85f, 10.0f) * m_viewZoom;
    const glm::vec3 orbit(std::cos(m_viewPitch) * std::cos(m_viewYaw),
                          std::sin(m_viewPitch),
                          std::cos(m_viewPitch) * std::sin(m_viewYaw));
    m_previewCameraPosition = target + orbit * distance;
    engine::Camera camera(m_previewCameraPosition);
    camera.fov = 52.0f;
    camera.nearPlane = std::max(0.02f, worldSize * 0.0005f);
    camera.farPlane = std::max(500.0f, distance * 5.0f);
    camera.LookAt(target);
    const float aspect = static_cast<float>(width) / height;
    m_previewViewProjection = camera.ProjectionMatrix(aspect) * camera.ViewMatrix();

    engine::DayNightCycle::Sample sky = engine::DayNightCycle::At(m_atmosphereTimeOfDay);
    if (!m_atmosphereEnabled) sky = engine::DayNightCycle::At(0.5f);
    sky.zenith *= m_atmosphereSkyTint;
    sky.horizon *= glm::mix(glm::vec3(1.0f), m_atmosphereSkyTint, 0.45f);
    m_previewSky->Draw(camera.ViewMatrix(), camera.ProjectionMatrix(aspect), sky, true);
    // The sky is colour-only in this isolated editor scene. Clear its far-plane
    // depth before drawing the landscape so driver depth precision cannot hide
    // a large, nearly horizontal terrain surface.
    glClear(GL_DEPTH_BUFFER_BIT);

    // Draw the engine Terrain mesh explicitly. The general scene renderer is
    // optimized around bounded closed meshes; routing this isolated, open
    // landscape through that batching path made the surface disappear on some
    // drivers even though its CPU heightfield remained pickable.
    if (!m_previewTerrainShader) {
        static const char* vertex = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
uniform mat4 uViewProjection;
uniform mat4 uModel;
out vec3 vWorldPosition;
out vec3 vNormal;
out vec2 vUV;
void main() {
    vWorldPosition = vec3(uModel * vec4(aPosition, 1.0));
    vNormal = aNormal;
    vUV = aUV;
    gl_Position = uViewProjection * vec4(vWorldPosition, 1.0);
}
)GLSL";
        static const char* fragment = R"GLSL(
#version 330 core
in vec3 vWorldPosition;
in vec3 vNormal;
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uAlbedo;
uniform sampler2D uOrm;
uniform vec3 uCameraPosition;
uniform vec3 uLightDirection;
uniform vec3 uLightColor;
uniform vec3 uAmbient;
uniform int uFogEnabled;
uniform vec3 uFogColor;
uniform float uFogDensity;
void main() {
    vec3 base = pow(texture(uAlbedo, vUV).rgb, vec3(2.2));
    vec3 orm = texture(uOrm, vUV).rgb;
    vec3 N = normalize(vNormal);
    vec3 L = normalize(-uLightDirection);
    vec3 V = normalize(uCameraPosition - vWorldPosition);
    vec3 H = normalize(L + V);
    float diffuse = max(dot(N, L), 0.0);
    float roughness = clamp(orm.g, 0.04, 1.0);
    float specular = pow(max(dot(N, H), 0.0), mix(96.0, 4.0, roughness));
    vec3 color = base * (uAmbient * orm.r + uLightColor * diffuse)
               + uLightColor * specular * mix(0.35, 0.04, roughness);
    if (uFogEnabled == 1) {
        float distanceToCamera = length(uCameraPosition - vWorldPosition);
        float fog = 1.0 - exp(-distanceToCamera * uFogDensity);
        color = mix(color, uFogColor, clamp(fog, 0.0, 0.88));
    }
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
)GLSL";
        m_previewTerrainShader = std::make_unique<engine::Shader>(vertex, fragment);
    }
    if (m_previewTerrain.Ready()) {
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        m_previewTerrainShader->Bind();
        m_previewTerrainShader->SetMat4("uViewProjection", m_previewViewProjection);
        m_previewTerrainShader->SetMat4("uModel", glm::translate(glm::mat4(1.0f),
            glm::vec3(-worldSize * 0.5f, 0.0f, -worldSize * 0.5f)));
        m_previewTerrainShader->SetVec3("uCameraPosition", camera.Position());
        m_previewTerrainShader->SetVec3("uLightDirection", sky.keyLightDirection);
        m_previewTerrainShader->SetVec3("uLightColor",
            sky.keyLightColor * (m_atmosphereEnabled ? m_atmosphereSunIntensity : 1.0f));
        m_previewTerrainShader->SetVec3("uAmbient",
            sky.ambient * (2.5f + m_atmosphereHaze));
        m_previewTerrainShader->SetInt("uFogEnabled",
            m_atmosphereEnabled && m_atmosphereFogDensity > 0.0001f ? 1 : 0);
        m_previewTerrainShader->SetVec3("uFogColor", sky.horizon);
        m_previewTerrainShader->SetFloat("uFogDensity",
            m_atmosphereFogDensity * 10.0f / worldSize);
        m_previewTerrain.Albedo().Bind(0);
        m_previewTerrainShader->SetInt("uAlbedo", 0);
        m_previewTerrain.SurfaceMap().Bind(1);
        m_previewTerrainShader->SetInt("uOrm", 1);
        m_previewTerrain.GetMesh().Draw();
    }
    if (m_asset.grassEnabled && m_previewTerrain.Ready()) {
        if (!m_previewGrass) {
            m_previewGrass = std::make_unique<engine::GrassField>();
            m_previewGrassDirty = true;
        }
        engine::GrassConfig grass;
        grass.density = m_asset.grassDensity;
        grass.bladeHeight = m_asset.grassHeight;
        grass.randomizeHeight = m_asset.grassRandomizeHeight;
        grass.minHeightScale = m_asset.grassMinHeightScale;
        grass.maxHeightScale = m_asset.grassMaxHeightScale;
        grass.windStrength = m_asset.grassWindStrength;
        grass.windSpeed = m_asset.grassWindSpeed;
        grass.baseColor = m_asset.grassBaseColor;
        grass.tipColor = m_asset.grassTipColor;
        grass.grassLayer = 1;
        grass.maxBlades = 100000;
        if (m_previewGrassDirty) {
            const std::vector<std::uint8_t> noStyles;
            const std::vector<engine::GrassStyle> noPalette;
            m_previewGrass->Build(m_previewTerrain.Map(), m_previewTerrain.Paint(),
                                  noStyles, noPalette,
                                  glm::vec3(-worldSize * 0.5f, 0.0f,
                                            -worldSize * 0.5f), grass);
            m_previewGrassDirty = false;
        } else {
            m_previewGrass->SetConfig(grass);
        }
        m_previewGrass->Update(1.0f / 60.0f);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        m_previewGrass->Draw(camera, aspect, sky.keyLightDirection,
            sky.keyLightColor * (m_atmosphereEnabled ? m_atmosphereSunIntensity : 1.0f),
            sky.ambient * 2.5f);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(oldFbo));
    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
    glClearColor(oldClear[0], oldClear[1], oldClear[2], oldClear[3]);
    if (depthEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (cullEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (blendEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (scissorEnabled) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    return m_previewFramebuffer->ColorTexture();
}

void TerrainCreatorPanel::DrawViewport(const std::string& contentRoot, float dt) {
    ImGui::TextUnformatted("ENGINE LANDSCAPE SCENE");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset View")) {
        m_viewYaw = -0.75f; m_viewPitch = 0.55f; m_viewZoom = 1.0f;
        m_viewPan = glm::vec2(0.0f);
    }
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.x = std::max(size.x, 320.0f);
    size.y = std::max(size.y, 320.0f);
    const unsigned int texture = RenderEngineViewport(contentRoot,
        static_cast<int>(size.x), static_cast<int>(size.y));
    ImGui::Image((ImTextureID)(std::intptr_t)texture, size,
                 ImVec2(0, 1), ImVec2(1, 0));
    const ImVec2 imageMin = ImGui::GetItemRectMin();
    const bool hovered = ImGui::IsItemHovered();
    const float worldSize = std::max(m_asset.size, 0.01f);
    const glm::vec3 target(m_viewPan.x, m_asset.maxHeight * 0.22f, m_viewPan.y);
    const glm::vec3 forward = glm::normalize(target - m_previewCameraPosition);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));
    bool brushHit = false;
    glm::vec3 brushPoint(0.0f);
    if (hovered) {
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            m_viewYaw -= io.MouseDelta.x * 0.008f;
            m_viewPitch = std::clamp(m_viewPitch + io.MouseDelta.y * 0.008f, 0.08f, 1.45f);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            const float scale = worldSize * m_viewZoom / std::max(size.y, 1.0f);
            m_viewPan.x -= io.MouseDelta.x * scale;
            m_viewPan.y -= io.MouseDelta.y * scale;
        }
        if (io.MouseWheel != 0.0f)
            m_viewZoom = std::clamp(m_viewZoom * std::pow(0.88f, io.MouseWheel), 0.18f, 4.0f);
        const ImVec2 mouse = ImGui::GetMousePos();
        const float nx = (mouse.x - imageMin.x) / size.x * 2.0f - 1.0f;
        const float ny = 1.0f - (mouse.y - imageMin.y) / size.y * 2.0f;
        const float tanHalf = std::tan(glm::radians(52.0f) * 0.5f);
        const glm::vec3 ray = glm::normalize(forward + right * nx * (size.x / size.y) * tanHalf
                                             + up * ny * tanHalf);
        const float distance = std::max(worldSize * 1.85f, 10.0f) * m_viewZoom;
        const float step = std::max(worldSize / 400.0f, 0.025f);
        float previousT = 0.0f, previousDelta = 0.0f;
        bool previousInside = false;
        for (float t = 0.0f; t <= distance * 3.0f; t += step) {
            const glm::vec3 point = m_previewCameraPosition + ray * t;
            const float halfSize = worldSize * 0.5f;
            const bool inside = point.x >= -halfSize && point.z >= -halfSize
                && point.x <= halfSize && point.z <= halfSize;
            if (!inside) { previousInside = false; continue; }
            const float localX = point.x + halfSize;
            const float localZ = point.z + halfSize;
            const float surface = m_previewTerrain.Map().Empty()
                ? 0.0f : m_previewTerrain.HeightAt(localX, localZ);
            const float delta = point.y - surface;
            if (previousInside && previousDelta > 0.0f && delta <= 0.0f) {
                float low = previousT, high = t;
                for (int i = 0; i < 9; ++i) {
                    const float mid = (low + high) * 0.5f;
                    const glm::vec3 p = m_previewCameraPosition + ray * mid;
                    if (p.y > m_previewTerrain.HeightAt(p.x + halfSize, p.z + halfSize))
                        low = mid;
                    else high = mid;
                }
                brushPoint = m_previewCameraPosition + ray * ((low + high) * 0.5f);
                brushPoint.y = m_previewTerrain.HeightAt(
                    brushPoint.x + halfSize, brushPoint.z + halfSize);
                brushHit = true;
                break;
            }
            previousInside = true; previousT = t; previousDelta = delta;
        }
        if (brushHit && ImGui::IsMouseDown(ImGuiMouseButton_Left)
            && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
            ApplyBrush(brushPoint.x + worldSize * 0.5f,
                       brushPoint.z + worldSize * 0.5f, dt);
    }
    if (brushHit) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        for (int i = 0; i < 64; ++i) {
            const float a0 = glm::two_pi<float>() * i / 64.0f;
            const float a1 = glm::two_pi<float>() * (i + 1) / 64.0f;
            glm::vec3 p0(brushPoint.x + std::cos(a0) * m_brushRadius, 0.0f,
                         brushPoint.z + std::sin(a0) * m_brushRadius);
            glm::vec3 p1(brushPoint.x + std::cos(a1) * m_brushRadius, 0.0f,
                         brushPoint.z + std::sin(a1) * m_brushRadius);
            const float halfSize = worldSize * 0.5f;
            if (p0.x < -halfSize || p0.z < -halfSize || p0.x > halfSize || p0.z > halfSize
                || p1.x < -halfSize || p1.z < -halfSize || p1.x > halfSize || p1.z > halfSize)
                continue;
            p0.y = m_previewTerrain.HeightAt(p0.x + halfSize, p0.z + halfSize) + 0.04f;
            p1.y = m_previewTerrain.HeightAt(p1.x + halfSize, p1.z + halfSize) + 0.04f;
            const glm::vec4 c0 = m_previewViewProjection * glm::vec4(p0, 1.0f);
            const glm::vec4 c1 = m_previewViewProjection * glm::vec4(p1, 1.0f);
            if (c0.w <= 0.0f || c1.w <= 0.0f) continue;
            const glm::vec3 n0 = glm::vec3(c0) / c0.w, n1 = glm::vec3(c1) / c1.w;
            const ImVec2 s0(imageMin.x + (n0.x * 0.5f + 0.5f) * size.x,
                            imageMin.y + (1.0f - (n0.y * 0.5f + 0.5f)) * size.y);
            const ImVec2 s1(imageMin.x + (n1.x * 0.5f + 0.5f) * size.x,
                            imageMin.y + (1.0f - (n1.y * 0.5f + 0.5f)) * size.y);
            draw->AddLine(s0, s1, IM_COL32(255, 196, 45, 255), 2.5f);
        }
    }
    ImGui::SetItemTooltip("LMB sculpt/paint | RMB orbit | MMB pan | Wheel zoom");
}

void TerrainCreatorPanel::Draw(EditorScene& scene, const std::string& contentRoot,
                               const EditorAssets& assets, bool* open,
                               bool* assetSaved, std::string* message, float dt) {
    if (assetSaved) *assetSaved = false;
    if (message) message->clear();
    if (m_asset.heights.empty()) NewLandscape();
    if (!m_pendingOpen.empty()) {
        std::string error;
        if (!Load(m_pendingOpen, &error) && message) *message = "Terrain Creator: " + error;
        m_pendingOpen.clear();
    }
    RefreshMaterialPreview(contentRoot);
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::TerrainCreator), open)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("New")) NewLandscape();
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        std::string error;
        if (Save(contentRoot, &error)) {
            if (assetSaved) *assetSaved = true;
            if (message) *message = "Saved terrain asset: " + m_path;
        } else if (message) *message = "Terrain save failed: " + error;
    }
    ImGui::SameLine();
    if (m_path.empty()) ImGui::BeginDisabled();
    if (ImGui::Button("Add to Level")) m_addToLevel = true;
    if (m_path.empty()) ImGui::EndDisabled();
    ImGui::SameLine();
    const EditorScene::Object* target = scene.FindObject(m_applyTarget);
    const bool validTarget = target && target->isTerrain && !target->locked;
    ImGui::BeginDisabled(!validTarget);
    const std::string applyLabel = validTarget
        ? "Apply to \"" + target->name + "\"" : "Apply (No Landscape Target)";
    if (ImGui::Button(applyLabel.c_str())) m_applyToSelected = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s%s", m_path.empty() ? "Unsaved" : m_path.c_str(),
                        m_dirty ? " *" : "");
    ImGui::Separator();
    ImGui::SeparatorText("Level Apply Target");
    if (target) ImGui::Text("Target: %s", target->name.c_str());
    else if (m_applyTarget != engine::ecs::kNull) {
        ImGui::TextColored(ImVec4(1, .45f, .3f, 1), "Target landscape no longer exists.");
        m_applyTarget = engine::ecs::kNull;
    } else ImGui::TextDisabled("Target: None");
    if (const EditorScene::Object* selected = scene.SelectedObject()) {
        if (selected->entity != m_applyTarget) {
            ImGui::BeginDisabled(!selected->isTerrain || selected->locked);
            if (ImGui::Button(("Use \"" + selected->name + "\" As Target").c_str()))
                m_applyTarget = selected->entity;
            ImGui::EndDisabled();
        }
    }
    if (target && scene.SelectedObject() && scene.SelectedObject()->entity != target->entity)
        ImGui::TextColored(ImVec4(1, .7f, .2f, 1), "Current selection differs from the stable target.");
    ImGui::Separator();

    const float detailsWidth = std::min(350.0f, ImGui::GetContentRegionAvail().x * 0.34f);
    if (ImGui::BeginChild("##TerrainCreatorDetails", ImVec2(detailsWidth, 0), true)) {
        const std::vector<std::string> materials =
            assets.ContentAssetPaths(EditorAssets::Type::Material);
        char name[128]{};
        std::snprintf(name, sizeof(name), "%s", m_name.c_str());
        if (ImGui::InputText("Name", name, sizeof(name))) { m_name = name; m_dirty = true; }
        ImGui::SliderInt("Resolution", &m_asset.resolution, 16, 1024);
        const char* previewDetail = m_previewResolutionLimit == 128 ? "Low (128)"
            : m_previewResolutionLimit == 512 ? "High (512)" : "Medium (256)";
        if (ImGui::BeginCombo("Preview Detail", previewDetail)) {
            const int limits[] = {128, 256, 512};
            const char* labels[] = {"Low (128)", "Medium (256)", "High (512)"};
            for (int i = 0; i < 3; ++i) {
                if (ImGui::Selectable(labels[i], m_previewResolutionLimit == limits[i])) {
                    m_previewResolutionLimit = limits[i];
                    m_previewTerrainDirty = true;
                    m_previewGrassDirty = true;
                }
            }
            ImGui::EndCombo();
        }
        if (m_asset.resolution > m_previewResolutionLimit)
            ImGui::TextDisabled("Preview only: %d x %d. Saved landscape remains %d x %d.",
                m_previewResolutionLimit, m_previewResolutionLimit,
                m_asset.resolution, m_asset.resolution);
        if (ImGui::DragFloat("World Size", &m_asset.size, 0.25f,
                             1.0f, 8192.0f, "%.1f m")) {
            m_asset.size = std::clamp(m_asset.size, 1.0f, 8192.0f);
            m_previewTerrainDirty = true;
            m_previewGrassDirty = true;
            m_dirty = true;
        }
        auto editMaximumHeight = [&](const char* label) {
            if (!ImGui::DragFloat(label, &m_asset.maxHeight, 0.1f,
                                  0.0f, 2048.0f, "%.1f m")) {
                return;
            }
            m_asset.maxHeight = std::clamp(m_asset.maxHeight, 0.0f, 2048.0f);
            for (float& height : m_asset.heights)
                height = std::clamp(height, 0.0f, m_asset.maxHeight);
            m_flattenHeight = std::clamp(m_flattenHeight, 0.0f,
                                         m_asset.maxHeight);
            m_previewTerrainDirty = true;
            m_previewGrassDirty = true;
            m_dirty = true;
        };
        editMaximumHeight("Maximum Height");
        int seed = static_cast<int>(m_asset.seed);
        if (ImGui::InputInt("Seed", &seed)) m_asset.seed = static_cast<unsigned>(std::max(seed, 0));
        ImGui::SliderInt("Octaves", &m_asset.octaves, 1, 12);
        ImGui::DragFloat("Frequency", &m_asset.frequency, 0.05f, 0.05f, 32.0f);
        if (ImGui::Button("Generate Landscape", ImVec2(-1, 0))) Generate();
        ImGui::SeparatorText("Brush");
        if (ImGui::BeginCombo("Tool", ToolName(m_tool))) {
            for (int i = 0; i <= static_cast<int>(Tool::ErasePaint); ++i) {
                const Tool tool = static_cast<Tool>(i);
                if (ImGui::Selectable(ToolName(tool), tool == m_tool)) m_tool = tool;
            }
            ImGui::EndCombo();
        }
        ImGui::DragFloat("Radius", &m_brushRadius, 0.1f, 0.1f, 256.0f, "%.2f m");
        ImGui::DragFloat("Strength", &m_brushStrength, 0.1f, 0.05f, 100.0f);
        if (m_tool == Tool::Raise) {
            editMaximumHeight("Raise Height Limit##RaiseBrush");
            ImGui::TextDisabled("Raise stops at this world-space terrain height.");
        }
        if (m_tool == Tool::Flatten)
            ImGui::SliderFloat("Target Height", &m_flattenHeight, 0.0f, m_asset.maxHeight);
        if (m_tool == Tool::Paint) {
            auto layerLabel = [&](int layer) {
                const std::string& material = m_asset.layerMaterials[layer];
                const std::string role = layer == 1 ? "Grass" : "Layer " + std::to_string(layer);
                return material.empty() ? role + " (no material)"
                    : role + " - " + std::filesystem::path(material).filename().string();
            };
            const std::string selectedLayer = layerLabel(m_paintLayer);
            if (ImGui::BeginCombo("Paint Material", selectedLayer.c_str())) {
                for (int layer = 1; layer <= 5; ++layer) {
                    const std::string label = layerLabel(layer);
                    if (ImGui::Selectable(label.c_str(), layer == m_paintLayer)) {
                        m_paintLayer = layer;
                        if (layer == 1 && !m_asset.grassEnabled) {
                            m_asset.grassEnabled = true;
                            m_previewGrassDirty = true;
                            m_dirty = true;
                        }
                    }
                }
                ImGui::EndCombo();
            }
            const std::string& current = m_asset.layerMaterials[m_paintLayer];
            const std::string currentName = current.empty() ? "None"
                : std::filesystem::path(current).filename().string();
            if (ImGui::BeginCombo("Material Asset", currentName.c_str())) {
                if (ImGui::Selectable("None", current.empty())) {
                    m_asset.layerMaterials[m_paintLayer].clear();
                    m_materialPreviewSignature.clear();
                    m_previewTerrainDirty = true;
                    m_dirty = true;
                }
                for (const std::string& material : materials) {
                    const std::string filename =
                        std::filesystem::path(material).filename().string();
                    if (ImGui::Selectable(filename.c_str(), material == current)) {
                        m_asset.layerMaterials[m_paintLayer] = material;
                        m_materialPreviewSignature.clear();
                        m_previewTerrainDirty = true;
                        m_dirty = true;
                    }
                }
                ImGui::EndCombo();
            }
            if (m_paintLayer == 1)
                ImGui::TextDisabled("Grass grows immediately on painted Grass layer areas.");
        }
        ImGui::Checkbox("Show Painted Layers", &m_showPaint);
        ImGui::SeparatorText("Preview Atmosphere");
        ImGui::Checkbox("Enabled##TerrainAtmosphere", &m_atmosphereEnabled);
        if (m_atmosphereEnabled) {
            ImGui::SliderFloat("Time of Day", &m_atmosphereTimeOfDay, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Sun Intensity", &m_atmosphereSunIntensity,
                               0.0f, 3.0f, "%.2f");
            ImGui::SliderFloat("Haze", &m_atmosphereHaze, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Fog Density", &m_atmosphereFogDensity,
                               0.0f, 0.20f, "%.3f");
            ImGui::ColorEdit3("Sky Tint", &m_atmosphereSkyTint.x);
            ImGui::TextDisabled("Preview only; level atmosphere remains in World Settings.");
        }
        ImGui::SeparatorText("Paint Materials");
        for (int layer = 1; layer <= 5; ++layer) {
            ImGui::PushID(layer);
            const std::string& current = m_asset.layerMaterials[layer];
            const std::string label = current.empty() ? "None"
                : std::filesystem::path(current).filename().string();
            if (ImGui::BeginCombo("##Material", label.c_str())) {
                if (ImGui::Selectable("None", current.empty())) {
                    m_asset.layerMaterials[layer].clear(); m_dirty = true;
                    m_materialPreviewSignature.clear(); m_previewTerrainDirty = true;
                }
                for (const std::string& material : materials) {
                    if (ImGui::Selectable(std::filesystem::path(material).filename().string().c_str(),
                                          material == current)) {
                        m_asset.layerMaterials[layer] = material; m_dirty = true;
                        m_materialPreviewSignature.clear(); m_previewTerrainDirty = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::Text("Layer %d", layer);
            ImGui::PopID();
        }
        ImGui::SeparatorText("Grass");
        if (ImGui::Checkbox("Enabled##TerrainGrass", &m_asset.grassEnabled)) {
            m_dirty = true; m_previewGrassDirty = true;
        }
        if (m_asset.grassEnabled) {
            if (ImGui::DragFloat("Density", &m_asset.grassDensity, 0.05f, 0.0f, 50.0f))
                { m_dirty = true; m_previewGrassDirty = true; }
            if (ImGui::DragFloat("Blade Height", &m_asset.grassHeight, 0.01f, 0.05f, 10.0f))
                { m_dirty = true; m_previewGrassDirty = true; }
            if (ImGui::Checkbox("Randomize Height", &m_asset.grassRandomizeHeight))
                { m_dirty = true; m_previewGrassDirty = true; }
            if (m_asset.grassRandomizeHeight) {
                if (ImGui::DragFloatRange2("Height Scale", &m_asset.grassMinHeightScale,
                        &m_asset.grassMaxHeightScale, 0.01f, 0.05f, 4.0f))
                    { m_dirty = true; m_previewGrassDirty = true; }
            }
            if (ImGui::DragFloat("Wind Strength", &m_asset.grassWindStrength, 0.01f, 0.0f, 4.0f))
                { m_dirty = true; m_previewGrassDirty = true; }
            if (ImGui::DragFloat("Wind Speed", &m_asset.grassWindSpeed, 0.01f, 0.0f, 10.0f))
                { m_dirty = true; m_previewGrassDirty = true; }
            if (ImGui::ColorEdit3("Grass Base", &m_asset.grassBaseColor.x))
                { m_dirty = true; m_previewGrassDirty = true; }
            if (ImGui::ColorEdit3("Grass Tip", &m_asset.grassTipColor.x))
                { m_dirty = true; m_previewGrassDirty = true; }
        }
        ImGui::TextWrapped("Materials affect only areas painted with their layer. "
                           "Untouched terrain keeps the automatic landscape surface.");
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("##TerrainCreatorViewportHost", ImVec2(0, 0), true))
        DrawViewport(contentRoot, dt);
    ImGui::EndChild();
    ImGui::End();
}

bool TerrainCreatorPanel::ConsumeAddToLevel(engine::TerrainAssetData* asset,
                                            std::string* sourcePath) {
    if (!m_addToLevel) return false;
    m_addToLevel = false;
    if (asset) *asset = m_asset;
    if (sourcePath) *sourcePath = m_path;
    return true;
}

bool TerrainCreatorPanel::ConsumeApplyToSelected(engine::TerrainAssetData* asset,
                                                  std::string* sourcePath) {
    if (!m_applyToSelected) return false;
    m_applyToSelected = false;
    if (asset) *asset = m_asset;
    if (sourcePath) *sourcePath = m_path;
    return true;
}
