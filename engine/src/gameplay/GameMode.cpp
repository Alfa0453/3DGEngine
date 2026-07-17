#include "engine/gameplay/GameMode.h"

#include "engine/ecs/Registry.h"
#include "engine/gameplay/GameplayComponents.h"   // Health

namespace engine {

GameMode& GameMode::Instance() {
    static GameMode instance;
    return instance;
}

void GameMode::Update(ecs::Registry& registry, ecs::Entity player, float dt) {
    if (m_state != GameState::Playing) return;
    m_elapsed += dt;

    if (loseOnPlayerDeath && player != ecs::kNull) {
        if (const Health* health = registry.TryGet<Health>(player)) {
            if (!health->alive || health->hp <= 0.0f) {
                Lose();
            }
        }
    }
}

void GameMode::Reset() {
    m_state = GameState::Playing;
    m_score = 0;
    m_elapsed = 0.0f;
    m_message.clear();
}

void GameMode::Win(const std::string& message) {
    if (m_state == GameState::GameOver) return;   // a loss already latched
    m_state = GameState::Victory;
    m_message = message;
}

void GameMode::Lose(const std::string& message) {
    if (m_state == GameState::Victory) return;    // a win already latched
    m_state = GameState::GameOver;
    m_message = message;
}

void GameMode::Pause() {
    if (m_state == GameState::Playing) m_state = GameState::Paused;
}

void GameMode::Resume() {
    if (m_state == GameState::Paused) m_state = GameState::Playing;
}

void GameMode::AddScore(int delta) { m_score += delta; }
void GameMode::SetScore(int value) { m_score = value; }

const char* GameMode::StateName(GameState state) {
    switch (state) {
        case GameState::Playing:  return "Playing";
        case GameState::Paused:   return "Paused";
        case GameState::GameOver: return "Game Over";
        case GameState::Victory:  return "Victory";
    }
    return "?";
}

} // namespace engine
