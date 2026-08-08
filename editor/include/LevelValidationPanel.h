#pragma once

#include <string>
#include <vector>

class EditorScene;

class LevelValidationPanel {
public:
    enum class Severity { Info, Warning, Error };
    enum class Kind {
        EmptyName,
        DuplicateName,
        InvalidTransform,
        TinyScale,
        MissingModel,
        MissingMaterial,
        MissingScript,
        MissingAudio,
        MissingParticle,
        MissingBehaviorTree,
        MissingTarget,
        InvalidCollider,
        OrphanEmpty,
        SceneConfiguration
    };

    struct Issue {
        Severity severity = Severity::Warning;
        Kind kind = Kind::SceneConfiguration;
        int objectIndex = -1;
        std::string objectName;
        std::string message;
        bool fixable = false;
        bool safeFix = false;
    };

    void Draw(EditorScene& scene, const std::string& assetRoot, bool* open);
    void Scan(const EditorScene& scene, const std::string& assetRoot);

private:
    bool FixIssue(EditorScene& scene, const Issue& issue);
    int FixAllSafe(EditorScene& scene);
    bool AssetExists(const std::string& path, const std::string& assetRoot) const;

    std::vector<Issue> m_issues;
    std::string m_assetRoot;
    int m_lastObjectCount = -1;
    int m_selectedIssue = -1;
    bool m_showInfo = true;
    bool m_showWarnings = true;
    bool m_showErrors = true;
};
