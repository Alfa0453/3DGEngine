#pragma once

#include <engine/assets/TerrainAsset.h>
#include <engine/graphics/Framebuffer.h>
#include <engine/graphics/Terrain.h>
#include <engine/ecs/Entity.h>

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <string>

class EditorAssets;
class EditorScene;

namespace engine {
class ProceduralSky;
class Shader;
class GrassField;
}

class TerrainCreatorPanel {
public:
    enum class Tool { Raise, Lower, Smooth, Flatten, Paint, ErasePaint };

    ~TerrainCreatorPanel();

    void QueueOpen(const std::string& path);
    void Draw(EditorScene& scene, const std::string& contentRoot, const EditorAssets& assets,
              bool* open, bool* assetSaved, std::string* message, float dt);

    bool ConsumeAddToLevel(engine::TerrainAssetData* asset,
                           std::string* sourcePath);
    bool ConsumeApplyToSelected(engine::TerrainAssetData* asset,
                                std::string* sourcePath);
    engine::ecs::Entity ApplyTarget() const { return m_applyTarget; }
    bool IsDirty() const { return m_dirty; }
    const std::string& Path() const { return m_path; }
    bool SaveForShutdown(const std::string& root, std::string* error) { return Save(root, error); }

private:
    void NewLandscape();
    bool Load(const std::string& path, std::string* error);
    bool Save(const std::string& contentRoot, std::string* error);
    void Generate();
    void RefreshMaterialPreview(const std::string& contentRoot);
    void DrawViewport(const std::string& contentRoot, float dt);
    void DrawLegacyViewport(float dt);
    unsigned int RenderEngineViewport(const std::string& contentRoot,
                                      int width, int height);
    void RebuildPreviewTerrain(const std::string& contentRoot);
    void RefreshPreviewBrushRegion(float localX, float localZ, float radius,
                                   bool paintChanged);
    void ApplyBrush(float localX, float localZ, float dt);

    engine::TerrainAssetData m_asset;
    std::string m_path;
    std::string m_pendingOpen;
    std::string m_name = "Landscape";
    Tool m_tool = Tool::Raise;
    int m_paintLayer = 1;
    float m_brushRadius = 4.0f;
    float m_brushStrength = 3.0f;
    float m_flattenHeight = 0.0f;
    bool m_showPaint = true;
    bool m_dirty = false;
    bool m_addToLevel = false;
    bool m_applyToSelected = false;
    engine::ecs::Entity m_applyTarget = engine::ecs::kNull;
    std::string m_materialPreviewSignature = "<uninitialized>";
    glm::vec3 m_previewLayerColors[6]{};
    float m_viewYaw = -0.75f;
    float m_viewPitch = 0.55f;
    float m_viewZoom = 1.0f;
    glm::vec2 m_viewPan{0.0f};
    bool m_atmosphereEnabled = true;
    float m_atmosphereTimeOfDay = 0.38f;
    float m_atmosphereSunIntensity = 1.0f;
    float m_atmosphereHaze = 0.22f;
    float m_atmosphereFogDensity = 0.015f;
    glm::vec3 m_atmosphereSkyTint{0.62f, 0.78f, 1.0f};
    engine::Terrain m_previewTerrain;
    std::optional<engine::Framebuffer> m_previewFramebuffer;
    std::unique_ptr<engine::ProceduralSky> m_previewSky;
    std::unique_ptr<engine::Shader> m_previewTerrainShader;
    std::unique_ptr<engine::GrassField> m_previewGrass;
    glm::mat4 m_previewViewProjection{1.0f};
    glm::vec3 m_previewCameraPosition{0.0f};
    bool m_previewTerrainDirty = true;
    bool m_previewGrassDirty = true;
    int m_previewResolutionLimit = 256;
    std::string m_previewContentRoot;
};
