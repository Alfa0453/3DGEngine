#include "WizardSceneApp.h"

#include <engine/graphics/Primitives.h>
#include <engine/graphics/ImageDecode.h>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <system_error>

using engine::ecs::Entity;
using engine::ecs::Transform;
using engine::ecs::MeshPBR;
using engine::ecs::PbrMaterial;
using engine::ecs::Light;

namespace {
engine::WindowProps MakeProps(const engine::Config& cfg) {
    engine::WindowProps p;
    p.title  = "Wizard — UE4 skinned mesh + external FBX animations";
    p.width  = cfg.GetInt("window.width", 1280);
    p.height = cfg.GetInt("window.height", 720);
    p.vsync  = cfg.GetBool("window.vsync", true);
    return p;
}
constexpr float kWalkAt = 0.4f, kRunAt = 3.0f;   // speed thresholds (u/s)
} // namespace

WizardSceneApp::WizardSceneApp(engine::Config& config)
    : engine::Application(MakeProps(config)), m_config(config) {}

std::string WizardSceneApp::Asset(const std::string& rel) const {
    const std::string beside = engine::ExecutableDir() + "/assets";
    std::error_code ec;
    const std::string root = std::filesystem::exists(beside, ec) ? beside : std::string(ASSETS_DIR);
    return root + "/" + rel;
}

void WizardSceneApp::OnInit() {
    m_renderer.Init();
    m_plane.emplace(engine::primitives::Plane(1.0f, 24.0f));
    m_cube.emplace(engine::primitives::Cube());

    // The mesh FBX carries the skeleton but no clips; attach the animation FBXs.
    m_model.emplace(engine::SkinnedModel::FromFile(Asset("models/WizardSM.FBX")));
    m_model->AddAnimationsFromFile(Asset("anims/Idle01Anim.FBX"),        false, "Idle");   // clip 0
    m_model->AddAnimationsFromFile(Asset("anims/WalkForwardAnim.FBX"),   true,  "Walk");   // clip 1 (root motion stripped)
    m_model->AddAnimationsFromFile(Asset("anims/BattleRunForwardAnim.FBX"), true, "Run");  // clip 2
    // Combat clips (played on the action layer, not the locomotion FSM).
    m_model->AddAnimationsFromFile(Asset("anims/Attack01Anim.FBX"), false, "Attack");        // clip 3
    m_model->AddAnimationsFromFile(Asset("anims/GetHitAnim.FBX"),   false, "Hit");           // clip 4
    m_model->AddAnimationsFromFile(Asset("anims/DieAnim.FBX"),      false, "Die");           // clip 5
    m_clipCount = static_cast<int>(m_model->AnimationCount());
    m_attackClip = (m_clipCount > 3) ? 3 : -1;
    m_dieClip    = (m_clipCount > 5) ? 5 : -1;

    // Weapon: load the staff (static mesh) and find the right-hand bone to attach to.
    try { m_staff.emplace(engine::Model::FromFile(Asset(m_staffName))); } catch (const std::exception&) {}
    for (const char* hb : { "hand_r", "RightHand", "hand_R", "Hand_R", "mixamorig:RightHand" }) {
        m_handIdx = m_model->GetSkeleton().Find(hb);
        if (m_handIdx >= 0) break;
    }
    m_hitClip    = (m_clipCount > 4) ? 4 : -1;

    // Upper-body mask so an attack plays over the legs while walking. Try the usual
    // UE4 spine bone names; fall back to full body if the rig names differ.
    for (const char* spine : { "spine_01", "spine_02", "Spine", "spine" }) {
        m_upperMask = engine::Animator::BuildMask(m_model->GetSkeleton(), spine);
        if (std::any_of(m_upperMask.begin(), m_upperMask.end(), [](float v){ return v > 0.0f; })) break;
    }
    // Fire the 'cast' event ~40% through the attack clip.
    if (m_attackClip >= 0) {
        const engine::Animation& atk = m_model->Animations()[static_cast<std::size_t>(m_attackClip)];
        const float tps = (atk.ticksPerSecond > 0.0f) ? atk.ticksPerSecond : 25.0f;
        const float len = (atk.duration > 0.0f) ? atk.duration / tps : 1.0f;
        m_castTime = len * 0.4f;
    }

    m_pbr.emplace(2048);
    m_skinned.emplace();
    m_sky.emplace();
    m_post.emplace(GetWindow().Width(), GetWindow().Height());
    m_text.emplace();
    m_sample = engine::DayNightCycle::At(0.37f);
    m_ibl.emplace(256);
    m_ibl->Generate([&](const glm::mat4& v, const glm::mat4& p) { m_sky->Draw(v, p, m_sample, false); });

    // Spatial audio: listener tracks the camera; sounds are positioned in the world.
    m_audio.emplace();
    m_audio->SetMasterVolume(0.9f);
    m_audio->SetAttenuation(1.0f, 28.0f, 1.5f);
    m_sndStep   = Asset("sounds/footstep.wav");
    m_sndWhoosh = Asset("sounds/whoosh.wav");
    m_sndImpact = Asset("sounds/impact.wav");
    m_audio->Preload(m_sndStep);
    m_audio->Preload(m_sndWhoosh);
    m_audio->Preload(m_sndImpact);

    // Spell VFX emitter (burst-only, additive HDR so it blooms).
    m_particleRenderer.emplace();
    m_spell.emitting = false;
    m_spell.cfg.rate = 0.0f;
    m_spell.cfg.maxParticles = 1500;
    m_spell.cfg.rate = 420.0f;                                    // dense trail while it flies
    m_spell.cfg.shape = engine::EmitShape::Sphere;
    m_spell.cfg.shapeRadius = 0.13f;                              // puffy fireball core
    m_spell.cfg.speedMin = 0.3f; m_spell.cfg.speedMax = 1.3f;     // slow drift -> trails behind the head
    m_spell.cfg.lifeMin = 0.30f; m_spell.cfg.lifeMax = 0.70f;     // short -> the trail fades fast
    m_spell.cfg.gravity = glm::vec3(0.0f, 1.2f, 0.0f); m_spell.cfg.drag = 2.2f;   // buoyant flames
    m_spell.cfg.startColor = glm::vec4(3.6f, 1.7f, 0.35f, 1.0f);  // bright orange/yellow (HDR)
    m_spell.cfg.endColor   = glm::vec4(1.3f, 0.08f, 0.0f, 0.0f);  // -> dark red, fade out
    m_spell.cfg.startSize = 0.28f; m_spell.cfg.endSize = 0.03f;
    m_spell.cfg.blend = engine::ParticleBlend::Additive;

    // UE4 assets are Z-up: the character's height is along model Z (world Y after
    // the upright fix applied in OnUpdate), so normalize from the Z extent and rest
    // the feet (min Z) on the ground.
    const float height = std::max(m_model->Max().z - m_model->Min().z, 1e-3f);
    m_scale   = 1.8f / height;
    m_groundY = -m_model->Min().z * m_scale;

    // Base-color palette atlas. The pack's textures aren't referenced by the FBX, so
    // load one explicitly. Decode the PNG directly (the engine's decoder) and build
    // the texture from raw pixels -- this bypasses Texture(path)'s extension sniffing
    // (which mis-routes here) and is guaranteed to use the PNG path. Try the
    // beside-exe copy first, then the source assets dir.
    const std::string rel = "textures/StandardPolyartMat_BaseColor.png";
    const std::string candidates[] = { Asset(rel), std::string(ASSETS_DIR) + "/" + rel };
    m_texStatus = "tex FAIL";
    for (const std::string& tp : candidates) {
        try {
            engine::image::Image im = engine::image::DecodePNG(tp);
            // PNG rows are top-first; GL wants bottom-left -> flip to match.
            const std::size_t rowB = static_cast<std::size_t>(im.width) * 4;
            for (int y = 0; y < im.height / 2; ++y)
                std::swap_ranges(im.rgba.begin() + static_cast<std::ptrdiff_t>(y * rowB),
                                 im.rgba.begin() + static_cast<std::ptrdiff_t>((y + 1) * rowB),
                                 im.rgba.begin() + static_cast<std::ptrdiff_t>((im.height - 1 - y) * rowB));
            m_tex.emplace(im.rgba.data(), im.width, im.height);
            m_texStatus = "tex OK";
            break;
        } catch (const std::exception& e) {
            m_texStatus = std::string("tex FAIL: ") + e.what();
            std::fprintf(stderr, "wizardscene: %s (path='%s')\n", m_texStatus.c_str(), tp.c_str());
        }
    }

    // Pillars (obstacles the enemy must navigate around) + auto-built navmesh.
    m_pillars = {
        { { 5.0f, 1.0f,  3.0f}, {1.0f, 2.0f, 1.0f} },
        { {-6.0f, 1.0f, -4.0f}, {1.0f, 2.0f, 1.0f} },
        { { 2.0f, 1.0f, -8.0f}, {1.2f, 2.0f, 1.2f} },
    };
    {
        engine::ai::NavBuildConfig nc;
        nc.boundsMin = glm::vec3(-18.0f, m_groundY, -18.0f);
        nc.boundsMax = glm::vec3( 18.0f, m_groundY,  18.0f);
        nc.cellSize = 1.0f; nc.agentRadius = 0.8f;
        m_nav = engine::ai::NavMeshBuilder::Build(nc, m_pillars);
    }
    m_enemyPos = glm::vec3(12.0f, m_groundY, 12.0f);

    BuildScene();
    GetWindow().SetCursorCaptured(false);
}

void WizardSceneApp::BuildScene() {
    // Ground.
    {
        Entity e = m_reg.Create();
        Transform t; t.scale = glm::vec3(40.0f, 1.0f, 40.0f);
        m_reg.Add<Transform>(e, t);
        PbrMaterial m; m.albedo = glm::vec3(0.32f, 0.34f, 0.37f); m.roughness = 0.95f;
        m_reg.Add<MeshPBR>(e, MeshPBR{&*m_plane, m});
    }

    // Wizard.
    m_pos = glm::vec3(0.0f, m_groundY, 0.0f);
    m_char = m_reg.Create();
    {
        Transform t; t.position = m_pos; t.scale = glm::vec3(m_scale);
        m_reg.Add<Transform>(m_char, t);

        engine::AnimatedModel am;
        am.model = &*m_model;
        const int n = m_clipCount;
        const int idle = 0;
        const int walk = std::min(1, std::max(0, n - 1));
        const int run  = std::min(2, std::max(0, n - 1));
        am.controller = engine::AnimationController::Locomotion(idle, walk, run, kWalkAt, kRunAt, 0.2f);
        if (m_tex) { am.albedoOverride = &*m_tex; am.tint = glm::vec3(1.0f); }
        else       { am.tint = glm::vec3(0.62f, 0.42f, 0.85f); }   // fallback if texture missing
        am.metallic = 0.0f; am.roughness = 0.55f;
        m_reg.Add<engine::AnimatedModel>(m_char, std::move(am));
        m_reg.Get<engine::AnimatedModel>(m_char).onEvent = [this](const std::string& name) {
            m_lastEvent = name;
            if (name == "cast") {
                m_flash = 0.35f;
                m_fireballActive = true;
                m_fireballPos = m_spellTip;
                m_fireballDir = glm::normalize(glm::vec3(std::cos(m_facing), 0.0f, std::sin(m_facing)));
                m_fireballDist = 0.0f;
                m_spell.emitting = true;
                m_spell.position = m_fireballPos;
                m_spell.Burst(40);                 // muzzle flash
                if (m_audio) m_audio->PlayAt(m_sndWhoosh, m_spellTip, 1.0f, 0.8f);
            }
        };
    }

    // Weapon entities: one MeshPBR per staff submesh; their transforms are driven
    // each frame from the hand bone (see OnUpdate). Positioned initially at origin.
    if (m_staff && m_handIdx >= 0) {
        for (const auto& sm : m_staff->SubMeshes()) {
            Entity e = m_reg.Create();
            m_reg.Add<Transform>(e, Transform{});
            PbrMaterial m; m.albedo = glm::vec3(1.0f); m.roughness = 0.55f; m.metallic = 0.0f;
            if (m_tex) m.albedoMap = &*m_tex;
            m_reg.Add<MeshPBR>(e, MeshPBR{ &sm.mesh, m });
            m_staffEnts.push_back(e);
        }
    }

    // Pillars (obstacles the enemy navmesh-routes around).
    for (const engine::ai::NavObstacle& o : m_pillars) {
        Entity e = m_reg.Create();
        Transform t; t.position = o.center; t.scale = o.halfExtents * 2.0f;
        m_reg.Add<Transform>(e, t);
        PbrMaterial m; m.albedo = glm::vec3(0.34f, 0.30f, 0.28f); m.roughness = 0.85f;
        m_reg.Add<MeshPBR>(e, MeshPBR{&*m_cube, m});
    }

    // Enemy: the same rig, tinted red, chasing the wizard across the navmesh.
    m_enemy = m_reg.Create();
    {
        Transform t; t.position = m_enemyPos; t.scale = glm::vec3(m_scale);
        m_reg.Add<Transform>(m_enemy, t);
        engine::AnimatedModel am;
        am.model = &*m_model;
        const int walk = std::min(1, std::max(0, m_clipCount - 1));
        const int run  = std::min(2, std::max(0, m_clipCount - 1));
        am.controller = engine::AnimationController::Locomotion(0, walk, run, kWalkAt, kRunAt, 0.2f);
        if (m_tex) { am.albedoOverride = &*m_tex; am.tint = glm::vec3(1.7f, 0.5f, 0.45f); }  // reddish
        else       { am.tint = glm::vec3(0.9f, 0.3f, 0.3f); }
        am.metallic = 0.0f; am.roughness = 0.55f;
        m_reg.Add<engine::AnimatedModel>(m_enemy, std::move(am));
    }

    // Sun.
    { Entity s = m_reg.Create(); m_reg.Add<Transform>(s, Transform{});
      Light l; l.type = Light::Type::Directional; l.direction = glm::vec3(-0.5f, -1.0f, -0.35f);
      l.color = glm::vec3(1.0f, 0.96f, 0.88f); l.intensity = 3.0f; m_reg.Add<Light>(s, l); }
}

void WizardSceneApp::OnUpdate(float dt) {
    engine::Window& w = GetWindow();
    m_dt = dt;
    if (dt > 0.0f) m_fps = m_fps * 0.92f + (1.0f / dt) * 0.08f;
    if (w.IsKeyPressed(GLFW_KEY_ESCAPE)) w.SetShouldClose(true);

    // Camera-relative movement; Shift to run.
    glm::vec3 fwd = glm::normalize(glm::vec3(std::cos(m_camYaw), 0.0f, std::sin(m_camYaw)));
    const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
    glm::vec3 wish(0.0f);
    if (w.IsKeyPressed(GLFW_KEY_W)) wish += fwd;
    if (w.IsKeyPressed(GLFW_KEY_S)) wish -= fwd;
    if (w.IsKeyPressed(GLFW_KEY_D)) wish += right;
    if (w.IsKeyPressed(GLFW_KEY_A)) wish -= right;
    const bool run = w.IsKeyPressed(GLFW_KEY_LEFT_SHIFT) || w.IsKeyPressed(GLFW_KEY_RIGHT_SHIFT);
    const float maxSpeed = run ? 4.0f : 1.6f;
    float target = 0.0f;
    if (glm::dot(wish, wish) > 1e-5f) {
        wish = glm::normalize(wish);
        m_pos += wish * (maxSpeed * dt);
        m_facing = std::atan2(wish.z, wish.x);
        target = maxSpeed;
    }
    m_speed += (target - m_speed) * std::min(1.0f, dt * 8.0f);
    m_pos.x = glm::clamp(m_pos.x, -18.0f, 18.0f);
    m_pos.z = glm::clamp(m_pos.z, -18.0f, 18.0f);
    m_pos.y = m_groundY;

    if (w.IsKeyPressed(GLFW_KEY_LEFT_BRACKET))  m_modelYawOffset -= 1.5f * dt;
    if (w.IsKeyPressed(GLFW_KEY_RIGHT_BRACKET)) m_modelYawOffset += 1.5f * dt;

    Transform& t = m_reg.Get<Transform>(m_char);
    t.position = m_pos;
    // Z-up (UE4) -> Y-up: stand the model up, then yaw it to face its travel dir.
    const glm::quat upright = glm::angleAxis(glm::radians(-90.0f), glm::vec3(1, 0, 0));
    const glm::quat yaw     = glm::angleAxis(-m_facing + m_modelYawOffset, glm::vec3(0, 1, 0));
    t.rotation = yaw * upright;
    m_reg.Get<engine::AnimatedModel>(m_char).controller.SetParameter(m_speed);

    const float rot = 1.6f * dt;
    if (w.IsKeyPressed(GLFW_KEY_LEFT))  m_camYaw   -= rot;
    if (w.IsKeyPressed(GLFW_KEY_RIGHT)) m_camYaw   += rot;
    if (w.IsKeyPressed(GLFW_KEY_UP))    m_camPitch -= rot;
    if (w.IsKeyPressed(GLFW_KEY_DOWN))  m_camPitch += rot;
    if (w.IsKeyPressed(GLFW_KEY_Z)) m_camDist -= 6.0f * dt;
    if (w.IsKeyPressed(GLFW_KEY_X)) m_camDist += 6.0f * dt;
    m_camPitch = glm::clamp(m_camPitch, 0.05f, 1.35f);
    m_camDist  = glm::clamp(m_camDist, 3.0f, 20.0f);

    // Combat input (edge-triggered): left mouse = attack (upper body over walk),
    // H = hit reaction (full body).
    engine::AnimatedModel& cam = m_reg.Get<engine::AnimatedModel>(m_char);
    const bool atkDown = w.IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
    const bool hitDown = w.IsKeyPressed(GLFW_KEY_H);
    if (atkDown && !m_prevAttack && m_attackClip >= 0)
        cam.PlayAction(m_attackClip, m_upperMask, { engine::AnimEvent{ -1, m_castTime, "cast" } }, 0.12f, 0.18f, 1.0f);
    if (hitDown && !m_prevHit && m_hitClip >= 0)
        cam.PlayAction(m_hitClip, {}, {}, 0.08f, 0.15f, 1.0f);   // empty mask = full body
    m_prevAttack = atkDown; m_prevHit = hitDown;

    // Emissive flash decay (fired by the 'cast' event).
    m_flash = std::max(0.0f, m_flash - dt);
    cam.emissive = glm::vec3(0.35f, 0.55f, 1.0f) * (m_flash / 0.35f) * 2.0f;   // cyan spell glow

    // Spatial audio: listener = the orbit camera; footsteps at the wizard's feet.
    if (m_audio) {
        const glm::vec3 ltgt = m_pos + glm::vec3(0.0f, 1.1f, 0.0f);
        const glm::vec3 lback(-std::cos(m_camYaw) * std::cos(m_camPitch),
                               std::sin(m_camPitch),
                              -std::sin(m_camYaw) * std::cos(m_camPitch));
        const glm::vec3 lcam = ltgt + lback * m_camDist;
        m_audio->SetListener(lcam, glm::normalize(ltgt - lcam));

        m_stepTimer -= dt;
        if (m_speed > 0.4f && m_stepTimer <= 0.0f) {
            m_stepFlip = !m_stepFlip;
            m_audio->PlayAt(m_sndStep, m_pos, m_stepFlip ? 1.05f : 0.93f, 0.6f);
            m_stepTimer = (m_speed > 3.0f) ? 0.28f : 0.45f;   // quicker steps when running
        }
    }

    // --- Enemy: navmesh-chase the wizard, or die + respawn -------------------
    {
        engine::AnimatedModel& eam = m_reg.Get<engine::AnimatedModel>(m_enemy);
        m_enemyFlash = std::max(0.0f, m_enemyFlash - dt);
        eam.emissive = glm::vec3(2.0f, 0.5f, 0.15f) * (m_enemyFlash / 0.4f);   // hit flash

        if (m_enemyDead) {
            m_deathTimer -= dt;
            if (m_deathTimer <= 0.0f) {   // respawn a fresh enemy at a far corner
                m_enemyDead = false; m_enemyHP = 100.0f; m_enemySpeed = 0.0f;
                m_enemyPos = glm::vec3((m_pos.x > 0 ? -13.0f : 13.0f), m_groundY, -13.0f);
                m_enemyPath.clear();
            }
        } else {
            m_repath -= dt;
            if (m_repath <= 0.0f) { m_nav.FindPath(m_enemyPos, m_pos, m_enemyPath); m_repath = 0.3f; }
            float target = 0.0f;
            const glm::vec3 flatToWiz(m_pos.x - m_enemyPos.x, 0.0f, m_pos.z - m_enemyPos.z);
            if (glm::length(flatToWiz) > 2.2f && m_enemyPath.size() >= 2) {
                glm::vec3 to = m_enemyPath[1] - m_enemyPos; to.y = 0.0f;
                const float d = glm::length(to);
                const glm::vec3 dir = (d > 1e-3f) ? to / d : glm::vec3(0, 0, 1);
                const float spd = 3.4f;
                m_enemyPos += dir * (spd * dt);
                m_enemyFacing = std::atan2(dir.z, dir.x);
                target = spd;
            }
            m_enemySpeed += (target - m_enemySpeed) * std::min(1.0f, dt * 8.0f);
            m_enemyPos.y = m_groundY;
            eam.controller.SetParameter(m_enemySpeed);
        }

        Transform& et = m_reg.Get<Transform>(m_enemy);
        et.position = m_enemyPos;
        const glm::quat up = glm::angleAxis(glm::radians(-90.0f), glm::vec3(1, 0, 0));
        et.rotation = glm::angleAxis(-m_enemyFacing + m_modelYawOffset, glm::vec3(0, 1, 0)) * up;
    }

    engine::UpdateAnimations(m_reg, dt);

    // --- Attach the staff to the right hand ---------------------------------
    // Grip tuning: I/K/J/L/U/O move it in the hand frame; T/G Y/B N/M rotate it;
    // , / . scale it. Watch the HUD for the values, then bake them as defaults.
    const float gp = 0.6f * dt, gr = 1.2f * dt;
    if (w.IsKeyPressed(GLFW_KEY_I)) m_gripPos.x += gp;
    if (w.IsKeyPressed(GLFW_KEY_K)) m_gripPos.x -= gp;
    if (w.IsKeyPressed(GLFW_KEY_J)) m_gripPos.y += gp;
    if (w.IsKeyPressed(GLFW_KEY_L)) m_gripPos.y -= gp;
    if (w.IsKeyPressed(GLFW_KEY_U)) m_gripPos.z += gp;
    if (w.IsKeyPressed(GLFW_KEY_O)) m_gripPos.z -= gp;
    if (w.IsKeyPressed(GLFW_KEY_T)) m_gripEuler.x += gr;
    if (w.IsKeyPressed(GLFW_KEY_G)) m_gripEuler.x -= gr;
    if (w.IsKeyPressed(GLFW_KEY_Y)) m_gripEuler.y += gr;
    if (w.IsKeyPressed(GLFW_KEY_B)) m_gripEuler.y -= gr;
    if (w.IsKeyPressed(GLFW_KEY_N)) m_gripEuler.z += gr;
    if (w.IsKeyPressed(GLFW_KEY_M)) m_gripEuler.z -= gr;
    if (w.IsKeyPressed(GLFW_KEY_COMMA))  m_gripScale = std::max(0.05f, m_gripScale - 0.5f * dt);
    if (w.IsKeyPressed(GLFW_KEY_PERIOD)) m_gripScale += 0.5f * dt;

    if (!m_staffEnts.empty() && m_handIdx >= 0) {
        const engine::AnimatedModel& am = m_reg.Get<engine::AnimatedModel>(m_char);
        if (static_cast<int>(am.pose.size()) > m_handIdx) {
            // Hand bone's transform in mesh space, then to world via the character's model.
            const glm::mat4 offInv = glm::inverse(m_model->GetSkeleton().bones[static_cast<std::size_t>(m_handIdx)].offset);
            const glm::mat4 handMesh = am.pose[static_cast<std::size_t>(m_handIdx)] * offInv;
            const glm::mat4 grip = glm::translate(glm::mat4(1.0f), m_gripPos)
                                 * glm::mat4_cast(glm::quat(m_gripEuler))
                                 * glm::scale(glm::mat4(1.0f), glm::vec3(m_gripScale));
            const glm::mat4 sw = m_reg.Get<Transform>(m_char).Model() * handMesh * grip;
            // Decompose sw (rigid + uniform scale) into the entities' Transforms.
            const glm::vec3 T(sw[3]);
            const glm::vec3 c0(sw[0]), c1(sw[1]), c2(sw[2]);
            const glm::vec3 S(glm::length(c0), glm::length(c1), glm::length(c2));
            const glm::mat3 R(c0 / std::max(S.x, 1e-8f), c1 / std::max(S.y, 1e-8f), c2 / std::max(S.z, 1e-8f));
            const glm::quat q = glm::quat_cast(R);
            for (Entity e : m_staffEnts) {
                Transform& t = m_reg.Get<Transform>(e);
                t.position = T; t.rotation = q; t.scale = S;
            }
            // Spell origin = the staff-bbox corner farthest from the grip (the tip),
            // so it works regardless of the staff's local axis.
            if (m_staff) {
                const glm::vec3 mn = m_staff->Min(), mx = m_staff->Max();
                float best = -1.0f; glm::vec3 tip = T;
                for (int i = 0; i < 8; ++i) {
                    const glm::vec3 c((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z);
                    const glm::vec3 wc(sw * glm::vec4(c, 1.0f));
                    const float d = glm::length(wc - T);
                    if (d > best) { best = d; tip = wc; }
                }
                m_spellTip = tip;
            }
        }
    }
    if (m_fireballActive) {
        const float step = m_fireballSpeed * dt;
        m_fireballPos  += m_fireballDir * step;
        m_fireballDist += step;
        m_spell.position = m_fireballPos;          // emit the trail at the moving head

        // Fireball vs enemy (sphere overlap at the chest).
        if (!m_enemyDead) {
            const glm::vec3 chest = m_enemyPos + glm::vec3(0.0f, 1.0f, 0.0f);
            if (glm::length(m_fireballPos - chest) < 1.2f) {
                m_fireballActive = false; m_spell.emitting = false; m_spell.Burst(80);
                if (m_audio) m_audio->PlayAt(m_sndImpact, m_fireballPos, 1.0f, 1.0f);
                m_enemyHP -= 34.0f;
                m_enemyFlash = 0.4f;
                engine::AnimatedModel& eam = m_reg.Get<engine::AnimatedModel>(m_enemy);
                if (m_enemyHP <= 0.0f) {
                    m_enemyDead = true;
                    if (m_dieClip >= 0) {
                        eam.PlayAction(m_dieClip, {}, {}, 0.1f, 0.02f, 1.0f);   // full-body death
                        const engine::Animation& da = m_model->Animations()[static_cast<std::size_t>(m_dieClip)];
                        const float tps = (da.ticksPerSecond > 0.0f) ? da.ticksPerSecond : 25.0f;
                        m_deathTimer = (da.duration > 0.0f) ? da.duration / tps + 0.1f : 2.2f;
                    } else { m_deathTimer = 2.2f; }
                } else if (m_hitClip >= 0) {
                    eam.PlayAction(m_hitClip, {}, {}, 0.06f, 0.15f, 1.0f);      // flinch
                }
            }
        }

        if (m_fireballDist >= m_fireballRange) {
            m_fireballActive = false;
            m_spell.emitting = false;
            m_spell.Burst(70);                     // impact pop where it lands
            if (m_audio) m_audio->PlayAt(m_sndImpact, m_fireballPos, 1.0f, 0.9f);
        }
    }
    m_spell.Update(dt);
}

void WizardSceneApp::OnRender() {
    engine::Window& w = GetWindow();
    const float aspect = w.AspectRatio();
    m_post->Resize(w.Width(), w.Height());

    const glm::vec3 target = m_pos + glm::vec3(0.0f, 1.1f, 0.0f);
    const glm::vec3 back(-std::cos(m_camYaw) * std::cos(m_camPitch),
                          std::sin(m_camPitch),
                         -std::sin(m_camYaw) * std::cos(m_camPitch));
    engine::Camera cam(target + back * m_camDist);
    cam.LookAt(target);

    const glm::vec3 sunDir = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.35f));
    const glm::vec3 sunColor = glm::vec3(1.0f, 0.96f, 0.88f) * 3.0f;
    const glm::vec3 ambient = m_sample.ambient;

    m_post->BeginScene();
    engine::PbrRenderer::Options opt;
    opt.ambient = ambient;
    opt.tonemap = false;
    opt.ibl     = &*m_ibl;
    opt.fog     = true;
    opt.fogColor = m_sample.horizon;
    opt.pointShadows = false;
    opt.shadowCasters = [&](const glm::mat4& lightVP) { m_skinned->DrawSceneDepth(m_reg, lightVP); };
    m_pbr->Render(m_reg, cam, aspect, w.Width(), w.Height(), opt);

    engine::SkinnedLighting lit;
    lit.sunDir = sunDir; lit.sunColor = sunColor; lit.ambient = ambient;
    lit.cascade = &m_pbr->Cascade();
    lit.ibl = &*m_ibl;
    lit.tonemap = false;
    lit.fog = true; lit.fogColor = m_sample.horizon;
    m_skinned->DrawScene(m_reg, cam, aspect, lit);

    // Spell particles into the HDR buffer -> bloom.
    if (m_particleRenderer) m_particleRenderer->Draw(m_spell, cam, aspect);

    m_sky->Draw(cam.ViewMatrix(), cam.ProjectionMatrix(aspect), m_sample, false);
    m_post->RenderToScreen(w.Width(), w.Height(), m_dt);

    const int ww = w.Width(), hh = w.Height();
    m_text->Begin(ww, hh);
    const engine::AnimatedModel& am = m_reg.Get<engine::AnimatedModel>(m_char);
    char buf[160];
    const char* actName = am.ActionPlaying()
        ? (am.action.clip == m_attackClip ? "Attack" : (am.action.clip == m_hitClip ? "Hit" : "Action"))
        : "-";
    std::snprintf(buf, sizeof(buf), "WIZARD   %-5s + action:%-6s   clips %d   %s   %.0f fps",
                  am.controller.CurrentStateName().c_str(), actName, m_clipCount, m_texStatus.c_str(), m_fps);
    m_text->Text(buf, 24.0f, 22.0f, 2.0f, glm::vec3(1.0f));
    char gbuf[128];
    std::snprintf(gbuf, sizeof(gbuf), "GRIP pos(%.2f,%.2f,%.2f) rot(%.2f,%.2f,%.2f) scl %.2f   %s",
                  m_gripPos.x, m_gripPos.y, m_gripPos.z, m_gripEuler.x, m_gripEuler.y, m_gripEuler.z,
                  m_gripScale, (m_handIdx >= 0 ? "hand_r ok" : "NO hand bone"));
    m_text->Text(gbuf, 24.0f, hh - 52.0f, 1.3f, glm::vec3(0.7f, 0.85f, 1.0f));
    char ebuf[96];
    if (m_enemyDead) std::snprintf(ebuf, sizeof(ebuf), "ENEMY: DOWN  (respawning...)");
    else             std::snprintf(ebuf, sizeof(ebuf), "ENEMY HP %d/100  (chasing)", (int)m_enemyHP);
    m_text->Text(ebuf, 24.0f, 46.0f, 1.6f, m_enemyDead ? glm::vec3(0.5f,0.9f,0.5f) : glm::vec3(1.0f,0.5f,0.4f));
    m_text->Text("WASD move  LMB fireball (aim by facing)  H hit-react  arrows orbit  Z/X zoom  Esc",
                 24.0f, hh - 30.0f, 1.3f, glm::vec3(0.75f));
    m_text->End();
}

void WizardSceneApp::OnShutdown() {
    m_config.Set("window.vsync", GetWindow().IsVSync());
    m_config.Save();
}
