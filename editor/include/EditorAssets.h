#pragma once

#include <filesystem>
#include <string>
#include <vector>

class EditorAssets {
public:
    enum class Type {
        Model,
        Material,
        Texture,
        Shader,
        Audio,
        Scene,
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
    bool ImportAsset(const std::string& sourcePath, std::string* error);
    bool EnterFolder(int index, std::string* error);
    bool EnterSelectedFolder(std::string* error);
    bool GoUp(std::string* error);
    bool CopySelected(std::string* error);
    bool PasteCopied(std::string* error);
    bool DeleteSelectedEntry(std::string* error);

    const std::string& RootPath() const { return m_rootPath; }
    const std::string& CurrentFolder() const { return m_currentFolder; }
    const std::vector<Asset>& Assets() const { return m_assets; }
    const std::vector<Folder>& Folders() const { return m_folders; }
    std::size_t TotalFileCount() const { return m_totalFileCount; }
    SelectionType SelectedType() const { return m_selectedType; }
    int SelectedFolderIndex() const { return m_selectedFolderIndex; }
    int SelectedIndex() const { return m_selectedIndex; }
    const Folder* SelectedFolder() const;
    const Asset* SelectedAsset() const;
    bool HasCopiedEntry() const { return !m_clipboardRelativePath.empty(); }
    std::string CopiedDisplayName() const;
    std::string SelectedAssetFullPath() const;
    std::string CopiedFullPath() const;

    void SelectNext();
    void SelectPrevious();
    void SelectFolderIndex(int index);
    void SelectIndex(int index);

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
};