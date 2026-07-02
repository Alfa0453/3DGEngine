#pragma once

#include "MaterialMaker/MaterialDocument.h"

#include <string>

namespace material_maker {

class MaterialMakerPanel {
public:
    explicit MaterialMakerPanel(std::string outputDirectory = "MaterialMaker/generated");

    bool Draw();
    bool Draw(bool* open);
    bool DrawContent();
    bool SaveCurrent();
    bool LoadFromFile(const std::string& path);
    void SetOutputDirectory(const std::string& outputDirectory);
    void SetAlbedoMap(const std::string& path);
    void SetNormalMap(const std::string& path);
    void SetMetalRoughMap(const std::string& path);

    const MaterialDocument& CurrentMaterial() const { return m_material; }
    const std::string& LastSavedPath() const { return m_lastSavedPath; }
    const std::string& StatusMessage() const { return m_status; }

private:
    void DrawPreview();
    void DrawSurfaceControls();
    void DrawTextureControls();
    void DrawExportControls();
    void Reset();
    void SyncBuffersFromMaterial();
    bool Save();

    MaterialDocument m_material;
    std::string m_outputDirectory;
    std::string m_lastSavedPath;
    std::string m_status;
    bool m_savedThisFrame = false;

    char m_nameBuffer[128]{};
    char m_outputDirectoryBuffer[260]{};
    char m_albedoMapBuffer[260]{};
    char m_normalMapBuffer[260]{};
    char m_metalRoughMapBuffer[260]{};
};

} // namespace material_maker
