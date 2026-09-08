#pragma once

#include <engine/assets/StaticMeshAsset.h>
#include <engine/assets/SkeletalAsset.h>
#include <engine/assets/TextureAsset.h>

#include <filesystem>
#include <string>
#include <vector>

namespace engine { class AssetRegistry; }

class EditorAssets {
public:
    enum class ModelImportMode {
        Automatic,
        StaticMesh,
        SkeletalMesh,
        Animation
    };

    enum class Type {
        Model,
        SkeletalModel,
        Skeleton,
        Animation,
        Material,
        Texture,
        Shader,
        Audio,
        Scene,
        Particle,
        ParticleEffect,
        Hud,
        Character,
        AnimationClip,
        AnimationGraph,
        BehaviorGraph,
        Prefab,
        Script,
        World,
        Foliage,
        Terrain,
        Ragdoll,
        AnimationRetarget,
        Ability,
        Weather,
        Lighting,
        Building,
        Road,
        ScatterGraph,
        Biome,
        DayNightTimeline,
        Cave,
        FenceWall,
        Destruction,
        Interaction,
        Portal,
        Quest,
        Dialogue,
        Item,
        Combat,
        Spawn,
        SaveProfile,
        IKRig,
        PoseLibrary,
        EquipmentSet,
        Localization,
        Other
    };

    enum class SelectionType {
        None,
        Folder,
        Asset
    };

    struct Asset {
        std::string relativePath;
        std::string displayName;
        Type type = Type::Other;
    };

    struct Folder {
        std::string relativePath;
        std::string displayName;
    };

    bool Refresh(const std::string& rootPath, std::string* error);
    bool CreateFolder(const std::string& name, std::string* error);
    bool CreateFolderAt(const std::string& parentRelativePath,
                        const std::string& name,
                        std::string* createdRelativePath,
                        std::string* error);
    bool ImportAsset(const std::string& sourcePath, std::string* error);
    bool ImportAsset(const std::string& sourcePath, ModelImportMode modelMode,
                     std::string* error);
    bool ImportAssetToFolder(const std::string& sourcePath,
                             const std::string& destinationRelativePath,
                             std::string* error);
    bool ImportAssetToFolder(const std::string& sourcePath,
                             const std::string& destinationRelativePath,
                             ModelImportMode modelMode,
                             std::string* error);
    bool ReimportSelectedStaticMesh(std::string* error);
    bool ReimportSelectedSkeletalAssets(std::string* error);
    bool ReimportSelectedTexture(std::string* error);
    void SetAssetRegistry(engine::AssetRegistry* registry) { m_assetRegistry = registry; }
    engine::AssetHandle AssetIdForPath(const std::string& relativePath) const;
    engine::StaticMeshImportOptions& StaticMeshImportSettings() {
        return m_staticMeshImportOptions;
    }
    engine::SkeletalImportOptions& SkeletalImportSettings() {
        return m_skeletalImportOptions;
    }
    engine::TextureImportOptions& TextureImportSettings() {
        return m_textureImportOptions;
    }
    bool EnterFolder(int index, std::string* error);
    bool EnterSelectedFolder(std::string* error);
    bool GoUp(std::string* error);
    bool CopySelected(std::string* error);
    bool CutSelected(std::string* error);   // mark for move; Paste relocates it
    bool PasteCopied(std::string* error);
    bool RenameSelectedEntry(const std::string& newName, std::string* error);
    bool RenameSelectedFolder(const std::string& newName, std::string* error);
    bool DeleteSelectedEntry(std::string* error);
    // Batch delete: removes the whole asset multi-selection (or the selected folder). Returns the
    // number deleted; on any failure `error` is set and the successfully-deleted ones are gone.
    int  DeleteSelectedAssets(std::string* error);

    const std::string& RootPath() const { return m_rootPath; }
    const std::string& CurrentFolder() const { return m_currentFolder; }
    const std::vector<Asset>& Assets() const { return m_assets; }
    const std::vector<Folder>& Folders() const { return m_folders; }
    std::vector<std::string> ContentFolderPaths() const;
    // Project-relative native asset paths across all Content folders.
    std::vector<std::string> ContentAssetPaths(Type type) const;
    std::size_t TotalFileCount() const { return m_totalFileCount; }
    SelectionType SelectedType() const { return m_selectedType; }
    int SelectedFolderIndex() const { return m_selectedFolderIndex; }
    int SelectedIndex() const { return m_selectedIndex; }

    // Multi-selection (Ctrl/Shift). m_selectedIndex stays the "primary" (last-clicked) asset that the
    // single-target operations use; this set holds the whole highlighted group.
    const std::vector<int>& SelectedIndices() const { return m_selectedIndices; }
    bool IsAssetSelected(int index) const;
    void ToggleAssetSelection(int index);          // Ctrl+click: add/remove one asset
    void SelectAssetRange(int anchor, int index);  // Shift+click: contiguous range [anchor..index]
    const Folder* SelectedFolder() const;
    const Asset* SelectedAsset() const;
    bool HasCopiedEntry() const { return !m_clipboardRelativePath.empty(); }
    bool CopiedEntryIsCut() const { return m_clipboardIsCut; }
    std::string CopiedDisplayName() const;
    std::string SelectedAssetFullPath() const;
    std::string CopiedFullPath() const;
    const std::string& LastImportMessage() const { return m_lastImportMessage; }

    void SelectNext();
    void SelectPrevious();
    void SelectFolderIndex(int index);
    void SelectIndex(int index);
    bool RevealAsset(const std::string& relativePath, std::string* error);

    static const char* TypeName(Type type);

    private:
        static Type ClassifyExtension(const std::string& extension);
        static std::string SanitizeFolderName(const std::string& name);
        static std::filesystem::path UniqueDestinationPath(const std::filesystem::path& destination);
        std::string CurrentPath() const;
        std::string FullPathForRelative(const std::string& relativePath) const;
        // Paste one clipboard entry (no Refresh); shared by single and multi paste.
        bool PasteOneEntry(const std::string& relativePath, bool isFolder, bool cut, std::string* error);

        struct ClipEntry { std::string relativePath; bool isFolder = false; };

        std::string m_rootPath;
        std::string m_currentFolder;
        std::vector<Asset> m_assets;
        std::vector<Folder> m_folders;
        std::size_t m_totalFileCount = 0;
        SelectionType m_selectedType = SelectionType::None;
        int m_selectedFolderIndex = -1;
        int m_selectedIndex = -1;
        std::vector<int> m_selectedIndices;   // multi-selection group (Ctrl/Shift)
        std::string m_clipboardRelativePath;         // primary entry (display / rename-sync)
        bool m_clipboardIsFolder = false;
        bool m_clipboardIsCut = false;   // Paste moves (and clears) instead of copying
        std::vector<ClipEntry> m_clipboardEntries;   // full clipboard (multi copy/cut); primary first
        engine::AssetRegistry* m_assetRegistry = nullptr;
        engine::StaticMeshImportOptions m_staticMeshImportOptions;
        engine::SkeletalImportOptions m_skeletalImportOptions;
        engine::TextureImportOptions m_textureImportOptions;
        std::string m_lastImportMessage;
};
