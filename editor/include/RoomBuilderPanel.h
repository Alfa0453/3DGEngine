#pragma once

#include <glm/glm.hpp>

#include <array>
#include <string>
#include <vector>

class RoomBuilderPanel {
public:
    struct Result {
        bool generateRequested = false;
        bool deleteExistingRequested = false;
    };

    Result Draw(const std::string& assetRoot, bool* open);
    void RefreshMaterials(const std::string& assetRoot);
    bool CapturingOutline() const { return m_captureOutline; }
    void SetHoverPoint(const glm::vec3& point);
    void CapturePoint(const glm::vec3& point);
    void CancelCapture();

    bool HasFirstCorner() const { return m_hasFirst; }
    bool HasRoom() const { return m_hasFirst && m_hasSecond; }
    glm::vec3 FirstCorner() const { return m_first; }
    glm::vec3 SecondCorner() const { return m_second; }
    glm::vec3 PreviewCorner() const { return m_hasSecond ? m_second : m_hover; }
    const char* RoomName() const { return m_roomName.data(); }
    float WallHeight() const { return m_wallHeight; }
    float WallThickness() const { return m_wallThickness; }
    float FloorThickness() const { return m_floorThickness; }
    bool CreateFloor() const { return m_createFloor; }
    bool CreateCeiling() const { return m_createCeiling; }
    bool CreateCornerPosts() const { return m_cornerPosts; }
    bool CreateColliders() const { return m_colliders; }
    bool ReplaceExisting() const { return m_replaceExisting; }
    bool DoorEnabled() const { return m_doorEnabled; }
    int DoorWall() const { return m_doorWall; }
    float DoorWidth() const { return m_doorWidth; }
    float DoorHeight() const { return m_doorHeight; }
    float DoorOffset() const { return m_doorOffset; }
    const std::string& MaterialPath() const { return m_materialPath; }

private:
    struct MaterialChoice { std::string path; std::string name; };
    glm::vec3 Snap(const glm::vec3& value) const;

    std::array<char, 96> m_roomName{{'R','o','o','m','_','1','\0'}};
    std::vector<MaterialChoice> m_materials;
    std::string m_materialRoot;
    std::string m_materialPath;
    glm::vec3 m_first{0.0f};
    glm::vec3 m_second{6.0f, 0.0f, 6.0f};
    glm::vec3 m_hover{0.0f};
    bool m_hasFirst = false;
    bool m_hasSecond = false;
    bool m_captureOutline = false;
    float m_gridSize = 0.5f;
    float m_wallHeight = 3.0f;
    float m_wallThickness = 0.25f;
    float m_floorThickness = 0.2f;
    bool m_createFloor = true;
    bool m_createCeiling = false;
    bool m_cornerPosts = false;
    bool m_colliders = true;
    bool m_replaceExisting = true;
    bool m_doorEnabled = true;
    int m_doorWall = 0;
    float m_doorWidth = 1.2f;
    float m_doorHeight = 2.2f;
    float m_doorOffset = 0.0f;
};
