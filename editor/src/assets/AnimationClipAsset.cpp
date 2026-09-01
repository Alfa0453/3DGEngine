#include "AnimationClipAsset.h"

#include <engine/assets/AssetReference.h>
#include <engine/assets/AssetRegistry.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <cmath>

void AnimationClipAsset::Normalize(float sourceDuration) {
    speed = std::clamp(std::isfinite(speed) ? speed : 1.0f, 0.0f, 8.0f);
    fadeIn = std::clamp(std::isfinite(fadeIn) ? fadeIn : 0.08f, 0.0f, 30.0f);
    fadeOut = std::clamp(std::isfinite(fadeOut) ? fadeOut : 0.15f, 0.0f, 30.0f);
    playbackStart = std::max(std::isfinite(playbackStart) ? playbackStart : 0.0f, 0.0f);
    playbackEnd = std::isfinite(playbackEnd) ? playbackEnd : -1.0f;
    if (sourceDuration > 0.0f) {
        playbackStart = std::min(playbackStart, sourceDuration);
        if (playbackEnd > playbackStart) playbackEnd = std::min(playbackEnd, sourceDuration);
    }
    if (playbackEnd >= 0.0f && playbackEnd <= playbackStart) playbackEnd = -1.0f;
    additiveReferenceTime = std::max(
        std::isfinite(additiveReferenceTime) ? additiveReferenceTime : 0.0f, 0.0f);
    if (sourceDuration > 0.0f) additiveReferenceTime = std::min(additiveReferenceTime, sourceDuration);
    for (Event& event : events) {
        event.time = std::max(std::isfinite(event.time) ? event.time : playbackStart, 0.0f);
        if (sourceDuration > 0.0f) event.time = std::min(event.time, sourceDuration);
    }
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        return a.time < b.time;
    });
    for (std::size_t i = 0; i < curves.size(); ++i) {
        Curve& curve = curves[i];
        if (curve.name.empty()) curve.name = "Curve" + std::to_string(i + 1);
        for (CurveKey& key : curve.keys) {
            key.time = std::max(std::isfinite(key.time) ? key.time : playbackStart, 0.0f);
            key.value = std::isfinite(key.value) ? key.value : 0.0f;
            if (sourceDuration > 0.0f) key.time = std::min(key.time, sourceDuration);
        }
        std::stable_sort(curve.keys.begin(), curve.keys.end(),
            [](const CurveKey& a, const CurveKey& b) { return a.time < b.time; });
    }
}

float AnimationClipAsset::PlaybackDuration(float sourceDuration) const {
    const float end = playbackEnd > playbackStart
        ? playbackEnd : std::max(sourceDuration, playbackStart);
    return std::max(end - playbackStart, 0.0f);
}

float AnimationClipAsset::SampleCurve(const std::string& curveName, float playbackTime,
                                      float fallback) const {
    const auto curve = std::find_if(curves.begin(), curves.end(),
        [&](const Curve& value) { return value.name == curveName; });
    if (curve == curves.end() || curve->keys.empty()) return fallback;
    const float sourceTime = playbackStart + std::max(playbackTime, 0.0f);
    if (sourceTime <= curve->keys.front().time) return curve->keys.front().value;
    if (sourceTime >= curve->keys.back().time) return curve->keys.back().value;
    const auto upper = std::upper_bound(curve->keys.begin(), curve->keys.end(), sourceTime,
        [](float time, const CurveKey& key) { return time < key.time; });
    const CurveKey& b = *upper;
    const CurveKey& a = *(upper - 1);
    const float span = std::max(b.time - a.time, 0.000001f);
    return a.value + (b.value - a.value) * ((sourceTime - a.time) / span);
}

bool AnimationClipAsset::Save(const std::string& path, std::string* error) {
    if (!assetId.Valid()) assetId = engine::AssetHandle::Generate();
    version = 5;
    Normalize();
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
    out << "TIMELINE " << playbackStart << ' ' << playbackEnd << ' '
        << (additive ? 1 : 0) << ' '
        << std::quoted(additiveReferenceClip.empty() ? std::string("-")
                                                     : additiveReferenceClip)
        << ' ' << additiveReferenceTime << '\n';
    out << "CURVES " << curves.size() << '\n';
    for (const Curve& curve : curves) {
        out << std::quoted(curve.name.empty() ? std::string("Curve") : curve.name)
            << ' ' << curve.keys.size();
        for (const CurveKey& key : curve.keys) out << ' ' << key.time << ' ' << key.value;
        out << '\n';
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
    playbackStart = 0.0f;
    playbackEnd = -1.0f;
    additive = false;
    additiveReferenceClip.clear();
    additiveReferenceTime = 0.0f;
    curves.clear();
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
    if (loadedVersion >= 5) {
        std::string tag;
        std::string reference;
        int isAdditive = 0;
        in >> tag >> playbackStart >> playbackEnd >> isAdditive
           >> std::quoted(reference) >> additiveReferenceTime;
        if (!in || tag != "TIMELINE") {
            if (error) *error = "Clip timeline data is incomplete: " + path;
            return false;
        }
        additive = isAdditive != 0;
        if (reference != "-") additiveReferenceClip = reference;
        std::size_t curveCount = 0;
        in >> tag >> curveCount;
        if (!in || tag != "CURVES" || curveCount > 128) {
            if (error) *error = "Clip curve data is invalid: " + path;
            return false;
        }
        curves.resize(curveCount);
        for (Curve& curve : curves) {
            std::size_t keyCount = 0;
            in >> std::quoted(curve.name) >> keyCount;
            if (!in || keyCount > 4096) {
                if (error) *error = "Clip curve contains too many keys: " + path;
                return false;
            }
            curve.keys.resize(keyCount);
            for (CurveKey& key : curve.keys) in >> key.time >> key.value;
            if (!in) {
                if (error) *error = "Clip curve data is incomplete: " + path;
                return false;
            }
        }
    }
    if (action) loop = false;
    Normalize();
    version = 5;
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
