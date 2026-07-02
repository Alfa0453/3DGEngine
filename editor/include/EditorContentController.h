#pragma once

#include "EditorAssets.h"

#include <string>

class EditorLog;
class EditorPanels;
class EditorProject;
class EditorScene;

class EditorContentController {
public:
    void Refresh(EditorAssets& assets, const EditorProject& project, EditorLog& log) const;
    void GoUp(EditorAssets& assets, EditorLog& log) const;
    void CreateFolder(EditorAssets& assets, const std::string& name, EditorLog& log) const;
    void ImportAsset(EditorAssets& assets, const std::string& sourcePath, EditorLog& log) const;
    bool CopyEntry(EditorAssets& assets, EditorLog& log) const;
    void PasteEntry(EditorAssets& assets, EditorLog& log) const;
    void DeleteEntry(EditorAssets& assets, EditorLog& log) const;
    bool UseSelectedAsset(EditorAssets& assets, EditorProject& project, EditorLog& log) const;

    std::string AssetFullPath(const EditorAssets& assets, const EditorAssets::Asset& asset) const;
    float AssetPanelTop(const EditorPanels& panels, const EditorScene& scene) const;
    int FolderIndexAtPosition(const EditorAssets& assets,
                              const EditorPanels& panels,
                              const EditorScene& scene,
                              float x,
                              float y) const;
    int AssetIndexAtPosition(const EditorAssets& assets,
                             const EditorPanels& panels,
                             const EditorScene& scene,
                             float x,
                             float y) const;
};
