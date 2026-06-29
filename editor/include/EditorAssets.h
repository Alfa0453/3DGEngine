#pragma once

#include <string>
#include <vector>

class EditorAssets {
public:
    enum class Type {
        Model,
        Texture,
        Shader,
        Audio,
        Scene,
        Other
    };

    struct Asset {
        std::string relativePath;
        std::string displayName;
        Type type = Type::Other;
    };

    bool Refresh(const std::string& rootPath, std::string* error);

    const std::string& RootPath() const { return m_rootPath; }
    const std::vector<Asset>& Assets() const { return m_assets; }
    int SelectedIndex() const { return m_selectedIndex; }
    const Asset* SelectedAsset() const;

    void SelectNext();
    void SelectPrevious();

    static const char* TypeName(Type type);

    private:
        static Type ClassifyExtension(const std::string& extension);

        std::string m_rootPath;
        std::vector<Asset> m_assets;
        int m_selectedIndex = -1;
};