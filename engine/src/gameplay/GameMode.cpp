#include "engine/gameplay/GameMode.h"

#include "engine/ecs/Registry.h"
#include "engine/gameplay/GameplayComponents.h"   // Health

#include <algorithm>

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
    m_timeDilation = 1.0f;
    ClearHitStop();
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

void GameMode::SetGlobalTimeDilation(float dilation) {
    m_timeDilation = std::clamp(dilation, 0.0f, 20.0f);
}

float GameMode::EffectiveTimeDilation() const {
    return m_timeDilation
        * (HitStopActive() ? m_hitStopDilation : 1.0f);
}

float GameMode::ScaleDelta(float unscaledDelta) const {
    return std::max(unscaledDelta, 0.0f) * EffectiveTimeDilation();
}

void GameMode::HitStop(float unscaledSeconds, float dilation) {
    if (unscaledSeconds <= 0.0f) {
        ClearHitStop();
        return;
    }
    const float requestedDilation = std::clamp(dilation, 0.0f, 1.0f);
    if (!HitStopActive()) {
        m_hitStopDilation = requestedDilation;
    } else {
        // A new, stronger impact should never weaken a hit stop already active.
        m_hitStopDilation = std::min(m_hitStopDilation, requestedDilation);
    }
    m_hitStopRemaining = std::max(m_hitStopRemaining, unscaledSeconds);
}

void GameMode::ClearHitStop() {
    m_hitStopRemaining = 0.0f;
    m_hitStopDilation = 1.0f;
}

void GameMode::UpdateUnscaledTime(float unscaledDelta) {
    if (!HitStopActive()) return;
    m_hitStopRemaining =
        std::max(m_hitStopRemaining - std::max(unscaledDelta, 0.0f), 0.0f);
    if (m_hitStopRemaining <= 0.0f) ClearHitStop();
}

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
