#include "PhotoMode/PhotoMode.h"

#include <engine/ai/BtScript.h>
#include <engine/gameplay/Script.h>
#include <engine/plugins/LinkedPlugin.h>

#include <GLFW/glfw3.h>

#include <memory>

namespace {

class PM_PhotoModeController final : public engine::Script {
public:
    void OnCreate() override {
        BindScriptFunction("Enable", [this](const engine::ScriptEvent&, engine::ScriptEvent&) {
            Enable(); return true;
        });
        BindScriptFunction("Disable", [this](const engine::ScriptEvent&, engine::ScriptEvent&) {
            Disable(); return true;
        });
        BindScriptFunction("Toggle", [this](const engine::ScriptEvent&, engine::ScriptEvent&) {
            Toggle(); return true;
        });
        BindScriptFunction("Capture", [](const engine::ScriptEvent& arguments,
                                           engine::ScriptEvent&) {
            const std::string filename = arguments.GetString("filename");
            photo_mode::Capture(filename.empty() ? nullptr : filename.c_str());
            return true;
        });
    }

    void OnUpdate(float) override {
        if (WasKeyPressed(GetFieldInt("toggleKey", GLFW_KEY_F8))) Toggle();
        if (!photo_mode::IsEnabled()) return;
        engine::PhotoModeRuntime::Instance().SetSettings(ReadSettings());
        if (WasKeyPressed(GetFieldInt("captureKey", GLFW_KEY_F9)))
            photo_mode::Capture();
    }

    void OnDisable() override { Disable(); }
    void OnDestroy() override { Disable(); }

private:
    engine::PhotoModeSettings ReadSettings() const {
        engine::PhotoModeSettings settings;
        settings.fieldOfView = GetFieldFloat("fieldOfView", 45.0f);
        settings.moveSpeed = GetFieldFloat("moveSpeed", 6.0f);
        settings.lookSensitivity = GetFieldFloat("lookSensitivity", 0.10f);
        settings.exposureCompensationEV = GetFieldFloat("exposureEV", 0.0f);
        settings.saturation = GetFieldFloat("saturation", 1.0f);
        settings.contrast = GetFieldFloat("contrast", 1.0f);
        settings.depthOfField = GetFieldBool("depthOfField", false);
        settings.focusDistance = GetFieldFloat("focusDistance", 5.0f);
        settings.focusRange = GetFieldFloat("focusRange", 2.0f);
        settings.blurStrength = GetFieldFloat("blurStrength", 0.6f);
        return settings;
    }

    void Enable() {
        if (photo_mode::IsEnabled()) return;
        m_previousDilation = GlobalTimeDilation();
        m_changedDilation = GetFieldBool("freezeGameplay", true);
        photo_mode::Enable(ReadSettings());
        if (m_changedDilation) SetGlobalTimeDilation(0.0f);
    }
    void Disable() {
        if (!photo_mode::IsEnabled()) return;
        photo_mode::Disable();
        if (m_changedDilation) SetGlobalTimeDilation(m_previousDilation);
        m_changedDilation = false;
    }
    void Toggle() { if (photo_mode::IsEnabled()) Disable(); else Enable(); }

    float m_previousDilation = 1.0f;
    bool m_changedDilation = false;
};

void RegisterPhotoMode(engine::ScriptRegistry& scripts,
                       engine::ai::BtScriptRegistry&) {
    scripts.Register("PM_PhotoModeController",
        [] { return std::make_unique<PM_PhotoModeController>(); });
}

const bool kPhotoModeLinked = engine::plugins::RegisterLinkedPlugin(
    "com.3dgengine.photo_mode", &RegisterPhotoMode);
} // namespace
