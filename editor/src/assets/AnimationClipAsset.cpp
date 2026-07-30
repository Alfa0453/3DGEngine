#include "AnimationClipAsset.h"

#include <engine/assets/AssetReference.h>
#include <engine/assets/AssetRegistry.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>

bool AnimationClipAsset::Save(const std::string& path, std::string* error) {
    if (!assetId.Valid()) assetId = engine::AssetHandle::Generate();
    version = 4;
    const std::string contentRoot = engine::FindContentRootForAsset(path);
    engine::AssetRegistry registry;
    if (!contentRoot.empty()) {
        std::string ignored;
        registry.Load(
            engine::AssetRegistry::DefaultRegistryPath(contentRoot), &ignored);
        const engine::AssetHandle currentSource =
            engine::MakeAssetReference(
                &registry, contentRoot, sourceFile).id;
        if (currentSource.Valid()) sourceAssetId = currentSource;
    }
    std::error_code ec;
    const std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) { if (error) *error = "Could not write clip asset: " + path; return false; }
    out << "3DG_CLIP " << version << ' ' << assetId.ToString() << '\n'
        << std::quoted(name.empty() ? std::string("-") : name) << ' '
        << std::quoted(sourceFile.empty() ? std::string("-") : sourceFile) << ' '
        << std::quoted(clipName.empty() ? std::string("-") : clipName) << ' '
        << (stripRootMotion ? 1 : 0) << ' '
        << (loop ? 1 : 0) << ' '
        << speed << '\n'
        << "ACTION " << (action ? 1 : 0) << ' '
        << std::quoted(maskRootBone.empty() ? std::string("-") : maskRootBone) << ' '
        << fadeIn << ' ' << fadeOut << '\n'
        << "SOURCE_ASSET "
        << (sourceAssetId.Valid() ? sourceAssetId.ToString() : std::string("-"))
        << '\n'
        << "EVENTS " << events.size() << '\n';
    for (const Event& event : events) {
        out << std::max(event.time, 0.0f) << ' '
            << std::quoted(event.name.empty() ? std::string("-") : event.name) << '\n';
    }
    out << "ASSET_DEPS " << (sourceAssetId.Valid() ? 1 : 0);
    if (sourceAssetId.Valid()) out << ' ' << sourceAssetId.ToString();
    out << '\n';
    return static_cast<bool>(out);
}

bool AnimationClipAsset::Load(const std::string& path, std::string* error) {
    std::ifstream in(path);
    std::string magic;
    int loadedVersion = 0;
    if (!(in >> magic >> loadedVersion) || magic != "3DG_CLIP" || loadedVersion < 1) {
        if (error) *error = "Invalid clip asset: " + path;
        return false;
    }
    assetId = {};
    if (loadedVersion >= 4) {
        std::string id;
        in >> id;
        if (!engine::AssetHandle::Parse(id, &assetId)) {
            if (error) *error = "Clip asset has an invalid stable ID: " + path;
            return false;
        }
    }
    int strip = 0, doLoop = 1;
    in >> std::quoted(name) >> std::quoted(sourceFile) >> std::quoted(clipName)
       >> strip >> doLoop >> speed;
    if (!in) { if (error) *error = "Clip asset is incomplete: " + path; return false; }
    if (name == "-") name.clear();
    if (sourceFile == "-") sourceFile.clear();
    if (clipName == "-") clipName.clear();
    stripRootMotion = strip != 0;
    loop = doLoop != 0;
    action = false;
    maskRootBone.clear();
    fadeIn = 0.08f;
    fadeOut = 0.15f;
    sourceAssetId = {};
    events.clear();
    if (loadedVersion >= 2) {
        std::string tag;
        int isAction = 0;
        in >> tag >> isAction >> std::quoted(maskRootBone) >> fadeIn >> fadeOut;
        if (!in || tag != "ACTION") {
            if (error) *error = "Clip action settings are incomplete: " + path;
            return false;
        }
        action = isAction != 0;
        if (maskRootBone == "-") maskRootBone.clear();
    }
    if (loadedVersion >= 4) {
        std::string tag;
        std::string id;
        in >> tag >> id;
        if (!in || tag != "SOURCE_ASSET"
            || (id != "-" && !engine::AssetHandle::Parse(id, &sourceAssetId))) {
            if (error) *error = "Clip source asset identity is invalid: " + path;
            return false;
        }
    }
    if (loadedVersion >= 3) {
        std::string tag;
        std::size_t eventCount = 0;
        in >> tag >> eventCount;
        if (!in || tag != "EVENTS") {
            if (error) *error = "Clip event data is incomplete: " + path;
            return false;
        }
        events.reserve(eventCount);
        for (std::size_t i = 0; i < eventCount; ++i) {
            Event event;
            in >> event.time >> std::quoted(event.name);
            if (!in) {
                if (error) *error = "Clip event data is incomplete: " + path;
                return false;
            }
            event.time = std::max(event.time, 0.0f);
            if (event.name == "-") event.name.clear();
            events.push_back(std::move(event));
        }
    }
    if (action) loop = false;
    speed = std::max(speed, 0.0f);
    fadeIn = std::max(fadeIn, 0.0f);
    fadeOut = std::max(fadeOut, 0.0f);
    version = 4;
    if (sourceAssetId.Valid()) {
        const std::string contentRoot = engine::FindContentRootForAsset(path);
        engine::AssetRegistry registry;
        std::string ignored;
        if (!contentRoot.empty()
            && registry.Load(
                engine::AssetRegistry::DefaultRegistryPath(contentRoot),
                &ignored)) {
            const std::string resolved = engine::ResolveAssetReference(
                &registry, contentRoot, {sourceAssetId, sourceFile});
            if (!resolved.empty()) sourceFile = resolved;
        }
    }
    return true;
}
