#pragma once

#include <engine/gameplay/PhotoMode.h>

namespace photo_mode {
void Enable(const engine::PhotoModeSettings& settings = {});
void Disable();
void Toggle(const engine::PhotoModeSettings& settings = {});
bool IsEnabled();
void Capture(const char* filename = nullptr);
} // namespace photo_mode
