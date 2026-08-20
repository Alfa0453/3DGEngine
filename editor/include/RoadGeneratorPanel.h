#pragma once

#include <engine/assets/AssetIdentity.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <string>
#include <vector>

class EditorScene;

class RoadGeneratorPanel {
public:
    enum class Surface { Road, Shoulder, Marking, Curb, Sidewalk, Barrier };
    struct Part {
        std::string suffix;
        glm::vec3 position{0.0f};
        glm::vec3 scale{1.0f};
        glm::quat rotation{1,0,0,0};
        Surface surface = Surface::Road;
    };
    struct Result { bool generate = false; bool remove = false; bool assetsChanged = false; };

    Result Draw(const EditorScene& scene, const std::string& assetRoot, bool* open);
    void QueueOpen(const std::string& path) { m_pendingOpen = path; }
    std::vector<Part> GenerateParts(const std::vector<glm::vec3>& points, bool closed) const;
    const std::string& MaterialFor(Surface surface) const;

    const char* Name() const { return m_name.data(); }
    const std::string& SplineName() const { return m_splineName; }
    bool ReplaceExisting() const { return m_replace; }
    bool CreateColliders() const { return m_colliders; }
    bool ConformTerrain() const { return m_conformTerrain; }
    float TerrainOffset() const { return m_terrainOffset; }
    bool IsDirty() const { return m_dirty; }
    const std::string& Path() const { return m_path; }
    bool SaveForShutdown(const std::string& root, std::string* error) { return Save(root, error); }

    void SetWidth(float value) { m_width = value; }
    void SetLanes(int value) { m_lanes = value; }
    void SetShoulders(bool value) { m_shoulders = value; }
    void SetMarkings(bool value) { m_markings = value; }

private:
    struct AssetChoice { std::string path; std::string name; };
    void NewAsset();
    void Preset(int preset);
    void RefreshMaterials(const std::string& root);
    void MaterialCombo(const char* label, std::string& path);
    bool Save(const std::string& root, std::string* error);
    bool Load(const std::string& path, std::string* error);

    std::array<char, 96> m_name{{'R','o','a','d','_','1','\0'}};
    engine::AssetHandle m_assetId;
    std::string m_path, m_pendingOpen, m_status, m_assetRoot, m_splineName;
    std::vector<AssetChoice> m_materials;
    float m_width = 6.0f;
    float m_thickness = 0.18f;
    float m_spacing = 1.5f;
    int m_lanes = 2;
    bool m_shoulders = true;
    float m_shoulderWidth = 1.0f;
    bool m_markings = true;
    float m_markingWidth = 0.1f;
    float m_markingHeight = 0.012f;
    bool m_curbs = false;
    float m_curbWidth = 0.18f;
    float m_curbHeight = 0.16f;
    bool m_sidewalks = false;
    float m_sidewalkWidth = 1.5f;
    float m_sidewalkHeight = 0.12f;
    bool m_barriers = false;
    float m_barrierHeight = 0.8f;
    bool m_endCaps = true;
    bool m_conformTerrain = false;
    float m_terrainOffset = 0.03f;
    bool m_colliders = true;
    bool m_replace = true;
    std::string m_roadMaterial, m_shoulderMaterial, m_markingMaterial;
    std::string m_curbMaterial, m_sidewalkMaterial, m_barrierMaterial;
    bool m_dirty = false;
};
