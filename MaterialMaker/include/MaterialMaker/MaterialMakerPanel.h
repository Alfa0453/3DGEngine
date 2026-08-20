#pragma once

#include "MaterialMaker/MaterialDocument.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace material_maker {

class MaterialPreview;   // forward-declared so this header stays engine-free

class MaterialMakerPanel {
public:
    explicit MaterialMakerPanel(std::string outputDirectory = "MaterialMaker/generated");
    ~MaterialMakerPanel();   // defined in the .cpp where MaterialPreview is complete

    bool Draw();
    bool Draw(bool* open);
    bool DrawContent();
    bool SaveCurrent();
    bool LoadFromFile(const std::string& path);
    void SetOutputDirectory(const std::string& outputDirectory);
    void SetAlbedoMap(const std::string& path);
    void SetNormalMap(const std::string& path);
    void SetMetalRoughMap(const std::string& path);
    void SetHeightMap(const std::string& path);
    bool SetShaderAsset(const std::string& path);
    void RefreshShaderLibrary();

    const MaterialDocument& CurrentMaterial() const { return m_material; }
    const std::string& LastSavedPath() const { return m_lastSavedPath; }
    const std::string& StatusMessage() const { return m_status; }
    bool IsDirty() const;

private:
    struct ShaderParameterMetadata {
        std::string defaultValue;
        std::string group = "General";
        std::string tooltip;
        bool useRange = false;
        float minValue = 0.0f;
        float maxValue = 1.0f;
        float step = 0.01f;
        bool visible = true;
    };

    void DrawPreview();          // live GL preview (falls back to the drawn one)
    void DrawApproxPreview();    // hand-drawn 2D approximation (fallback)
    void DrawSurfaceControls();
    void DrawTilingControls();
    void DrawAdvancedControls();
    void DrawTextureControls();
    void DrawShaderControls();
    void ScanShaderLibrary();
    bool LoadShaderDefinition(const std::string& path, bool preserveOverrides);
    void DrawOrmPacker();          // pack separate metal/rough/AO into one ORM texture
    void DrawExportControls();
    void DrawPresetControls();     // starter presets (incl. measured metals)
    void DrawValidation();         // physically-based sanity warnings
    void DrawLibraryControls();    // browse + load saved .3dgmat files
    void RefreshLibrary();         // rescan the output directory
    void DrawModelImport();        // import a material from a model file (Assimp)
    void ApplyPreset(int index);
    void Reset();
    void SyncBuffersFromMaterial();
    bool Save();

    MaterialDocument m_material;
    MaterialDocument m_savedSnapshot;   // last saved/loaded state, for the dirty flag
    bool m_hasSavedSnapshot = false;
    std::string m_outputDirectory;
    std::string m_lastSavedPath;
    std::string m_status;
    bool m_savedThisFrame = false;

    // Live PBR preview (held by pointer so this header pulls in no engine headers).
    std::unique_ptr<MaterialPreview> m_preview;
    bool  m_useLivePreview = true;
    int   m_previewShape   = 0;       // 0 sphere, 1 cube, 2 plane
    int   m_previewChannel = 0;       // 0 full, 1 albedo, 2 metallic, 3 roughness, 4 normal, 5 ao
    float m_previewYaw     = 35.0f;
    float m_previewPitch   = 18.0f;
    float m_previewEnv     = 0.42f;   // DayNightCycle time-of-day for the lighting
    float m_previewEnvYaw  = 0.0f;    // rotate the environment / key light
    float m_previewEnvApplied = 0.42f; // last slider values committed for IBL baking
    float m_previewEnvYawApplied = 0.0f;
    float m_previewLight   = 1.0f;    // key-light intensity multiplier
    float m_previewZoom    = 1.0f;    // mouse-wheel zoom (higher = closer)
    bool  m_previewGround  = false;   // ground plane + contact shadow
    bool  m_linkTilingAxes = true;    // edit U and V as one uniform tiling value
    float m_previewBg[3]   = {0.05f, 0.06f, 0.08f};
    std::string m_hdriPath;           // equirectangular environment image (optional)

    char m_nameBuffer[128]{};
    char m_outputDirectoryBuffer[1024]{};
    char m_albedoMapBuffer[1024]{};
    char m_normalMapBuffer[1024]{};
    char m_metalRoughMapBuffer[1024]{};
    char m_heightMapBuffer[1024]{};
    char m_shaderPathBuffer[1024]{};
    char m_shaderSearchBuffer[128]{};
    char m_hdriPathBuffer[1024]{};

    // ORM channel-packer source paths.
    std::string m_packMetallic;
    std::string m_packRoughness;
    std::string m_packAO;

    // Material library (saved .3dgmat files in the output directory).
    std::vector<std::string> m_libraryFiles;
    bool m_libraryScanned = false;

    // Engine-owned Surface shaders found below the active project Content root.
    // This is cached so the panel does not recursively scan Content every frame.
    std::vector<std::string> m_shaderLibraryFiles;
    bool m_shaderLibraryScanned = false;
    std::unordered_map<std::string, ShaderParameterMetadata>
        m_shaderParameterMetadata;

    // Import-from-model state.
    std::string m_importModelPath;
    int         m_importMaterialIndex = 0;
    int         m_importMaterialCount = 0;
};

} // namespace material_maker
