#include "EditorAssets.h"

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

    fs::remove_all(root, ec);
    std::cout << "editor assets tests passed\n";
    return 0;
}
