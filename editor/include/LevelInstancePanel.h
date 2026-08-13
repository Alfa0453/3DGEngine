#pragma once

#include <engine/scene/WorldManifest.h>

#include <array>
#include <string>
#include <vector>

class LevelInstancePanel {
public:
    struct Result {
        bool worldChanged = false;
        bool saveWorld = false;
        bool assetsChanged = false;
        int openSource = -1;
        int breakInstance = -1;
        bool createFromSelection = false;
        std::string selectionScenePath;
        bool removeSelection = true;
    };

    struct Issue { int index = -1; std::string message; };

    Result Draw(engine::WorldManifest& world, const std::string& assetRoot,
                const std::string& currentScenePath, int selectionCount, bool* open);

    static std::vector<Issue> Validate(const engine::WorldManifest& world,
                                       const std::string& worldPath,
                                       const std::string& currentScenePath);
    static void Normalize(engine::LevelRef& level);

private:
    struct SceneChoice { std::string path; std::string label; };
    void RefreshScenes(const std::string& assetRoot);
    void SceneCombo(const char* label, std::string& path);

    std::vector<SceneChoice> m_scenes;
    std::string m_scannedRoot;
    int m_selected = -1;
    std::array<char, 96> m_selectionName{{'L','e','v','e','l','I','n','s','t','a','n','c','e','\0'}};
    bool m_removeSelection = true;
    std::string m_status;
};
