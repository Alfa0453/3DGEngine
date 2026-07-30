#include "EditorAssets.h"
#include "AnimationClipAsset.h"
#include "AnimationGraphAsset.h"

#include <engine/assets/AssetRegistry.h>
#include <engine/assets/SkeletalAsset.h>
#include <engine/assets/StaticMeshAsset.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {
void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

int FolderIndex(const EditorAssets& assets, const std::string& name) {
    for (int i = 0; i < static_cast<int>(assets.Folders().size()); ++i) {
        if (assets.Folders()[static_cast<std::size_t>(i)].displayName == name) return i;
    }
    return -1;
}

const EditorAssets::Asset* AssetNamed(const EditorAssets& assets, const std::string& name) {
    for (const EditorAssets::Asset& asset : assets.Assets()) {
        if (asset.displayName == name) return &asset;
    }
    return nullptr;
}
} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "3dg_editor_assets_rename_test";
    std::error_code ec;
    fs::remove_all(root, ec);

    EditorAssets assets;
    std::string error;
    Check(assets.Refresh(root.string(), &error), "create temporary Content root");
    std::ofstream(root / "gameplay.hud") << "3DGHud 1\n";
    Check(assets.Refresh(root.string(), &error), "scan editor asset types");
    const EditorAssets::Asset* hud = AssetNamed(assets, "gameplay.hud");
    Check(hud && hud->type == EditorAssets::Type::Hud,
          "HUD documents are classified for double-click editor routing");
    std::ofstream(root / "crate.3dgmesh") << "native static mesh placeholder";
    std::ofstream(root / "wizard.3dgskmesh") << "native skeletal mesh placeholder";
    std::ofstream(root / "wizard.3dgskel") << "native skeleton placeholder";
    std::ofstream(root / "walk.3dganim") << "native animation placeholder";
    Check(assets.Refresh(root.string(), &error), "scan native engine asset extensions");
    Check(AssetNamed(assets, "crate.3dgmesh")
              && AssetNamed(assets, "crate.3dgmesh")->type == EditorAssets::Type::Model,
          "native static mesh is identified as a model");
    Check(AssetNamed(assets, "wizard.3dgskmesh")
              && AssetNamed(assets, "wizard.3dgskmesh")->type
                     == EditorAssets::Type::SkeletalModel,
          "native skeletal mesh has a distinct content-browser type");
    Check(AssetNamed(assets, "wizard.3dgskel")
              && AssetNamed(assets, "wizard.3dgskel")->type == EditorAssets::Type::Skeleton,
          "native skeleton has a distinct content-browser type");
    Check(AssetNamed(assets, "walk.3dganim")
              && AssetNamed(assets, "walk.3dganim")->type == EditorAssets::Type::Animation,
          "native animation has a distinct content-browser type");

    const fs::path importSource = root.parent_path() / "BrowserTriangle.obj";
    {
        std::ofstream source(importSource);
        source << "o BrowserTriangle\n"
               << "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
               << "vn 0 0 1\n"
               << "f 1//1 2//1 3//1\n";
    }
    engine::AssetRegistry registry;
    assets.SetAssetRegistry(&registry);
    engine::ModelSourceInfo sourceInfo;
    Check(engine::InspectModelSource(
              importSource.string(), &sourceInfo, &error)
              && !sourceInfo.IsSkeletal() && sourceInfo.meshCount == 1,
          "import settings identify a raw static model before importing");
    const bool browserImport = assets.ImportAsset(importSource.string(), &error);
    if (!browserImport) std::cerr << "Browser import error: " << error << '\n';
    Check(browserImport,
          "Content Browser converts raw static models during import");
    const EditorAssets::Asset* converted = AssetNamed(assets, "BrowserTriangle.3dgmesh");
    Check(converted && converted->type == EditorAssets::Type::Model
              && !fs::exists(root / "BrowserTriangle.obj")
              && registry.FindByPath("/Game/BrowserTriangle.3dgmesh")
              && assets.LastImportMessage().find("3 vertices") != std::string::npos,
          "Content Browser exposes the native asset and registers import statistics");
    engine::StaticMeshAssetData browserMesh;
    Check(engine::LoadStaticMeshAsset(
              (root / "BrowserTriangle.3dgmesh").string(), &browserMesh, &error)
              && browserMesh.header.id
                     == registry.FindByPath("/Game/BrowserTriangle.3dgmesh")->id,
          "Content Browser output contains the registered stable identity");
    const engine::AssetHandle browserMeshId = browserMesh.header.id;
    {
        std::ofstream source(importSource, std::ios::trunc);
        source << "o BrowserTriangle\n"
               << "v 0 0 0\nv 2 0 0\nv 0 1 0\n"
               << "vn 0 0 1\n"
               << "f 1//1 2//1 3//1\n";
    }
    assets.StaticMeshImportSettings().uniformScale = 2.0f;
    Check(assets.ReimportSelectedStaticMesh(&error)
              && engine::LoadStaticMeshAsset(
                  (root / "BrowserTriangle.3dgmesh").string(),
                  &browserMesh, &error)
              && browserMesh.header.id == browserMeshId
              && browserMesh.maximum[0] == 4.0f,
          "Content Browser reimport uses current settings and preserves identity");

    std::string importFolder;
    Check(assets.CreateFolderAt("", "Imported", &importFolder, &error)
              && importFolder == "Imported",
          "import dialog can create a project destination folder");
    Check(assets.CreateFolderAt(importFolder, "Meshes", &importFolder, &error)
              && importFolder == "Imported/Meshes",
          "import dialog can create and select a nested destination folder");
    Check(assets.ImportAssetToFolder(
              importSource.string(), importFolder, &error)
              && fs::is_regular_file(
                  root / "Imported" / "Meshes" / "BrowserTriangle.3dgmesh")
              && registry.FindByPath(
                  "/Game/Imported/Meshes/BrowserTriangle.3dgmesh"),
          "system-browser import stores converted assets in the chosen project folder");
    Check(!assets.ImportAssetToFolder(
              importSource.string(), "../Outside", &error),
          "import destination cannot escape the project Content folder");
    Check(!assets.ImportAssetToFolder(
              importSource.string(), importFolder,
              EditorAssets::ModelImportMode::SkeletalMesh, &error),
          "explicit skeletal import refuses a source without bones or animation");
    const std::vector<std::string> importFolders = assets.ContentFolderPaths();
    Check(std::find(importFolders.begin(), importFolders.end(),
                    "Imported/Meshes") != importFolders.end(),
          "destination picker lists nested Content folders");
    fs::remove(importSource, ec);

    Check(assets.CreateFolder("Original", &error), "create folder to rename");
    fs::create_directories(root / "Original" / "Nested");
    std::ofstream(root / "Original" / "Nested" / "asset.txt") << "preserved";
    Check(assets.Refresh(root.string(), &error), "refresh nested folder contents");

    assets.SelectFolderIndex(FolderIndex(assets, "Original"));
    Check(assets.CopySelected(&error), "copy selected folder before rename");
    Check(assets.RenameSelectedFolder("Renamed", &error), "rename selected folder");
    Check(!fs::exists(root / "Original")
          && fs::is_regular_file(root / "Renamed" / "Nested" / "asset.txt"),
          "rename preserves nested folder contents");
    Check(assets.SelectedFolder()
          && assets.SelectedFolder()->displayName == "Renamed",
          "renamed folder remains selected");
    Check(assets.CopiedDisplayName() == "Renamed",
          "rename updates copied folder path");

    Check(assets.CreateFolder("Existing", &error), "create conflicting folder");
    assets.SelectFolderIndex(FolderIndex(assets, "Renamed"));
    Check(!assets.RenameSelectedFolder("Existing", &error),
          "rename refuses to overwrite another folder");
    Check(assets.RenameSelectedFolder("renamed", &error),
          "case-only folder rename succeeds");
    Check(fs::is_directory(root / "renamed"), "case-only rename reaches requested spelling");
    Check(!assets.RenameSelectedFolder("..", &error), "invalid parent folder name is rejected");

    AnimationClipAsset action;
    action.name = "StaffAttack";
    action.sourceFile = "Content/Animations/StaffAttack.fbx";
    action.clipName = "Attack_01";
    action.stripRootMotion = true;
    action.loop = true;
    action.speed = 1.25f;
    action.action = true;
    action.maskRootBone = "Spine";
    action.fadeIn = 0.06f;
    action.fadeOut = 0.18f;
    action.events.push_back({0.42f, "CastFireball"});
    action.events.push_back({0.76f, "AttackFinished"});
    const fs::path actionPath = root / "StaffAttack.3dgclip";
    Check(action.Save(actionPath.string(), &error),
          "save standalone action clip metadata");
    AnimationClipAsset loadedAction;
    Check(loadedAction.Load(actionPath.string(), &error),
          "load standalone action clip metadata");
    Check(loadedAction.action && !loadedAction.loop
          && loadedAction.assetId.Valid()
          && loadedAction.assetId == action.assetId
          && loadedAction.name == "StaffAttack"
          && loadedAction.sourceFile == action.sourceFile
          && loadedAction.clipName == "Attack_01"
          && loadedAction.maskRootBone == "Spine"
          && loadedAction.fadeIn == action.fadeIn
          && loadedAction.fadeOut == action.fadeOut
          && loadedAction.speed == action.speed
          && loadedAction.events.size() == 2
          && loadedAction.events[0].time == 0.42f
          && loadedAction.events[0].name == "CastFireball"
          && loadedAction.events[1].name == "AttackFinished",
          "action clip preserves source, mask, fades, speed, events and one-shot behavior");
    Check(assets.Refresh(root.string(), &error)
          && registry.Find(action.assetId)
          && registry.Find(action.assetId)->type
                 == engine::AssetType::AnimationClip,
          "Content refresh registers the stable identity of authored clip assets");

    AnimationGraphAsset graph;
    graph.name = "WizardGraph";
    graph.previewModel = (root / "BrowserTriangle.3dgmesh").string();
    graph.previewModelAssetId = browserMeshId;
    AnimationGraphClip graphClip;
    graphClip.clipAsset = actionPath.string();
    graphClip.clipAssetId = action.assetId;
    graphClip.sourceFile = action.sourceFile;
    graphClip.sourceClipName = action.clipName;
    graphClip.clipName = action.name;
    graph.clips.push_back(graphClip);
    const fs::path graphPath = root / "Wizard.3dggraph";
    Check(graph.Save(graphPath.string(), &error)
          && graph.assetId.Valid(),
          "save animation graph with stable referenced asset IDs");
    AnimationGraphAsset loadedGraph;
    Check(loadedGraph.Load(graphPath.string(), &error)
          && loadedGraph.assetId == graph.assetId
          && loadedGraph.previewModelAssetId == browserMeshId
          && loadedGraph.clips.size() == 1
          && loadedGraph.clips[0].clipAssetId == action.assetId,
          "animation graph identity and clip references round-trip");
    Check(assets.Refresh(root.string(), &error)
          && registry.Find(graph.assetId)
          && registry.Find(graph.assetId)->dependencies.size() == 2,
          "animation graph dependencies are recorded in the project registry");

    fs::remove_all(root, ec);
    std::cout << "editor assets tests passed\n";
    return 0;
}
