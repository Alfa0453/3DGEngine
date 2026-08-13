#pragma once

#include <engine/scene/WorldManifest.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class WorldPartitionPanel {
public:
    struct Cell {
        int x = 0, z = 0;
        int instances = 0;
        int alwaysLoaded = 0;
        std::uintmax_t sourceBytes = 0;
        int estimatedObjects = 0;
        int estimatedTriangles = 0;
        int estimatedTextures = 0;
    };
    struct Issue { int level = -1; std::string message; };
    struct Result {
        bool changed = false;
        bool saveWorld = false;
        bool createCellFromSelection = false;
        std::string cellScenePath;
        int cellX = 0, cellZ = 0;
    };

    Result Draw(engine::WorldManifest& world, const std::string& assetRoot,
                int selectedObjectCount, bool* open);
    static glm::ivec2 CellFor(const engine::WorldPartitionSettings& settings,
                              const glm::vec3& worldPosition);
    static void AssignCells(engine::WorldManifest& world, bool applyDefaultRadii);
    static std::vector<Cell> BuildCells(const engine::WorldManifest& world,
                                        const std::string& worldPath = {});
    static std::vector<Issue> Validate(const engine::WorldManifest& world);

private:
    void DrawGrid(const engine::WorldManifest& world, const std::vector<Cell>& cells);
    std::array<char,96> m_cellName{{'P','a','r','t','i','t','i','o','n','C','e','l','l','\0'}};
    glm::vec3 m_previewViewer{0.0f};
    int m_selectedCellX = 0, m_selectedCellZ = 0;
};
