#pragma once

#include "engine/assets/AssetIdentity.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

inline constexpr std::uint32_t kDialogueAssetVersion = 1;

struct DialogueCondition {
    std::string flag;
    bool expected = true;
};

struct DialogueSpeaker {
    std::string id = "Speaker";
    std::string displayName = "Speaker";
    std::string localizationKey;
    std::string portraitPath;
    AssetHandle portraitId;
};

struct DialogueChoice {
    std::string text = "Continue";
    std::string localizationKey;
    std::string nextNode;
    std::string event;
    std::vector<DialogueCondition> conditions;
};

struct DialogueNode {
    std::string id = "Start";
    std::string speaker = "Speaker";
    std::string text = "New dialogue line";
    std::string localizationKey;
    std::string voicePath;
    AssetHandle voiceId;
    std::string cameraHook;
    std::string enterEvent;
    std::string exitEvent;
    std::vector<DialogueCondition> conditions;
    std::vector<DialogueChoice> choices;
};

struct DialogueAssetData {
    NativeAssetHeader header;
    std::string name = "Dialogue";
    std::string entryNode = "Start";
    bool skippable = true;
    bool pauseGameplay = false;
    bool duckMusic = true;
    std::vector<DialogueSpeaker> speakers;
    std::vector<DialogueNode> nodes;
};

void NormalizeDialogueAsset(DialogueAssetData& asset);
bool ValidateDialogueAsset(const DialogueAssetData& asset, std::string* error = nullptr);
bool SaveDialogueAsset(const std::string& path, DialogueAssetData asset, std::string* error = nullptr);
bool LoadDialogueAsset(const std::string& path, DialogueAssetData* asset, std::string* error = nullptr);

} // namespace engine
