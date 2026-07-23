#pragma once

#include <engine/ecs/Components.h>
#include <engine/gameplay/GameMode.h>
#include <engine/gameplay/Script.h>

class GoalTracker final : public engine::Script {
public:
    void OnUpdate(float) override {
        int remaining = 0;
        Registry()->view<engine::ecs::RuntimeName>().each(
            [&](engine::ecs::Entity, engine::ecs::RuntimeName& name) {
                if (name.value.rfind("Coin_", 0) == 0) ++remaining;
            });

        if (remaining > 0) sawCoin = true;
        if (sawCoin && remaining == 0 && engine::GameMode::Instance().IsPlaying()) {
            engine::GameMode::Instance().Win("All coins collected!");
        }
    }

private:
    bool sawCoin = false;
};
