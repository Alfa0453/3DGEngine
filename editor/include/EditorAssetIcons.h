#pragma once

#include "EditorAssets.h"
#include "EditorIcons.h"

#include <imgui.h>

namespace editor::icons {

struct AssetStyle {
    const char* glyph = Document;
    ImVec4 color{0.76f, 0.80f, 0.88f, 1.0f};
};

inline AssetStyle ForAsset(EditorAssets::Type type) {
    switch (type) {
    case EditorAssets::Type::Model:
        return {Layers, {0.25f, 0.78f, 0.95f, 1.0f}};
    case EditorAssets::Type::SkeletalModel:
        return {Contact, {0.34f, 0.64f, 1.0f, 1.0f}};
    case EditorAssets::Type::Skeleton:
        return {Link, {0.65f, 0.55f, 0.95f, 1.0f}};
    case EditorAssets::Type::Animation:
    case EditorAssets::Type::AnimationClip:
        return {Play, {0.30f, 0.85f, 0.55f, 1.0f}};
    case EditorAssets::Type::AnimationGraph:
        return {Layers, {0.36f, 0.83f, 0.58f, 1.0f}};
    case EditorAssets::Type::Material:
        return {Palette, {1.0f, 0.65f, 0.28f, 1.0f}};
    case EditorAssets::Type::Texture:
        return {Image, {1.0f, 0.48f, 0.68f, 1.0f}};
    case EditorAssets::Type::Shader:
        return {Code, {0.70f, 0.48f, 1.0f, 1.0f}};
    case EditorAssets::Type::Audio:
        return {Music, {0.23f, 0.82f, 0.82f, 1.0f}};
    case EditorAssets::Type::Scene:
    case EditorAssets::Type::World:
        return {World, {0.30f, 0.66f, 1.0f, 1.0f}};
    case EditorAssets::Type::Particle:
    case EditorAssets::Type::ParticleEffect:
        return {Star, {1.0f, 0.77f, 0.24f, 1.0f}};
    case EditorAssets::Type::Hud:
        return {Screen, {0.33f, 0.72f, 1.0f, 1.0f}};
    case EditorAssets::Type::Character:
        return {Contact, {0.38f, 0.70f, 1.0f, 1.0f}};
    case EditorAssets::Type::BehaviorGraph:
        return {Link, {0.82f, 0.52f, 1.0f, 1.0f}};
    case EditorAssets::Type::Prefab:
        return {Archive, {0.94f, 0.62f, 0.30f, 1.0f}};
    case EditorAssets::Type::Script:
        return {Code, {0.67f, 0.52f, 1.0f, 1.0f}};
    case EditorAssets::Type::Foliage:
        return {Leaf, {0.31f, 0.84f, 0.38f, 1.0f}};
    case EditorAssets::Type::Other:
        break;
    }
    return {};
}

inline ImVec4 FolderColor() {
    return {0.95f, 0.72f, 0.28f, 1.0f};
}

} // namespace editor::icons
