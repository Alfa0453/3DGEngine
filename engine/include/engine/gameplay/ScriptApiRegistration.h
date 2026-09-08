#pragma once

// Scripting Pass 2/5 (follow-up) -- seed the Script API Registry with descriptors for the REAL
// engine script API, so the metadata tooling (parity report, API docs, visual-scripting readiness,
// level validation) has actual data instead of an empty registry.
//
// This is a curated STARTER SEED, not the full ~200-function surface. Each entry's flags are derived
// by cross-referencing the two authoritative headers:
//   * NativeVisible  -- the method exists on Script (engine/gameplay/Script.h).
//   * LuaVisible     -- a matching Api* binding exists in LuaScript (engine/gameplay/LuaScript.h).
//   * StructuralCommand -- the call spawns/destroys entities (routed through deferred structural ops).
//   * MainThreadOnly -- touches renderer/audio/camera singletons that are not worker-safe.
// Expand this table as more of the surface is described; nothing here changes the functions
// themselves -- it only publishes metadata about them. Header-only, GL-free.

#include "engine/gameplay/ScriptReflection.h"

namespace engine {
namespace script {

// Register a representative, accurately-flagged slice of the engine script API + core event schemas.
inline void RegisterCoreScriptApi(ScriptApiRegistry& reg = ScriptApiRegistry::Get()) {
    const auto fn = [&](const char* name, const char* category, ScriptFieldType ret,
                        std::vector<ScriptParam> params, std::uint32_t flags,
                        const char* deprecation = "") {
        ScriptFunctionDescriptor d;
        d.name = name; d.category = category; d.returnType = ret; d.params = std::move(params);
        d.flags = flags; d.deprecationNote = deprecation;
        reg.RegisterFunction(std::move(d));
    };

    // ---- Physics queries (native + Lua; use the accelerated PhysicsWorld) --------------------
    fn("TraceLine", "Physics", ScriptFieldType::Bool,
       {{"start", ScriptFieldType::Vec3}, {"end", ScriptFieldType::Vec3}, {"layerMask", ScriptFieldType::Int}},
       SFF_NativeVisible | SFF_LuaVisible | SFF_VisualScriptVisible);
    fn("TraceSphere", "Physics", ScriptFieldType::Bool,
       {{"start", ScriptFieldType::Vec3}, {"end", ScriptFieldType::Vec3}, {"radius", ScriptFieldType::Float}},
       SFF_NativeVisible | SFF_LuaVisible | SFF_VisualScriptVisible);
    fn("TraceOverlapSphere", "Physics", ScriptFieldType::Entity,
       {{"center", ScriptFieldType::Vec3}, {"radius", ScriptFieldType::Float}},
       SFF_NativeVisible | SFF_LuaVisible | SFF_VisualScriptVisible);

    // ---- Transform / movement (native; tracked via Pass 1 helpers) ---------------------------
    fn("SetPosition", "Transform", ScriptFieldType::Bool, {{"position", ScriptFieldType::Vec3}},
       SFF_NativeVisible | SFF_VisualScriptVisible | SFF_FixedStepSafe);
    fn("Translate", "Transform", ScriptFieldType::Bool, {{"delta", ScriptFieldType::Vec3}},
       SFF_NativeVisible | SFF_VisualScriptVisible | SFF_FixedStepSafe);
    fn("SetVelocity", "Physics", ScriptFieldType::Bool, {{"velocity", ScriptFieldType::Vec3}},
       SFF_NativeVisible | SFF_VisualScriptVisible | SFF_FixedStepSafe);

    // ---- Structural (spawn/destroy -> deferred) ----------------------------------------------
    fn("SpawnEmpty", "World", ScriptFieldType::Entity,
       {{"name", ScriptFieldType::String}, {"position", ScriptFieldType::Vec3}},
       SFF_NativeVisible | SFF_LuaVisible | SFF_VisualScriptVisible | SFF_StructuralCommand);
    fn("Destroy", "World", ScriptFieldType::Bool, {{"entity", ScriptFieldType::Entity}},
       SFF_NativeVisible | SFF_LuaVisible | SFF_VisualScriptVisible | SFF_StructuralCommand);
    fn("DestroySelf", "World", ScriptFieldType::Bool, {},
       SFF_NativeVisible | SFF_LuaVisible | SFF_VisualScriptVisible | SFF_StructuralCommand);

    // ---- Events / cross-script ---------------------------------------------------------------
    fn("PublishEvent", "Events", ScriptFieldType::Bool,
       {{"name", ScriptFieldType::String}, {"target", ScriptFieldType::Entity}},
       SFF_NativeVisible | SFF_LuaVisible | SFF_VisualScriptVisible);
    fn("ListenForEvent", "Events", ScriptFieldType::Bool, {{"name", ScriptFieldType::String}},
       SFF_NativeVisible | SFF_LuaVisible);

    // ---- Audio / animation (main-thread; native + Lua) ---------------------------------------
    fn("PlayAudio", "Audio", ScriptFieldType::Bool, {{"restart", ScriptFieldType::Bool}},
       SFF_NativeVisible | SFF_LuaVisible | SFF_MainThreadOnly);
    fn("PlayActionClip", "Animation", ScriptFieldType::Bool, {{"actionName", ScriptFieldType::String}},
       SFF_NativeVisible | SFF_LuaVisible | SFF_MainThreadOnly);
    fn("ShakeCamera", "Camera", ScriptFieldType::Bool,
       {{"intensity", ScriptFieldType::Float}, {"duration", ScriptFieldType::Float}},
       SFF_NativeVisible | SFF_LuaVisible | SFF_MainThreadOnly);

    // ---- Timers (Lua uses the function-name variant) -----------------------------------------
    fn("SetTimerByFunctionName", "Timers", ScriptFieldType::Int,
       {{"functionName", ScriptFieldType::String}, {"seconds", ScriptFieldType::Float}, {"repeat", ScriptFieldType::Bool}},
       SFF_NativeVisible | SFF_LuaVisible);
    fn("BindTimerFunction", "Timers", ScriptFieldType::Bool, {{"functionName", ScriptFieldType::String}},
       SFF_NativeVisible);   // native-only: takes a std::function, no Lua binding
    fn("RequestSceneLoad", "World", ScriptFieldType::Bool, {{"scenePath", ScriptFieldType::String}},
       SFF_NativeVisible | SFF_LuaVisible | SFF_StructuralCommand);
    fn("LoadLocalization", "Localization", ScriptFieldType::Bool, {{"assetPath", ScriptFieldType::String}},
       SFF_NativeVisible | SFF_LuaVisible);
    fn("SetLanguage", "Localization", ScriptFieldType::Bool, {{"language", ScriptFieldType::String}},
       SFF_NativeVisible | SFF_LuaVisible);
    fn("Localize", "Localization", ScriptFieldType::String,
       {{"key", ScriptFieldType::String}, {"fallback", ScriptFieldType::String}},
       SFF_NativeVisible | SFF_LuaVisible | SFF_VisualScriptVisible);

    // ---- core engine event schemas (Phase 16) ------------------------------------------------
    reg.RegisterEvent({0, "Physics.TriggerEnter",
        {{"self", ScriptFieldType::Entity, true}, {"other", ScriptFieldType::Entity, true}}});
    reg.RegisterEvent({0, "Physics.TriggerExit",
        {{"self", ScriptFieldType::Entity, true}, {"other", ScriptFieldType::Entity, true}}});
    reg.RegisterEvent({0, "Animation.Event",
        {{"entity", ScriptFieldType::Entity, true}, {"name", ScriptFieldType::String, true}}});
    reg.RegisterEvent({0, "Ability.Event",
        {{"entity", ScriptFieldType::Entity, true}, {"name", ScriptFieldType::String, true}}});
}

} // namespace script
} // namespace engine
