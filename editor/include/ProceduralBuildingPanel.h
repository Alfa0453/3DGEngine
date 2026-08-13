#pragma once

#include <glm/glm.hpp>
#include <engine/assets/AssetIdentity.h>

#include <array>
#include <string>
#include <utility>
#include <vector>

class ProceduralBuildingPanel {
public:
    enum class OpeningType { Door, Window, Arch, Stairwell };

    struct Opening {
        OpeningType type = OpeningType::Door;
        int segment = 0;
        int storey = 0;
        float offset = 0.5f;
        float width = 1.2f;
        float height = 2.2f;
        float sill = 0.9f;
    };

    struct Part {
        std::string suffix;
        glm::vec3 position{0.0f};
        glm::vec3 scale{1.0f};
        float yawRadians = 0.0f;
        std::string materialPath;
    };

    struct Result {
        bool generateRequested = false;
        bool deleteExistingRequested = false;
        bool assetListChanged = false;
    };

    Result Draw(const std::string& assetRoot, bool* open);
    void QueueOpen(const std::string& path) { m_pendingOpen = path; }
    std::vector<Part> GenerateParts() const;

    const char* BuildingName() const { return m_name.data(); }
    bool ReplaceExisting() const { return m_replaceExisting; }
    bool CreateColliders() const { return m_colliders; }
    const std::string& AssetPath() const { return m_path; }
    const std::vector<glm::vec2>& Footprint() const { return m_footprint; }
    float BaseHeight() const { return m_baseHeight; }
    float TotalHeight() const { return m_storeys * m_storeyHeight; }

    // Programmatic authoring is shared by tests and future editor automation.
    void SetFootprint(std::vector<glm::vec2> footprint) { m_footprint = std::move(footprint); }
    void SetStoreys(int storeys) { m_storeys = storeys; }
    void SetOpenings(std::vector<Opening> openings) { m_openings = std::move(openings); }

private:
    struct MaterialChoice { std::string path; std::string name; };

    void NewAsset();
    void ApplyPreset(int preset);
    void RefreshMaterials(const std::string& assetRoot);
    bool Save(const std::string& assetRoot, std::string* error);
    bool Load(const std::string& path, std::string* error);
    bool ValidFootprint(std::string* error = nullptr) const;
    void DrawFootprintPreview();
    void DrawMaterialCombo(const char* label, std::string& value);

    std::array<char, 96> m_name{{'B','u','i','l','d','i','n','g','_','1','\0'}};
    std::string m_path;
    engine::AssetHandle m_assetId;
    std::string m_pendingOpen;
    std::string m_status;
    std::string m_materialRoot;
    std::vector<MaterialChoice> m_materials;
    std::vector<glm::vec2> m_footprint;
    std::vector<Opening> m_openings;
    int m_storeys = 1;
    float m_baseHeight = 0.0f;
    float m_storeyHeight = 3.0f;
    float m_wallThickness = 0.25f;
    float m_floorThickness = 0.2f;
    float m_roofThickness = 0.25f;
    bool m_createFloors = true;
    bool m_createCeilings = true;
    bool m_createRoof = true;
    bool m_createColumns = false;
    bool m_colliders = true;
    bool m_replaceExisting = true;
    std::string m_wallMaterial;
    std::string m_floorMaterial;
    std::string m_roofMaterial;
    int m_selectedPoint = -1;
    bool m_dirty = false;
};
