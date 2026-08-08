#include "engine/gameplay/LuaScript.h"

#include "engine/core/Paths.h"
#include "engine/gameplay/GameMode.h"
#include "engine/gameplay/GameplayComponents.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <utility>
#include <vector>

namespace engine {
namespace {

constexpr const char* kInstanceRegistryKey = "3DGEngine.LuaScript.Instance";

std::filesystem::path ResolveSourcePath(const std::string& authored) {
    const std::filesystem::path source(authored);
    std::error_code ec;
    if (source.is_absolute() && std::filesystem::is_regular_file(source, ec)) {
        return source.lexically_normal();
    }

    const std::filesystem::path executable(ExecutableDir());
    const std::filesystem::path candidates[] = {
        source,
        std::filesystem::current_path(ec) / source,
        executable / source,
        executable.parent_path() / source
    };
    for (const std::filesystem::path& candidate : candidates) {
        ec.clear();
        if (std::filesystem::is_regular_file(candidate, ec)) {
            return std::filesystem::absolute(candidate, ec).lexically_normal();
        }
    }
    return source;
}

void PushVec3(lua_State* state, const glm::vec3& value) {
    lua_pushnumber(state, value.x);
    lua_pushnumber(state, value.y);
    lua_pushnumber(state, value.z);
}

ecs::Entity EntityArgument(lua_State* state, int index, ecs::Entity fallback) {
    if (lua_gettop(state) < index || lua_isnoneornil(state, index)) return fallback;
    return static_cast<ecs::Entity>(luaL_checkinteger(state, index));
}

const char* OptionalString(lua_State* state, int index, const char* fallback) {
    return luaL_optstring(state, index, fallback);
}

} // namespace

bool IsLuaScriptPath(const std::string& path) {
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".lua";
}

LuaScript::LuaScript(std::string sourcePath)
    : m_sourcePath(std::move(sourcePath)) {}

LuaScript::~LuaScript() {
    if (m_state) lua_close(m_state);
}

void LuaScript::Load() {
    if (m_loaded) return;
    if (m_sourcePath.empty()) {
        throw std::runtime_error("Lua script has no source path");
    }

    m_state = luaL_newstate();
    if (!m_state) throw std::runtime_error("could not create Lua state");

    // Game scripts receive useful deterministic libraries, but not io, os,
    // package, or debug. Scripts therefore cannot launch processes or browse
    // the player's filesystem.
    luaL_requiref(m_state, "_G", luaopen_base, 1);       lua_pop(m_state, 1);
    luaL_requiref(m_state, LUA_COLIBNAME, luaopen_coroutine, 1); lua_pop(m_state, 1);
    luaL_requiref(m_state, LUA_TABLIBNAME, luaopen_table, 1);    lua_pop(m_state, 1);
    luaL_requiref(m_state, LUA_STRLIBNAME, luaopen_string, 1);   lua_pop(m_state, 1);
    luaL_requiref(m_state, LUA_MATHLIBNAME, luaopen_math, 1);    lua_pop(m_state, 1);
    luaL_requiref(m_state, LUA_UTF8LIBNAME, luaopen_utf8, 1);    lua_pop(m_state, 1);

    lua_pushlightuserdata(m_state, this);
    lua_setfield(m_state, LUA_REGISTRYINDEX, kInstanceRegistryKey);
    RegisterEngineApi();

    const std::filesystem::path resolved = ResolveSourcePath(m_sourcePath);
    if (luaL_loadfile(m_state, resolved.string().c_str()) != LUA_OK) {
        const std::string error = lua_tostring(m_state, -1);
        lua_pop(m_state, 1);
        throw std::runtime_error("could not load '" + resolved.string() + "': " + error);
    }
    if (lua_pcall(m_state, 0, 0, 0) != LUA_OK) {
        const std::string error = lua_tostring(m_state, -1);
        lua_pop(m_state, 1);
        throw std::runtime_error("error evaluating '" + resolved.string() + "': " + error);
    }
    m_loaded = true;
}

void LuaScript::Call(const char* functionName) {
    Load();
    lua_getglobal(m_state, functionName);
    if (lua_isnil(m_state, -1)) {
        lua_pop(m_state, 1);
        return;
    }
    if (!lua_isfunction(m_state, -1)) {
        lua_pop(m_state, 1);
        throw std::runtime_error(std::string(functionName) + " must be a function");
    }
    if (lua_pcall(m_state, 0, 0, 0) != LUA_OK) {
        const std::string error = lua_tostring(m_state, -1);
        lua_pop(m_state, 1);
        throw std::runtime_error(std::string(functionName) + ": " + error);
    }
}

void LuaScript::Call(const char* functionName, float argument) {
    Load();
    lua_getglobal(m_state, functionName);
    if (lua_isnil(m_state, -1)) {
        lua_pop(m_state, 1);
        return;
    }
    if (!lua_isfunction(m_state, -1)) {
        lua_pop(m_state, 1);
        throw std::runtime_error(std::string(functionName) + " must be a function");
    }
    lua_pushnumber(m_state, argument);
    if (lua_pcall(m_state, 1, 0, 0) != LUA_OK) {
        const std::string error = lua_tostring(m_state, -1);
        lua_pop(m_state, 1);
        throw std::runtime_error(std::string(functionName) + ": " + error);
    }
}

bool LuaScript::HasTimerFunction(const std::string& functionName) const {
    if (functionName.empty()) return false;
    const_cast<LuaScript*>(this)->Load();
    lua_getglobal(m_state, functionName.c_str());
    const bool found = lua_isfunction(m_state, -1) != 0;
    lua_pop(m_state, 1);
    return found;
}

bool LuaScript::InvokeTimerFunction(const std::string& functionName) {
    if (!HasTimerFunction(functionName)) return false;
    Call(functionName.c_str());
    return true;
}

void LuaScript::OnCreate() { Call("OnCreate"); }
void LuaScript::OnUpdate(float dt) { Call("OnUpdate", dt); }
void LuaScript::OnFixedUpdate(float dt) { Call("OnFixedUpdate", dt); }
void LuaScript::OnDestroy() {
    if (m_loaded) Call("OnDestroy");
}

LuaScript* LuaScript::Current(lua_State* state) {
    lua_getfield(state, LUA_REGISTRYINDEX, kInstanceRegistryKey);
    auto* script = static_cast<LuaScript*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    if (!script) luaL_error(state, "Lua script engine context is unavailable");
    return script;
}

void LuaScript::RegisterEngineApi() {
    static const luaL_Reg functions[] = {
        {"Log", ApiLog},
        {"Self", ApiSelf},
        {"GetPosition", ApiGetPosition},
        {"SetPosition", ApiSetPosition},
        {"Translate", ApiTranslate},
        {"GetScale", ApiGetScale},
        {"SetScale", ApiSetScale},
        {"GetForward", ApiGetForward},
        {"FindObject", ApiFindObject},
        {"SplinePointCount", ApiSplinePointCount},
        {"GetSplinePoint", ApiGetSplinePoint},
        {"SetSplinePoint", ApiSetSplinePoint},
        {"GetSplinePointRotation", ApiGetSplinePointRotation},
        {"SetSplinePointRotation", ApiSetSplinePointRotation},
        {"AddSplinePoint", ApiAddSplinePoint},
        {"InsertSplinePoint", ApiInsertSplinePoint},
        {"RemoveSplinePoint", ApiRemoveSplinePoint},
        {"TranslateSpline", ApiTranslateSpline},
        {"SetSplineClosed", ApiSetSplineClosed},
        {"IsSplineClosed", ApiIsSplineClosed},
        {"SplinePositionAt", ApiSplinePositionAt},
        {"SplineTangentAt", ApiSplineTangentAt},
        {"Destroy", ApiDestroy},
        {"DestroySelf", ApiDestroySelf},
        {"SpawnEmpty", ApiSpawnEmpty},
        {"SpawnFromObject", ApiSpawnFromObject},
        {"ConfigureProjectile", ApiConfigureProjectile},
        {"SocketPosition", ApiSocketPosition},
        {"TraceLine", ApiTraceLine},
        {"TraceSphere", ApiTraceSphere},
        {"TraceOverlapSphere", ApiTraceOverlapSphere},
        {"IsKeyDown", ApiKeyDown},
        {"WasKeyPressed", ApiKeyPressed},
        {"IsMouseButtonDown", ApiMouseDown},
        {"WasMouseButtonPressed", ApiMousePressed},
        {"MouseDelta", ApiMouseDelta},
        {"WasAnimationEvent", ApiWasAnimationEvent},
        {"GetFieldFloat", ApiFieldFloat},
        {"GetFieldInt", ApiFieldInt},
        {"GetFieldBool", ApiFieldBool},
        {"GetFieldString", ApiFieldString},
        {"GetFieldVec3", ApiFieldVec3},
        {"GetFieldColor", ApiFieldColor},
        {"GetFieldEntity", ApiFieldEntity},
        {"GetFieldAsset", ApiFieldAsset},
        {"PlayActionClip", ApiPlayActionClip},
        {"SetAnimationParameter", ApiSetAnimFloat},
        {"SetAnimationBool", ApiSetAnimBool},
        {"SetAnimationTrigger", ApiSetAnimTrigger},
        {"IsAnimationActionPlaying", ApiIsActionPlaying},
        {"PlayAudio", ApiPlayAudio},
        {"PlayAudioCue", ApiPlayAudioCue},
        {"StopAudio", ApiStopAudio},
        {"PlayParticles", ApiPlayParticles},
        {"StopParticles", ApiStopParticles},
        {"BurstParticles", ApiBurstParticles},
        {"ShakeCamera", ApiShakeCamera},
        {"GetHealth", ApiGetHealth},
        {"GetMaxHealth", ApiGetMaxHealth},
        {"Damage", ApiDamage},
        {"Heal", ApiHeal},
        {"AddScore", ApiAddScore},
        {"ResetGame", ApiResetGame},
        {"GetScore", ApiGetScore},
        {"GetElapsed", ApiGetElapsed},
        {"GetGameState", ApiGetGameState},
        {"Win", ApiWin},
        {"Lose", ApiLose},
        {"RequestSceneLoad", ApiRequestSceneLoad},
        {"RequestLevelLoad", ApiRequestLevelLoad},
        {"RequestLevelUnload", ApiRequestLevelUnload},
        {"SaveValue", ApiSaveValue},
        {"LoadValue", ApiLoadValue},
        {"LoadAdaptiveMusic", ApiLoadAdaptiveMusic},
        {"SetMusicState", ApiSetMusicState},
        {"PlayCameraSequence", ApiPlayCameraSequence},
        {"IsCameraSequencePlaying", ApiIsCameraSequencePlaying},
        {"WasCameraSequenceEvent", ApiWasCameraSequenceEvent},
        {"SetTimerByFunctionName", ApiSetTimerByFunctionName},
        {"ClearTimer", ApiClearTimer},
        {"ClearTimerByFunctionName", ApiClearTimerByFunctionName},
        {"IsTimerActive", ApiIsTimerActive},
        {"SetGlobalTimeDilation", ApiSetGlobalTimeDilation},
        {"GetGlobalTimeDilation", ApiGetGlobalTimeDilation},
        {"GetEffectiveTimeDilation", ApiGetEffectiveTimeDilation},
        {"HitStop", ApiHitStop},
        {"IsHitStopActive", ApiIsHitStopActive},
        {nullptr, nullptr}
    };
    luaL_newlib(m_state, functions);
    lua_setglobal(m_state, "Engine");
}

int LuaScript::ApiLog(lua_State* state) {
    std::fprintf(stdout, "[Lua] %s\n", luaL_checkstring(state, 1));
    return 0;
}

int LuaScript::ApiSelf(lua_State* state) {
    lua_pushinteger(state, static_cast<lua_Integer>(Current(state)->Self()));
    return 1;
}

int LuaScript::ApiGetPosition(lua_State* state) {
    LuaScript* script = Current(state);
    const ecs::Entity entity = EntityArgument(state, 1, script->Self());
    const ecs::Transform* transform = script->TryGet<ecs::Transform>(entity);
    if (!transform) return 0;
    PushVec3(state, transform->position);
    return 3;
}

int LuaScript::ApiSetPosition(lua_State* state) {
    LuaScript* script = Current(state);
    ecs::Entity entity = script->Self();
    int value = 1;
    if (lua_gettop(state) >= 4) {
        entity = EntityArgument(state, 1, entity);
        value = 2;
    }
    if (ecs::Transform* transform = script->TryGet<ecs::Transform>(entity)) {
        transform->position = glm::vec3(
            static_cast<float>(luaL_checknumber(state, value)),
            static_cast<float>(luaL_checknumber(state, value + 1)),
            static_cast<float>(luaL_checknumber(state, value + 2)));
        lua_pushboolean(state, 1);
    } else {
        lua_pushboolean(state, 0);
    }
    return 1;
}

int LuaScript::ApiTranslate(lua_State* state) {
    LuaScript* script = Current(state);
    if (ecs::Transform* transform = script->Transform()) {
        transform->position += glm::vec3(
            static_cast<float>(luaL_checknumber(state, 1)),
            static_cast<float>(luaL_checknumber(state, 2)),
            static_cast<float>(luaL_checknumber(state, 3)));
        lua_pushboolean(state, 1);
    } else lua_pushboolean(state, 0);
    return 1;
}

int LuaScript::ApiGetScale(lua_State* state) {
    LuaScript* script = Current(state);
    const ecs::Entity entity = EntityArgument(state, 1, script->Self());
    const ecs::Transform* transform = script->TryGet<ecs::Transform>(entity);
    if (!transform) return 0;
    PushVec3(state, transform->scale);
    return 3;
}

int LuaScript::ApiSetScale(lua_State* state) {
    LuaScript* script = Current(state);
    ecs::Entity entity = script->Self();
    int value = 1;
    if (lua_gettop(state) >= 4) {
        entity = EntityArgument(state, 1, entity);
        value = 2;
    }
    if (ecs::Transform* transform = script->TryGet<ecs::Transform>(entity)) {
        transform->scale = glm::vec3(
            static_cast<float>(luaL_checknumber(state, value)),
            static_cast<float>(luaL_checknumber(state, value + 1)),
            static_cast<float>(luaL_checknumber(state, value + 2)));
        lua_pushboolean(state, 1);
    } else lua_pushboolean(state, 0);
    return 1;
}

int LuaScript::ApiGetForward(lua_State* state) {
    LuaScript* script = Current(state);
    const ecs::Entity entity = EntityArgument(state, 1, script->Self());
    const ecs::Transform* transform = script->TryGet<ecs::Transform>(entity);
    if (!transform) return 0;
    PushVec3(state, glm::normalize(
        transform->rotation * glm::vec3(0.0f, 0.0f, 1.0f)));
    return 3;
}

int LuaScript::ApiTraceLine(lua_State* state) {
    LuaScript* script = Current(state);
    const glm::vec3 start(
        static_cast<float>(luaL_checknumber(state, 1)),
        static_cast<float>(luaL_checknumber(state, 2)),
        static_cast<float>(luaL_checknumber(state, 3)));
    const glm::vec3 end(
        static_cast<float>(luaL_checknumber(state, 4)),
        static_cast<float>(luaL_checknumber(state, 5)),
        static_cast<float>(luaL_checknumber(state, 6)));
    const std::uint32_t mask = static_cast<std::uint32_t>(
        luaL_optinteger(state, 7, static_cast<lua_Integer>(0xFFFFFFFFu)));
    const RaycastHit hit = script->TraceLine(start, end, mask);
    lua_pushboolean(state, hit.hit);
    if (hit.hit) lua_pushinteger(state, static_cast<lua_Integer>(hit.entity));
    else lua_pushnil(state);
    lua_pushnumber(state, hit.distance);
    PushVec3(state, hit.point);
    return 6;
}

int LuaScript::ApiTraceSphere(lua_State* state) {
    LuaScript* script = Current(state);
    const glm::vec3 start(
        static_cast<float>(luaL_checknumber(state, 1)),
        static_cast<float>(luaL_checknumber(state, 2)),
        static_cast<float>(luaL_checknumber(state, 3)));
    const glm::vec3 end(
        static_cast<float>(luaL_checknumber(state, 4)),
        static_cast<float>(luaL_checknumber(state, 5)),
        static_cast<float>(luaL_checknumber(state, 6)));
    const float radius = static_cast<float>(luaL_checknumber(state, 7));
    const std::uint32_t mask = static_cast<std::uint32_t>(
        luaL_optinteger(state, 8, static_cast<lua_Integer>(0xFFFFFFFFu)));
    const RaycastHit hit = script->TraceSphere(start, end, radius, mask);
    lua_pushboolean(state, hit.hit);
    if (hit.hit) lua_pushinteger(state, static_cast<lua_Integer>(hit.entity));
    else lua_pushnil(state);
    lua_pushnumber(state, hit.distance);
    PushVec3(state, hit.point);
    return 6;
}

int LuaScript::ApiTraceOverlapSphere(lua_State* state) {
    LuaScript* script = Current(state);
    const glm::vec3 center(
        static_cast<float>(luaL_checknumber(state, 1)),
        static_cast<float>(luaL_checknumber(state, 2)),
        static_cast<float>(luaL_checknumber(state, 3)));
    const float radius = static_cast<float>(luaL_checknumber(state, 4));
    const std::uint32_t mask = static_cast<std::uint32_t>(
        luaL_optinteger(state, 5, static_cast<lua_Integer>(0xFFFFFFFFu)));
    const std::vector<ecs::Entity> entities =
        script->TraceOverlapSphere(center, radius, mask);
    lua_createtable(state, static_cast<int>(entities.size()), 0);
    int index = 1;
    for (const ecs::Entity entity : entities) {
        lua_pushinteger(state, static_cast<lua_Integer>(entity));
        lua_rawseti(state, -2, index++);
    }
    return 1;
}

int LuaScript::ApiFindObject(lua_State* state) {
    const ecs::Entity entity = Current(state)->FindObject(luaL_checkstring(state, 1));
    if (entity == ecs::kNull) lua_pushnil(state);
    else lua_pushinteger(state, static_cast<lua_Integer>(entity));
    return 1;
}

int LuaScript::ApiSplinePointCount(lua_State* state) {
    lua_pushinteger(state, Current(state)->SplinePointCount(
        static_cast<ecs::Entity>(luaL_checkinteger(state, 1))));
    return 1;
}

int LuaScript::ApiGetSplinePoint(lua_State* state) {
    LuaScript* script = Current(state);
    const ecs::Entity entity = static_cast<ecs::Entity>(luaL_checkinteger(state, 1));
    const int index = static_cast<int>(luaL_checkinteger(state, 2)) - 1;
    if (index < 0 || index >= script->SplinePointCount(entity)) return 0;
    PushVec3(state, script->GetSplinePoint(entity, index));
    return 3;
}

int LuaScript::ApiSetSplinePoint(lua_State* state) {
    lua_pushboolean(state, Current(state)->SetSplinePoint(
        static_cast<ecs::Entity>(luaL_checkinteger(state, 1)),
        static_cast<int>(luaL_checkinteger(state, 2)) - 1,
        glm::vec3(static_cast<float>(luaL_checknumber(state, 3)),
                  static_cast<float>(luaL_checknumber(state, 4)),
                  static_cast<float>(luaL_checknumber(state, 5)))));
    return 1;
}

int LuaScript::ApiGetSplinePointRotation(lua_State* state) {
    LuaScript* script = Current(state);
    const ecs::Entity entity = static_cast<ecs::Entity>(luaL_checkinteger(state, 1));
    const int index = static_cast<int>(luaL_checkinteger(state, 2)) - 1;
    if (index < 0 || index >= script->SplinePointCount(entity)) return 0;
    PushVec3(state, script->GetSplinePointRotation(entity, index));
    return 3;
}

int LuaScript::ApiSetSplinePointRotation(lua_State* state) {
    lua_pushboolean(state, Current(state)->SetSplinePointRotation(
        static_cast<ecs::Entity>(luaL_checkinteger(state, 1)),
        static_cast<int>(luaL_checkinteger(state, 2)) - 1,
        glm::vec3(static_cast<float>(luaL_checknumber(state, 3)),
                  static_cast<float>(luaL_checknumber(state, 4)),
                  static_cast<float>(luaL_checknumber(state, 5)))));
    return 1;
}

int LuaScript::ApiAddSplinePoint(lua_State* state) {
    const glm::vec3 point(static_cast<float>(luaL_checknumber(state, 2)),
                          static_cast<float>(luaL_checknumber(state, 3)),
                          static_cast<float>(luaL_checknumber(state, 4)));
    const glm::vec3 rotation(static_cast<float>(luaL_optnumber(state, 5, 0.0)),
                             static_cast<float>(luaL_optnumber(state, 6, 0.0)),
                             static_cast<float>(luaL_optnumber(state, 7, 0.0)));
    const int index = Current(state)->AddSplinePoint(
        static_cast<ecs::Entity>(luaL_checkinteger(state, 1)), point, rotation);
    if (index < 0) lua_pushnil(state);
    else lua_pushinteger(state, index + 1);
    return 1;
}

int LuaScript::ApiInsertSplinePoint(lua_State* state) {
    const glm::vec3 point(static_cast<float>(luaL_checknumber(state, 3)),
                          static_cast<float>(luaL_checknumber(state, 4)),
                          static_cast<float>(luaL_checknumber(state, 5)));
    const glm::vec3 rotation(static_cast<float>(luaL_optnumber(state, 6, 0.0)),
                             static_cast<float>(luaL_optnumber(state, 7, 0.0)),
                             static_cast<float>(luaL_optnumber(state, 8, 0.0)));
    lua_pushboolean(state, Current(state)->InsertSplinePoint(
        static_cast<ecs::Entity>(luaL_checkinteger(state, 1)),
        static_cast<int>(luaL_checkinteger(state, 2)) - 1, point, rotation));
    return 1;
}

int LuaScript::ApiRemoveSplinePoint(lua_State* state) {
    lua_pushboolean(state, Current(state)->RemoveSplinePoint(
        static_cast<ecs::Entity>(luaL_checkinteger(state, 1)),
        static_cast<int>(luaL_checkinteger(state, 2)) - 1));
    return 1;
}

int LuaScript::ApiTranslateSpline(lua_State* state) {
    lua_pushboolean(state, Current(state)->TranslateSpline(
        static_cast<ecs::Entity>(luaL_checkinteger(state, 1)),
        glm::vec3(static_cast<float>(luaL_checknumber(state, 2)),
                  static_cast<float>(luaL_checknumber(state, 3)),
                  static_cast<float>(luaL_checknumber(state, 4)))));
    return 1;
}

int LuaScript::ApiSetSplineClosed(lua_State* state) {
    lua_pushboolean(state, Current(state)->SetSplineClosed(
        static_cast<ecs::Entity>(luaL_checkinteger(state, 1)), lua_toboolean(state, 2) != 0));
    return 1;
}

int LuaScript::ApiIsSplineClosed(lua_State* state) {
    lua_pushboolean(state, Current(state)->IsSplineClosed(
        static_cast<ecs::Entity>(luaL_checkinteger(state, 1))));
    return 1;
}

int LuaScript::ApiSplinePositionAt(lua_State* state) {
    PushVec3(state, Current(state)->SplinePositionAt(
        static_cast<ecs::Entity>(luaL_checkinteger(state, 1)),
        static_cast<float>(luaL_checknumber(state, 2))));
    return 3;
}

int LuaScript::ApiSplineTangentAt(lua_State* state) {
    PushVec3(state, Current(state)->SplineTangentAt(
        static_cast<ecs::Entity>(luaL_checkinteger(state, 1)),
        static_cast<float>(luaL_checknumber(state, 2))));
    return 3;
}

int LuaScript::ApiDestroy(lua_State* state) {
    Current(state)->Destroy(static_cast<ecs::Entity>(luaL_checkinteger(state, 1)));
    return 0;
}

int LuaScript::ApiDestroySelf(lua_State* state) {
    Current(state)->DestroySelf();
    return 0;
}

int LuaScript::ApiSpawnEmpty(lua_State* state) {
    const char* name = luaL_checkstring(state, 1);
    const glm::vec3 position(
        static_cast<float>(luaL_optnumber(state, 2, 0.0)),
        static_cast<float>(luaL_optnumber(state, 3, 0.0)),
        static_cast<float>(luaL_optnumber(state, 4, 0.0)));
    lua_pushinteger(state,
        static_cast<lua_Integer>(Current(state)->SpawnEmpty(name, position)));
    return 1;
}

int LuaScript::ApiSpawnFromObject(lua_State* state) {
    const char* prototype = luaL_checkstring(state, 1);
    const glm::vec3 position(
        static_cast<float>(luaL_optnumber(state, 2, 0.0)),
        static_cast<float>(luaL_optnumber(state, 3, 0.0)),
        static_cast<float>(luaL_optnumber(state, 4, 0.0)));
    const ecs::Entity entity =
        Current(state)->SpawnFromObject(prototype, position);
    if (entity == ecs::kNull) lua_pushnil(state);
    else lua_pushinteger(state, static_cast<lua_Integer>(entity));
    return 1;
}

int LuaScript::ApiConfigureProjectile(lua_State* state) {
    LuaScript* script = Current(state);
    const ecs::Entity entity =
        static_cast<ecs::Entity>(luaL_checkinteger(state, 1));
    Projectile* projectile = script->TryGet<Projectile>(entity);
    if (!projectile) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const glm::vec3 requestedDirection(
        static_cast<float>(luaL_checknumber(state, 2)),
        static_cast<float>(luaL_checknumber(state, 3)),
        static_cast<float>(luaL_checknumber(state, 4)));
    const float directionLength = glm::length(requestedDirection);
    if (directionLength <= 0.0001f) {
        lua_pushboolean(state, 0);
        return 1;
    }
    projectile->dir = requestedDirection / directionLength;
    projectile->speed = static_cast<float>(luaL_optnumber(state, 5, 10.0));
    projectile->damage = static_cast<float>(luaL_optnumber(state, 6, 25.0));
    projectile->range = static_cast<float>(luaL_optnumber(state, 7, 12.0));
    projectile->radius = static_cast<float>(luaL_optnumber(state, 8, 0.12));
    projectile->owner = EntityArgument(state, 9, script->Self());
    projectile->traveled = 0.0f;
    lua_pushboolean(state, 1);
    return 1;
}

int LuaScript::ApiSocketPosition(lua_State* state) {
    glm::vec3 position(0.0f);
    if (!Current(state)->SocketPosition(luaL_checkstring(state, 1), &position)) {
        return 0;
    }
    PushVec3(state, position);
    return 3;
}

int LuaScript::ApiKeyDown(lua_State* state) {
    lua_pushboolean(state, Current(state)->IsKeyDown(
        static_cast<int>(luaL_checkinteger(state, 1))));
    return 1;
}

int LuaScript::ApiKeyPressed(lua_State* state) {
    lua_pushboolean(state, Current(state)->WasKeyPressed(
        static_cast<int>(luaL_checkinteger(state, 1))));
    return 1;
}

int LuaScript::ApiMouseDown(lua_State* state) {
    lua_pushboolean(state, Current(state)->IsMouseButtonDown(
        static_cast<int>(luaL_checkinteger(state, 1))));
    return 1;
}

int LuaScript::ApiMousePressed(lua_State* state) {
    lua_pushboolean(state, Current(state)->WasMouseButtonPressed(
        static_cast<int>(luaL_checkinteger(state, 1))));
    return 1;
}

int LuaScript::ApiMouseDelta(lua_State* state) {
    LuaScript* script = Current(state);
    lua_pushnumber(state, script->MouseDeltaX());
    lua_pushnumber(state, script->MouseDeltaY());
    return 2;
}

int LuaScript::ApiWasAnimationEvent(lua_State* state) {
    lua_pushboolean(state, Current(state)->WasAnimationEvent(
        luaL_checkstring(state, 1)));
    return 1;
}

int LuaScript::ApiFieldFloat(lua_State* state) {
    lua_pushnumber(state, Current(state)->GetFieldFloat(
        luaL_checkstring(state, 1), static_cast<float>(luaL_optnumber(state, 2, 0.0))));
    return 1;
}

int LuaScript::ApiFieldInt(lua_State* state) {
    lua_pushinteger(state, Current(state)->GetFieldInt(
        luaL_checkstring(state, 1), static_cast<int>(luaL_optinteger(state, 2, 0))));
    return 1;
}

int LuaScript::ApiFieldBool(lua_State* state) {
    lua_pushboolean(state, Current(state)->GetFieldBool(
        luaL_checkstring(state, 1), lua_toboolean(state, 2) != 0));
    return 1;
}

int LuaScript::ApiFieldString(lua_State* state) {
    const std::string value = Current(state)->GetFieldString(
        luaL_checkstring(state, 1), OptionalString(state, 2, ""));
    lua_pushlstring(state, value.data(), value.size());
    return 1;
}

int LuaScript::ApiFieldVec3(lua_State* state) {
    const glm::vec3 fallback(
        static_cast<float>(luaL_optnumber(state, 2, 0.0)),
        static_cast<float>(luaL_optnumber(state, 3, 0.0)),
        static_cast<float>(luaL_optnumber(state, 4, 0.0)));
    PushVec3(state, Current(state)->GetFieldVec3(luaL_checkstring(state, 1), fallback));
    return 3;
}

int LuaScript::ApiFieldColor(lua_State* state) {
    const glm::vec3 fallback(
        static_cast<float>(luaL_optnumber(state, 2, 1.0)),
        static_cast<float>(luaL_optnumber(state, 3, 1.0)),
        static_cast<float>(luaL_optnumber(state, 4, 1.0)));
    PushVec3(state, Current(state)->GetFieldColor(
        luaL_checkstring(state, 1), fallback));
    return 3;
}

int LuaScript::ApiFieldEntity(lua_State* state) {
    const ecs::Entity entity = Current(state)->GetFieldEntity(luaL_checkstring(state, 1));
    if (entity == ecs::kNull) lua_pushnil(state);
    else lua_pushinteger(state, static_cast<lua_Integer>(entity));
    return 1;
}

int LuaScript::ApiFieldAsset(lua_State* state) {
    const std::string value = Current(state)->GetFieldAsset(
        luaL_checkstring(state, 1), OptionalString(state, 2, ""));
    lua_pushlstring(state, value.data(), value.size());
    return 1;
}

int LuaScript::ApiPlayActionClip(lua_State* state) {
    lua_pushboolean(state, Current(state)->PlayActionClip(luaL_checkstring(state, 1)));
    return 1;
}

int LuaScript::ApiSetAnimFloat(lua_State* state) {
    lua_pushboolean(state, Current(state)->SetAnimationParameter(
        luaL_checkstring(state, 1), static_cast<float>(luaL_checknumber(state, 2))));
    return 1;
}

int LuaScript::ApiSetAnimBool(lua_State* state) {
    lua_pushboolean(state, Current(state)->SetAnimationBool(
        luaL_checkstring(state, 1), lua_toboolean(state, 2) != 0));
    return 1;
}

int LuaScript::ApiSetAnimTrigger(lua_State* state) {
    lua_pushboolean(state, Current(state)->SetAnimationTrigger(luaL_checkstring(state, 1)));
    return 1;
}

int LuaScript::ApiIsActionPlaying(lua_State* state) {
    lua_pushboolean(state, Current(state)->IsAnimationActionPlaying());
    return 1;
}

int LuaScript::ApiPlayAudio(lua_State* state) {
    LuaScript* script = Current(state);
    const bool restart = lua_toboolean(state, 1) != 0;
    lua_pushboolean(state, script->PlayAudio(restart));
    return 1;
}

int LuaScript::ApiPlayAudioCue(lua_State* state) {
    lua_pushboolean(state, Current(state)->PlayAudioCue(
        luaL_checkstring(state, 1),
        lua_gettop(state) < 2 || lua_toboolean(state, 2) != 0));
    return 1;
}

int LuaScript::ApiStopAudio(lua_State* state) {
    lua_pushboolean(state, Current(state)->StopAudio());
    return 1;
}

int LuaScript::ApiPlayParticles(lua_State* state) {
    lua_pushboolean(state, Current(state)->PlayParticles(lua_toboolean(state, 1) != 0));
    return 1;
}

int LuaScript::ApiStopParticles(lua_State* state) {
    lua_pushboolean(state, Current(state)->StopParticles(lua_toboolean(state, 1) != 0));
    return 1;
}

int LuaScript::ApiBurstParticles(lua_State* state) {
    lua_pushboolean(state, Current(state)->BurstParticles(
        static_cast<int>(luaL_optinteger(state, 1, 0))));
    return 1;
}

int LuaScript::ApiShakeCamera(lua_State* state) {
    lua_pushboolean(state, Current(state)->ShakeCamera(
        static_cast<float>(luaL_optnumber(state, 1, 1.0)),
        static_cast<float>(luaL_optnumber(state, 2, 0.35)),
        static_cast<float>(luaL_optnumber(state, 3, 18.0))));
    return 1;
}

int LuaScript::ApiGetHealth(lua_State* state) {
    LuaScript* script = Current(state);
    const ecs::Entity entity = EntityArgument(state, 1, script->Self());
    const Health* health = script->TryGet<Health>(entity);
    if (!health) return 0;
    lua_pushnumber(state, health->hp);
    return 1;
}

int LuaScript::ApiGetMaxHealth(lua_State* state) {
    LuaScript* script = Current(state);
    const ecs::Entity entity = EntityArgument(state, 1, script->Self());
    const Health* health = script->TryGet<Health>(entity);
    if (!health) return 0;
    lua_pushnumber(state, health->maxHp);
    return 1;
}

int LuaScript::ApiDamage(lua_State* state) {
    LuaScript* script = Current(state);
    const ecs::Entity entity = EntityArgument(state, 1, script->Self());
    Health* health = script->TryGet<Health>(entity);
    if (!health) {
        lua_pushboolean(state, 0);
        return 1;
    }
    health->Damage(std::max(0.0f, static_cast<float>(luaL_checknumber(state, 2))));
    lua_pushboolean(state, 1);
    return 1;
}

int LuaScript::ApiHeal(lua_State* state) {
    LuaScript* script = Current(state);
    const ecs::Entity entity = EntityArgument(state, 1, script->Self());
    Health* health = script->TryGet<Health>(entity);
    if (!health) {
        lua_pushboolean(state, 0);
        return 1;
    }
    health->hp = std::min(health->maxHp, health->hp
        + std::max(0.0f, static_cast<float>(luaL_checknumber(state, 2))));
    if (health->hp > 0.0f) health->alive = true;
    lua_pushboolean(state, 1);
    return 1;
}

int LuaScript::ApiAddScore(lua_State* state) {
    if (GameMode* game = Current(state)->Game()) {
        game->AddScore(static_cast<int>(luaL_checkinteger(state, 1)));
        lua_pushboolean(state, 1);
    } else lua_pushboolean(state, 0);
    return 1;
}

int LuaScript::ApiResetGame(lua_State* state) {
    if (GameMode* game = Current(state)->Game()) {
        game->Reset();
        lua_pushboolean(state, 1);
    } else lua_pushboolean(state, 0);
    return 1;
}

int LuaScript::ApiGetScore(lua_State* state) {
    const GameMode* game = Current(state)->Game();
    if (!game) return 0;
    lua_pushinteger(state, game->Score());
    return 1;
}

int LuaScript::ApiGetElapsed(lua_State* state) {
    const GameMode* game = Current(state)->Game();
    if (!game) return 0;
    lua_pushnumber(state, game->Elapsed());
    return 1;
}

int LuaScript::ApiGetGameState(lua_State* state) {
    const GameMode* game = Current(state)->Game();
    if (!game) return 0;
    lua_pushstring(state, GameMode::StateName(game->State()));
    return 1;
}

int LuaScript::ApiWin(lua_State* state) {
    if (GameMode* game = Current(state)->Game()) {
        game->Win(OptionalString(state, 1, "You Win!"));
        lua_pushboolean(state, 1);
    } else lua_pushboolean(state, 0);
    return 1;
}

int LuaScript::ApiLose(lua_State* state) {
    if (GameMode* game = Current(state)->Game()) {
        game->Lose(OptionalString(state, 1, "Game Over"));
        lua_pushboolean(state, 1);
    } else lua_pushboolean(state, 0);
    return 1;
}

int LuaScript::ApiRequestSceneLoad(lua_State* state) {
    Current(state)->RequestSceneLoad(luaL_checkstring(state, 1));
    return 0;
}

int LuaScript::ApiRequestLevelLoad(lua_State* state) {
    Current(state)->RequestLevelLoad(luaL_checkstring(state, 1));
    return 0;
}

int LuaScript::ApiRequestLevelUnload(lua_State* state) {
    Current(state)->RequestLevelUnload(luaL_checkstring(state, 1));
    return 0;
}

int LuaScript::ApiSetTimerByFunctionName(lua_State* state) {
    const char* functionName = luaL_checkstring(state, 1);
    const float seconds = static_cast<float>(luaL_checknumber(state, 2));
    const bool repeat = lua_gettop(state) >= 3
        && lua_toboolean(state, 3) != 0;
    lua_pushinteger(state, Current(state)->SetTimerByFunctionName(
        functionName, seconds, repeat));
    return 1;
}

int LuaScript::ApiClearTimer(lua_State* state) {
    Current(state)->ClearTimer(static_cast<int>(luaL_checkinteger(state, 1)));
    return 0;
}

int LuaScript::ApiClearTimerByFunctionName(lua_State* state) {
    lua_pushinteger(state, Current(state)->ClearTimerByFunctionName(
        luaL_checkstring(state, 1)));
    return 1;
}

int LuaScript::ApiIsTimerActive(lua_State* state) {
    LuaScript* script = Current(state);
    if (lua_type(state, 1) == LUA_TNUMBER) {
        lua_pushboolean(state, script->IsTimerActive(
            static_cast<int>(lua_tointeger(state, 1))));
    } else {
        lua_pushboolean(state, script->IsTimerActive(
            luaL_checkstring(state, 1)));
    }
    return 1;
}

int LuaScript::ApiSetGlobalTimeDilation(lua_State* state) {
    Current(state)->SetGlobalTimeDilation(
        static_cast<float>(luaL_checknumber(state, 1)));
    return 0;
}

int LuaScript::ApiGetGlobalTimeDilation(lua_State* state) {
    lua_pushnumber(state, Current(state)->GlobalTimeDilation());
    return 1;
}

int LuaScript::ApiGetEffectiveTimeDilation(lua_State* state) {
    lua_pushnumber(state, Current(state)->EffectiveTimeDilation());
    return 1;
}

int LuaScript::ApiHitStop(lua_State* state) {
    const float duration = static_cast<float>(luaL_checknumber(state, 1));
    const float dilation = lua_gettop(state) >= 2
        ? static_cast<float>(luaL_checknumber(state, 2)) : 0.0f;
    Current(state)->HitStop(duration, dilation);
    return 0;
}

int LuaScript::ApiIsHitStopActive(lua_State* state) {
    lua_pushboolean(state, Current(state)->IsHitStopActive());
    return 1;
}

int LuaScript::ApiSaveValue(lua_State* state) {
    lua_pushboolean(state, Current(state)->SaveValue(
        luaL_checkstring(state, 1), luaL_checkstring(state, 2)));
    return 1;
}

int LuaScript::ApiLoadValue(lua_State* state) {
    const std::string value = Current(state)->LoadValue(
        luaL_checkstring(state, 1), OptionalString(state, 2, ""));
    lua_pushlstring(state, value.data(), value.size());
    return 1;
}

int LuaScript::ApiLoadAdaptiveMusic(lua_State* state) {
    lua_pushboolean(state, Current(state)->LoadAdaptiveMusic(
        luaL_checkstring(state, 1)));
    return 1;
}

int LuaScript::ApiSetMusicState(lua_State* state) {
    lua_pushboolean(state, Current(state)->SetMusicState(
        luaL_checkstring(state, 1), lua_gettop(state) < 2
            || lua_toboolean(state, 2) != 0));
    return 1;
}

int LuaScript::ApiPlayCameraSequence(lua_State* state) {
    lua_pushboolean(state, Current(state)->PlayCameraSequence(
        luaL_checkstring(state, 1),
        lua_gettop(state) < 2 || lua_toboolean(state, 2) != 0,
        lua_gettop(state) < 3 || lua_toboolean(state, 3) != 0));
    return 1;
}

int LuaScript::ApiIsCameraSequencePlaying(lua_State* state) {
    lua_pushboolean(state, Current(state)->IsCameraSequencePlaying(
        OptionalString(state, 1, "")));
    return 1;
}

int LuaScript::ApiWasCameraSequenceEvent(lua_State* state) {
    lua_pushboolean(state, Current(state)->WasCameraSequenceEvent(
        luaL_checkstring(state, 1), luaL_checkstring(state, 2)));
    return 1;
}

} // namespace engine
