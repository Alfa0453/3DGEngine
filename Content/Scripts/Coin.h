#pragma once

#include <engine/gameplay/GameMode.h>
#include <engine/gameplay/GameplayComponents.h>
#include <engine/gameplay/Script.h>

#include <algorithm>
#include <glm/geometric.hpp>
#include <string>

class Coin final : public engine::Script {
public:
    void OnCreate() override {
        // Called when the object enters Play mode.
    }

    void OnUpdate(float dt) override {
        (void)dt;
        if (m_collected) return;
        const engine::ecs::Entity target =
            FindObject(GetFieldString("target", "PlayerStart"));
        if (target == engine::ecs::kNull || !WasTriggerEntered(target)) return;

        m_collected = true;
        engine::GameMode::Instance().AddScore(GetFieldInt("score", 10));
        PlayAudio(true);
        BurstParticles(GetFieldInt("particleBurst", 16));
        Delay(0.12f, [this] { DestroySelf(); });
    }

private:
    bool m_collected = false;
};
