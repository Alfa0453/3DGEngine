#pragma once

#include <engine/core/Application.h>
#include <engine/core/Config.h>
#include <engine/core/Paths.h>
#include <engine/graphics/Renderer.h>
#include <engine/graphics/Mesh.h>
#include <engine/graphics/Camera.h>
#include <engine/graphics/TextRenderer.h>
#include <engine/graphics/PbrRenderer.h>
#include <engine/graphics/ProceduralSky.h>
#include <engine/graphics/PostProcess.h>
#include <engine/graphics/DayNightCycle.h>
#include <engine/graphics/IBL.h>
#include <engine/graphics/SkinnedModel.h>
#include <engine/graphics/Model.h>
#include <engine/graphics/ParticleSystem.h>
#include <engine/graphics/ParticleRenderer.h>
#include <engine/audio/AudioEngine.h>
#include <engine/ai/NavMeshBuilder.h>
#include <engine/graphics/Texture.h>
#include <engine/graphics/SkinnedRenderer.h>
#include <engine/ecs/Registry.h>
#include <engine/ecs/Components.h>
#include <engine/animation/AnimatedModel.h>
#include <engine/animation/Animator.h>

#include <glm/glm.hpp>

#include <optional>
#include <string>

// Loads the UE4-style Wizard: a skinned mesh FBX plus three per-clip animation
// FBX files (idle / walk / run) attached to its skeleton at load time. Walk it
// around a shadowed PBR ground with WASD (Shift to run); a locomotion state
// machine cross-blends idle -> walk -> run from the character's speed.
class WizardSceneApp : public engine::Application {
public:
    explicit WizardSceneApp(engine::Config& config);

protected:
    void OnInit()               override;
    void OnUpdate(float dt)     override;
    void OnRender()             override;
    void OnShutdown()           override;

private:
    std::string Asset(const std::string& rel) const;
    void BuildScene();

    engine::Config&  m_config;
    engine::Renderer m_renderer;

    std::optional<engine::Mesh>            m_plane, m_cube;
    std::optional<engine::PbrRenderer>     m_pbr;
    std::optional<engine::SkinnedRenderer> m_skinned;
    std::optional<engine::ProceduralSky>   m_sky;
    std::optional<engine::PostProcess>     m_post;
    std::optional<engine::TextRenderer>    m_text;
    std::optional<engine::IBL>             m_ibl;
    std::optional<engine::SkinnedModel>    m_model;
    std::optional<engine::Model>           m_staff;
    std::optional<engine::Texture>         m_tex;

    engine::ecs::Registry m_reg;
    engine::DayNightCycle::Sample m_sample{};
    engine::ecs::Entity m_char = engine::ecs::kNull;

    float m_scale = 1.0f, m_groundY = 0.0f;
    int   m_clipCount = 0;
    std::string m_texStatus = "no texture";
    // Combat action layer.
    int   m_attackClip = -1, m_hitClip = -1;
    float m_castTime = 0.35f;              // when the attack's 'cast' event fires
    std::vector<float> m_upperMask;        // upper-body bone mask for attacks
    float m_flash = 0.0f;                  // emissive flash timer (set on 'cast')
    bool  m_prevAttack = false, m_prevHit = false;
    std::string m_lastEvent;
    // Weapon attach (right-hand bone socket).
    std::string m_staffName = "staffs/Staff01SM.FBX";
    int   m_handIdx = -1;                         // "hand_r" bone index, -1 if not found
    std::vector<engine::ecs::Entity> m_staffEnts;
    glm::vec3 m_gripPos{0.0f};                   // grip offset in the hand's local frame
    glm::vec3 m_gripEuler{0.0f};                 // grip rotation (radians, XYZ)
    float     m_gripScale = 1.0f;
    // Spell VFX: a magic burst emitted at the staff tip on the 'cast' event.
    std::optional<engine::ParticleRenderer> m_particleRenderer;
    engine::ParticleEmitter m_spell;
    glm::vec3 m_spellTip{0.0f};
    // Fireball projectile: launched at the staff tip on 'cast', flies forward.
    bool      m_fireballActive = false;
    glm::vec3 m_fireballPos{0.0f};
    glm::vec3 m_fireballDir{0.0f, 0.0f, 1.0f};
    float     m_fireballDist  = 0.0f;
    float     m_fireballSpeed = 9.0f;    // units/sec
    float     m_fireballRange = 7.0f;    // travel distance before it bursts
    // Spatial audio (listener = camera; sounds positioned in the world).
    std::optional<engine::AudioEngine> m_audio;
    std::string m_sndStep, m_sndWhoosh, m_sndImpact;
    float m_stepTimer = 0.0f;
    bool  m_stepFlip = false;
    // Enemy + combat.
    engine::ecs::Entity m_enemy = engine::ecs::kNull;
    engine::ai::NavMesh m_nav;
    std::vector<engine::ai::NavObstacle> m_pillars;
    std::vector<glm::vec3> m_enemyPath;
    glm::vec3 m_enemyPos{0.0f};
    float m_enemyFacing = 0.0f, m_enemySpeed = 0.0f;
    float m_enemyHP = 100.0f, m_enemyFlash = 0.0f, m_repath = 0.0f, m_deathTimer = 0.0f;
    bool  m_enemyDead = false;
    int   m_dieClip = -1;

    glm::vec3 m_pos{0.0f};
    float m_facing = 0.0f;
    float m_speed  = 0.0f;
    float m_camYaw = 2.2f, m_camPitch = 0.35f, m_camDist = 7.0f;
    // UE4 mannequins face +Y in their own space; spin the mesh so "forward" reads
    // correctly. Tunable at runtime with [ and ] if it looks off.
    float m_modelYawOffset = 1.5708f;   // +90 deg: model forward is +Z; tune live with [ ]

    float m_dt = 0.016f, m_fps = 60.0f;
};
