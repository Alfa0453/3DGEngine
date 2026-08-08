#pragma once

#include <glm/glm.hpp>

#include <array>
#include <string>
#include <vector>

class EditorScene;

class SplineBuilderPanel {
public:
    enum class Mode { Road = 0, Fence, Props };
    struct Result { bool generate = false; bool remove = false; };

    Result Draw(const EditorScene& scene, const std::string& assetRoot, bool* open);
    void RefreshAssets(const std::string& assetRoot);
    std::vector<glm::vec3> PreviewPoints(const EditorScene& scene) const;

    Mode CurrentMode() const { return static_cast<Mode>(m_mode); }
    const char* GroupName() const { return m_groupName.data(); }
    const std::string& SplineName() const { return m_splineName; }
    float Spacing() const { return m_spacing; }
    float Width() const { return m_width; }
    float Height() const { return m_height; }
    float Thickness() const { return m_thickness; }
    float VerticalOffset() const { return m_verticalOffset; }
    float PostSize() const { return m_postSize; }
    int RailCount() const { return m_rails; }
    float PropScale() const { return m_propScale; }
    bool AlignProps() const { return m_alignProps; }
    bool CreateColliders() const { return m_colliders; }
    bool ReplaceExisting() const { return m_replace; }
    const std::string& MaterialPath() const { return m_materialPath; }
    const std::string& ModelPath() const { return m_modelPath; }

private:
    struct AssetChoice { std::string path; std::string name; };
    std::array<char, 96> m_groupName{{'S','p','l','i','n','e','B','u','i','l','d','_','1','\0'}};
    int m_mode = 0;
    std::string m_splineName;
    float m_spacing = 2.0f;
    float m_width = 4.0f;
    float m_height = 1.4f;
    float m_thickness = 0.18f;
    float m_verticalOffset = 0.03f;
    float m_postSize = 0.14f;
    int m_rails = 2;
    float m_propScale = 1.0f;
    bool m_alignProps = true;
    bool m_colliders = true;
    bool m_replace = true;
    std::string m_assetRoot;
    std::string m_materialPath;
    std::string m_modelPath;
    std::vector<AssetChoice> m_materials;
    std::vector<AssetChoice> m_models;
};
