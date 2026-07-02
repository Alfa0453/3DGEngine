#include "EditorContentController.h"

#include "EditorLog.h"
#include "EditorPanels.h"
#include "EditorProject.h"
#include "EditorScene.h"

#include <filesystem>

void EditorContentController::Refresh(EditorAssets& assets, const EditorProject& project, EditorLog& log) const {
    std::string error;
    if (assets.Refresh(project.AssetRoot(), &error)) {
        log.Info("Scanned Content: " + std::to_string(assets.TotalFileCount()) + " files");
    } else {
        log.Error(error);
    }
}

void EditorContentController::GoUp(EditorAssets& assets, EditorLog& log) const {
    std::string error;
    if (assets.GoUp(&error)) {
        log.Info("Content folder: " + (assets.CurrentFolder().empty() ? std::string("/") : assets.CurrentFolder()));
    } else {
        log.Error(error);
    }
}

void EditorContentController::CreateFolder(EditorAssets& assets, const std::string& name, EditorLog& log) const {
    std::string error;
    if (assets.CreateFolder(name, &error)) {
        log.Info("Created Content folder: " + name);
    } else {
        log.Error(error);
    }
}

void EditorContentController::ImportAsset(EditorAssets& assets, const std::string& sourcePath, EditorLog& log) const {
    std::string error;
    if (assets.ImportAsset(sourcePath, &error)) {
        log.Info("Imported asset: " + sourcePath);
    } else {
        log.Error(error);
    }
}

bool EditorContentController::CopyEntry(EditorAssets& assets, EditorLog& log) const {
    std::string error;
    if (assets.CopySelected(&error)) {
        log.Info("Copied Content entry: " + assets.CopiedDisplayName());
        return true;
    }
    log.Warning(error);
    return false;
}

void EditorContentController::PasteEntry(EditorAssets& assets, EditorLog& log) const {
    std::string error;
    if (assets.PasteCopied(&error)) {
        log.Info("Pasted Content entry");
    } else {
        log.Warning(error);
    }
}

void EditorContentController::DeleteEntry(EditorAssets& assets, EditorLog& log) const {
    std::string error;
    if (assets.DeleteSelectedEntry(&error)) {
        log.Info("Deleted Content entry");
    } else {
        log.Warning(error);
    }
}

bool EditorContentController::UseSelectedAsset(EditorAssets& assets, EditorProject& project, EditorLog& log) const {
    if (assets.SelectedType() == EditorAssets::SelectionType::Folder) {
        std::string error;
        if (assets.EnterSelectedFolder(&error)) {
            log.Info("Content folder: " + assets.CurrentFolder());
        } else {
            log.Warning(error);
        }
        return false;
    }

    const EditorAssets::Asset* asset = assets.SelectedAsset();
    if (!asset) {
        log.Warning("No asset selected");
        return false;
    }

    if (asset->type != EditorAssets::Type::Scene) {
        log.Warning("Selected asset is not a scene");
        return false;
    }

    project.SetScenePath(AssetFullPath(assets, *asset));
    return true;
}

std::string EditorContentController::AssetFullPath(const EditorAssets& assets,
                                                   const EditorAssets::Asset& asset) const {
    return (std::filesystem::path(assets.RootPath()) / asset.relativePath).string();
}

float EditorContentController::AssetPanelTop(const EditorPanels& panels, const EditorScene& scene) const {
    float y = 120.0f;
    if (panels.IsOpen(EditorPanels::Panel::Hierarchy)) {
        y += static_cast<float>(scene.Objects().size()) * 26.0f;
    }
    return y + 28.0f;
}

int EditorContentController::FolderIndexAtPosition(const EditorAssets& assets,
                                                   const EditorPanels& panels,
                                                   const EditorScene& scene,
                                                   float x,
                                                   float y) const {
    if (x < 30.0f || x > 430.0f) {
        return -1;
    }

    const float rowTop = AssetPanelTop(panels, scene) + 78.0f;
    const float rowHeight = 22.0f;
    if (y < rowTop) {
        return -1;
    }

    const int row = static_cast<int>((y - rowTop) / rowHeight);
    const int maxVisible = 8;
    if (row < 0 || row >= maxVisible || row >= static_cast<int>(assets.Folders().size())) {
        return -1;
    }

    return row;
}

int EditorContentController::AssetIndexAtPosition(const EditorAssets& assets,
                                                  const EditorPanels& panels,
                                                  const EditorScene& scene,
                                                  float x,
                                                  float y) const {
    if (x < 30.0f || x > 430.0f) {
        return -1;
    }

    const float rowTop = AssetPanelTop(panels, scene) + 78.0f;
    const float rowHeight = 22.0f;
    if (y < rowTop) {
        return -1;
    }

    const int row = static_cast<int>((y - rowTop) / rowHeight);
    const int index = row - static_cast<int>(assets.Folders().size());
    const int maxVisible = 8;
    if (row < 0 || row >= maxVisible || index < 0 || index >= static_cast<int>(assets.Assets().size())) {
        return -1;
    }

    return index;
}
