#include "GameplayUtilities/GameplayUtilities.h"

#include <engine/ai/BtScript.h>
#include <engine/ecs/Components.h>
#include <engine/gameplay/GameplayComponents.h>
#include <engine/gameplay/Script.h>
#include <engine/plugins/LinkedPlugin.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {

class GU_AutoRotate final : public engine::Script {
public:
    void OnUpdate(float dt) override {
        if (auto* transform = Transform()) {
            const float speed = GetFieldFloat("degreesPerSecond", 90.0f);
            gameplay_utilities::RotateWorldY(*transform, speed * dt);
        }
    }
};

class GU_HealthRegeneration final : public engine::Script {
public:
    void OnCreate() override {
        if (const auto* health = TryGet<engine::Health>()) m_previousHealth = health->hp;
    }

    void OnUpdate(float dt) override {
        auto* health = TryGet<engine::Health>();
        if (!health) return;

        if (health->hp < m_previousHealth) {
            m_delayRemaining = std::max(GetFieldFloat("delayAfterDamage", 2.0f), 0.0f);
        }
        m_delayRemaining = std::max(m_delayRemaining - dt, 0.0f);
        if (m_delayRemaining <= 0.0f && health->alive) {
            gameplay_utilities::Heal(*health,
                std::max(GetFieldFloat("healthPerSecond", 5.0f), 0.0f) * dt);
        }
        m_previousHealth = health->hp;
    }

private:
    float m_previousHealth = 0.0f;
    float m_delayRemaining = 0.0f;
};

class GU_DestroyAfter final : public engine::Script {
public:
    void OnCreate() override {
        const float lifetime = std::max(GetFieldFloat("lifetime", 3.0f), 0.0f);
        Delay(lifetime, [this] { DestroySelf(); });
    }
};

class GU_Bobbing final : public engine::Script {
public:
    void OnCreate() override {
        if (const auto* transform = Transform()) {
            m_baseHeight = transform->position.y;
            m_hasBaseHeight = true;
        }
    }

    void OnUpdate(float dt) override {
        auto* transform = Transform();
        if (!transform) return;
        if (!m_hasBaseHeight) {
            m_baseHeight = transform->position.y;
            m_hasBaseHeight = true;
        }
        m_phase += dt * std::max(GetFieldFloat("frequency", 1.0f), 0.0f);
        const float amplitude = std::max(GetFieldFloat("amplitude", 0.25f), 0.0f);
        transform->position.y = m_baseHeight + std::sin(m_phase * 6.28318530718f) * amplitude;
    }

private:
    float m_baseHeight = 0.0f;
    float m_phase = 0.0f;
    bool m_hasBaseHeight = false;
};

void RegisterGameplayUtilities(engine::ScriptRegistry& scripts,
                               engine::ai::BtScriptRegistry&) {
    scripts.Register("GU_AutoRotate", [] { return std::make_unique<GU_AutoRotate>(); });
    scripts.Register("GU_HealthRegeneration",
        [] { return std::make_unique<GU_HealthRegeneration>(); });
    scripts.Register("GU_DestroyAfter", [] { return std::make_unique<GU_DestroyAfter>(); });
    scripts.Register("GU_Bobbing", [] { return std::make_unique<GU_Bobbing>(); });
}

const bool kGameplayUtilitiesLinked = engine::plugins::RegisterLinkedPlugin(
    "com.3dgengine.gameplay_utilities", &RegisterGameplayUtilities);

} // namespace
