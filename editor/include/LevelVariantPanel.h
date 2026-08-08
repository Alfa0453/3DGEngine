#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class LevelVariantPanel {
public:
    enum class Action {
        None,
        Capture,
        Overwrite,
        Restore,
        Compare,
        Duplicate,
        Rename,
        Delete
    };

    struct Result {
        Action action = Action::None;
        std::string sourcePath;
        std::string targetPath;
    };

    Result Draw(const std::string& assetRoot, bool* open);
    void Refresh(const std::string& assetRoot);
    void SetStatus(std::string status, bool error = false);

private:
    struct Variant {
        std::string name;
        std::string path;
        std::uintmax_t bytes = 0;
    };

    std::string TargetPath(const std::string& assetRoot) const;
    static std::string SanitizeName(std::string name);

    std::vector<Variant> m_variants;
    std::array<char, 96> m_name{{'V','a','r','i','a','n','t','_','1','\0'}};
    std::string m_scannedRoot;
    std::string m_status;
    int m_selected = -1;
    bool m_statusError = false;
};
