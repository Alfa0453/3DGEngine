#pragma once

#include "engine/assets/AssetIdentity.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

inline constexpr std::uint32_t kQuestAssetVersion = 1;

enum class QuestObjectiveType : std::uint8_t { Custom, Reach, Collect, Defeat, Interact };
enum class QuestRewardType : std::uint8_t { Score, Item, Ability, ScriptEvent };

struct QuestCondition {
    std::string flag;
    bool expected = true;
};

struct QuestObjective {
    std::string id = "Objective";
    std::string description = "Complete the objective";
    QuestObjectiveType type = QuestObjectiveType::Custom;
    std::string target;
    int requiredCount = 1;
    bool optional = false;
    bool checkpoint = false;
    std::string requiredObjective;
    std::string requiredFlag;
    std::string dialogueTrigger;
};

struct QuestReward {
    QuestRewardType type = QuestRewardType::Score;
    std::string name;
    int amount = 1;
    std::string assetPath;
    AssetHandle assetId;
};

struct QuestAssetData {
    NativeAssetHeader header;
    std::string name = "Quest";
    std::string title = "New Quest";
    std::string description;
    bool autoStart = false;
    bool repeatable = false;
    bool hiddenUntilStarted = false;
    std::vector<QuestCondition> startConditions;
    std::vector<QuestObjective> objectives;
    std::vector<QuestReward> rewards;
};

const char* QuestObjectiveTypeName(QuestObjectiveType type);
const char* QuestRewardTypeName(QuestRewardType type);
void NormalizeQuestAsset(QuestAssetData& asset);
bool ValidateQuestAsset(const QuestAssetData& asset, std::string* error = nullptr);
bool SaveQuestAsset(const std::string& path, QuestAssetData asset, std::string* error = nullptr);
bool LoadQuestAsset(const std::string& path, QuestAssetData* asset, std::string* error = nullptr);

} // namespace engine
