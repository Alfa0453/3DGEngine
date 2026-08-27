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

        std::string m_rootPath;
        std::string m_currentFolder;
        std::vector<Asset> m_assets;
        std::vector<Folder> m_folders;
        std::size_t m_totalFileCount = 0;
        SelectionType m_selectedType = SelectionType::None;
        int m_selectedFolderIndex = -1;
        int m_selectedIndex = -1;
        std::string m_clipboardRelativePath;
        bool m_clipboardIsFolder = false;
        bool m_clipboardIsCut = false;   // Paste moves (and clears) instead of copying
        engine::AssetRegistry* m_assetRegistry = nullptr;
        engine::StaticMeshImportOptions m_staticMeshImportOptions;
        engine::SkeletalImportOptions m_skeletalImportOptions;
        engine::TextureImportOptions m_textureImportOptions;
        std::string m_lastImportMessage;
};
