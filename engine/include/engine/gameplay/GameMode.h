#pragma once

#include "engine/ecs/Entity.h"

#include <string>

namespace engine {
namespace ecs { class Registry; }

// High-level run state of a game.
enum class GameState { Playing, Paused, GameOver, Victory };

// A lightweight, engine-wide game-rules layer: the run's state (playing / paused /
// win / lose), a score, and a play clock. It is a process-global singleton (like
// ScriptRegistry) so game SCRIPTS can drive it without any plumbing:
//
//     engine::GameMode::Instance().AddScore(100);
//     if (allEnemiesDead) engine::GameMode::Instance().Win("Level Clear!");
//
// The RUNTIME (the standalone player, or the editor's Play mode) owns the loop: it
// calls Update() once per fixed step, freezes the simulation while the state isn't
// Playing, and reads State()/Score()/Message() to drive the HUD and menus.
class GameMode {
public:
    static GameMode& Instance();

    // Advance the play clock and evaluate built-in rules while Playing. `player` is
    // the player entity (kNull if none) used by the lose-on-death rule. No-op unless
    // the state is Playing.
    void Update(ecs::Registry& registry, ecs::Entity player, float dt);

    // --- Control (safe to call from scripts) ------------------------------
    void Reset();                                       // -> Playing, score 0, clock 0
    void Win(const std::string& message = "You Win!");  // -> Victory
    void Lose(const std::string& message = "Game Over");// -> GameOver
    void Pause();                                       // Playing -> Paused
    void Resume();                                      // Paused  -> Playing
    void AddScore(int delta);
    void SetScore(int value);
    void SetMessage(const std::string& message) { m_message = message; }

    // --- Rules ------------------------------------------------------------
    // Built-in: end the run (GameOver) when the player entity's Health dies. Turn
    // off for games that handle death themselves (respawn, lives, etc.).
    bool loseOnPlayerDeath = true;

    // --- Queries ----------------------------------------------------------
    GameState State()   const { return m_state; }
    bool  IsPlaying()   const { return m_state == GameState::Playing; }
    bool  IsPaused()    const { return m_state == GameState::Paused; }
    bool  IsOver()      const { return m_state == GameState::GameOver || m_state == GameState::Victory; }
    bool  IsWon()       const { return m_state == GameState::Victory; }
    int   Score()       const { return m_score; }
    float Elapsed()     const { return m_elapsed; }
    const std::string&  Message() const { return m_message; }
    static const char*  StateName(GameState state);

private:
    GameMode() = default;

    GameState   m_state = GameState::Playing;
    int         m_score = 0;
    float       m_elapsed = 0.0f;
    std::string m_message;
};

} // namespace engine
