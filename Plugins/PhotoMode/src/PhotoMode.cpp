#include "PhotoMode/PhotoMode.h"

namespace photo_mode {
void Enable(const engine::PhotoModeSettings& settings) {
    engine::PhotoModeRuntime::Instance().Activate(settings);
}
void Disable() { engine::PhotoModeRuntime::Instance().Deactivate(); }
void Toggle(const engine::PhotoModeSettings& settings) {
    auto& runtime = engine::PhotoModeRuntime::Instance();
    if (runtime.Active()) runtime.Deactivate(); else runtime.Activate(settings);
}
bool IsEnabled() { return engine::PhotoModeRuntime::Instance().Active(); }
void Capture(const char* filename) {
    engine::PhotoModeRuntime::Instance().RequestScreenshot(filename ? filename : "");
}
} // namespace photo_mode
