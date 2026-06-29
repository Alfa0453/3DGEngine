#include "EditorAssets.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string NormalizeSlashes(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

} // namespace

bool EditorAssets::Refresh(const std::string &rootPath, std::string *error)
{
    m_rootPath = rootPath;
    m_assets.clear();

    std::error_code ec;
    if (!fs::exists(rootPath, ec) || !fs::is_directory(rootPath, ec)) {
        if (error) *error = "Asset folder not found: " + rootPath;
        return false;
    }

    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(rootPath, ec)) {
        if (ec) {
            if (error) *error = "Could not scan asset folder,";
            return false;
        }
        if (!entry.is_regular_file(ec)) {
            continue;
        }

        const fs::path path = entry.path();
        const std::string relative = NormalizeSlashes(fs::relative(path, rootPath, ec).string());
        if (ec) {
            continue;
        }

        Asset asset;
        asset.relativePath = relative;
        asset.displayName = path.filename().string();
        asset.type = ClassifyExtension(Lower(path.extension().string()));
        m_assets.push_back(asset);
    }

    std::sort(m_assets.begin(), m_assets.end(), [](const Asset& a, const Asset& b) {
        if (a.type != b.type) {
            return static_cast<int>(a.type) < static_cast<int>(b.type);
        }
        return a.relativePath < b.relativePath;
    });

    if (m_assets.empty()) {
        m_selectedIndex = -1;
    } else if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_assets.size())) {
        m_selectedIndex = 0;
    }

    return true;
}

const EditorAssets::Asset *EditorAssets::SelectedAsset() const
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_assets.size())) {
        return nullptr;
    }
    return &m_assets[static_cast<std::size_t>(m_selectedIndex)];
}

void EditorAssets::SelectNext()
{
    if (m_assets.empty()) {
        m_selectedIndex = -1;
        return;
    }
    m_selectedIndex = (m_selectedIndex + 1) % static_cast<int>(m_assets.size());
}

void EditorAssets::SelectPrevious()
{
    if (m_assets.empty()) {
        m_selectedIndex = -1;
        return;
    }
    m_selectedIndex = (m_selectedIndex <= 0)
        ? static_cast<int>(m_assets.size()) - 1
        : m_selectedIndex - 1;
}

const char *EditorAssets::TypeName(Type type)
{
    switch (type) {
        case Type::Model: return "Model";
        case Type::Texture: return "Texture";
        case Type::Shader: return "Shader";
        case Type::Audio: return "Audio";
        case Type::Scene: return "Scene";
        case Type::Other: return "Other";
    }
    return "Other";
}

EditorAssets::Type EditorAssets::ClassifyExtension(const std::string &extension)
{
    if (extension == ".obj" || extension == ".fbx" || extension == ".gltf" || extension == ".glb") {
    return Type::Model;
    }
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga") {
        return Type::Texture;
    }
    if (extension == ".vert" || extension == ".frag" || extension == ".glsl") {
        return Type::Shader;
    }
    if (extension == ".wav" || extension == ".ogg" || extension == ".mp3") {
        return Type::Audio;
    }
    if (extension == ".scene") {
        return Type::Scene;
    }
    return Type::Other;
}
