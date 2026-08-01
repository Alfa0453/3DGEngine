#pragma once

#include "engine/gameplay/Script.h"

#include <string>

struct lua_State;

namespace engine {

// A gameplay Script implemented by a .lua source file. It deliberately shares
// NativeScriptComponent with C++ scripts, so attachment, fields, scene saving,
// prefabs, characters, Play mode, and packaged builds all follow one path.
class LuaScript final : public Script {
public:
    explicit LuaScript(std::string sourcePath);
    ~LuaScript() override;

    LuaScript(const LuaScript&) = delete;
    LuaScript& operator=(const LuaScript&) = delete;

    void OnCreate() override;
    void OnUpdate(float dt) override;
    void OnFixedUpdate(float dt) override;
    void OnDestroy() override;

    const std::string& SourcePath() const { return m_sourcePath; }

private:
    void Load();
    void Call(const char* functionName);
    void Call(const char* functionName, float argument);
    void RegisterEngineApi();
    bool HasTimerFunction(const std::string& functionName) const override;
    bool InvokeTimerFunction(const std::string& functionName) override;

    static LuaScript* Current(lua_State* state);
    static int ApiLog(lua_State* state);
    static int ApiSelf(lua_State* state);
    static int ApiGetPosition(lua_State* state);
    static int ApiSetPosition(lua_State* state);
    static int ApiTranslate(lua_State* state);
    static int ApiGetScale(lua_State* state);
    static int ApiSetScale(lua_State* state);
    static int ApiGetForward(lua_State* state);
    static int ApiFindObject(lua_State* state);
    static int ApiSplinePointCount(lua_State* state);
    static int ApiGetSplinePoint(lua_State* state);
    static int ApiSetSplinePoint(lua_State* state);
    static int ApiGetSplinePointRotation(lua_State* state);
    static int ApiSetSplinePointRotation(lua_State* state);
    static int ApiAddSplinePoint(lua_State* state);
    static int ApiInsertSplinePoint(lua_State* state);
    static int ApiRemoveSplinePoint(lua_State* state);
    static int ApiTranslateSpline(lua_State* state);
    static int ApiSetSplineClosed(lua_State* state);
    static int ApiIsSplineClosed(lua_State* state);
    static int ApiSplinePositionAt(lua_State* state);
    static int ApiSplineTangentAt(lua_State* state);
    static int ApiDestroy(lua_State* state);
    static int ApiDestroySelf(lua_State* state);
    static int ApiSpawnEmpty(lua_State* state);
    static int ApiSpawnFromObject(lua_State* state);
    static int ApiConfigureProjectile(lua_State* state);
    static int ApiSocketPosition(lua_State* state);
    static int ApiKeyDown(lua_State* state);
    static int ApiKeyPressed(lua_State* state);
    static int ApiMouseDown(lua_State* state);
    static int ApiMousePressed(lua_State* state);
    static int ApiMouseDelta(lua_State* state);
    static int ApiWasAnimationEvent(lua_State* state);
    static int ApiFieldFloat(lua_State* state);
    static int ApiFieldInt(lua_State* state);
    static int ApiFieldBool(lua_State* state);
    static int ApiFieldString(lua_State* state);
    static int ApiFieldVec3(lua_State* state);
    static int ApiFieldColor(lua_State* state);
    static int ApiFieldEntity(lua_State* state);
    static int ApiFieldAsset(lua_State* state);
    static int ApiPlayActionClip(lua_State* state);
    static int ApiSetAnimFloat(lua_State* state);
    static int ApiSetAnimBool(lua_State* state);
    static int ApiSetAnimTrigger(lua_State* state);
    static int ApiIsActionPlaying(lua_State* state);
    static int ApiPlayAudio(lua_State* state);
    static int ApiPlayAudioCue(lua_State* state);
    static int ApiStopAudio(lua_State* state);
    static int ApiPlayParticles(lua_State* state);
    static int ApiStopParticles(lua_State* state);
    static int ApiBurstParticles(lua_State* state);
    static int ApiShakeCamera(lua_State* state);
    static int ApiGetHealth(lua_State* state);
    static int ApiGetMaxHealth(lua_State* state);
    static int ApiDamage(lua_State* state);
    static int ApiHeal(lua_State* state);
    static int ApiAddScore(lua_State* state);
    static int ApiResetGame(lua_State* state);
    static int ApiGetScore(lua_State* state);
    static int ApiGetElapsed(lua_State* state);
    static int ApiGetGameState(lua_State* state);
    static int ApiWin(lua_State* state);
    static int ApiLose(lua_State* state);
    static int ApiRequestSceneLoad(lua_State* state);
    static int ApiRequestLevelLoad(lua_State* state);
    static int ApiRequestLevelUnload(lua_State* state);
    static int ApiSaveValue(lua_State* state);
    static int ApiLoadValue(lua_State* state);
    static int ApiLoadAdaptiveMusic(lua_State* state);
    static int ApiSetMusicState(lua_State* state);
    static int ApiPlayCameraSequence(lua_State* state);
    static int ApiIsCameraSequencePlaying(lua_State* state);
    static int ApiWasCameraSequenceEvent(lua_State* state);
    static int ApiSetTimerByFunctionName(lua_State* state);
    static int ApiClearTimer(lua_State* state);
    static int ApiClearTimerByFunctionName(lua_State* state);
    static int ApiIsTimerActive(lua_State* state);
    static int ApiSetGlobalTimeDilation(lua_State* state);
    static int ApiGetGlobalTimeDilation(lua_State* state);
    static int ApiGetEffectiveTimeDilation(lua_State* state);
    static int ApiHitStop(lua_State* state);
    static int ApiIsHitStopActive(lua_State* state);

    std::string m_sourcePath;
    lua_State* m_state = nullptr;
    bool m_loaded = false;
};

bool IsLuaScriptPath(const std::string& path);

} // namespace engine
