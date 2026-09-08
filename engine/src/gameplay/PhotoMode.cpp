#include "engine/gameplay/PhotoMode.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace engine {
namespace {

PhotoModeSettings Sanitize(PhotoModeSettings settings) {
    auto finiteOr = [](float value, float fallback) {
        return std::isfinite(value) ? value : fallback;
    };
    settings.fieldOfView = std::clamp(finiteOr(settings.fieldOfView, 45.0f), 10.0f, 120.0f);
    settings.moveSpeed = std::clamp(finiteOr(settings.moveSpeed, 6.0f), 0.05f, 200.0f);
    settings.lookSensitivity = std::clamp(
        finiteOr(settings.lookSensitivity, 0.10f), 0.001f, 2.0f);
    settings.exposureCompensationEV = std::clamp(
        finiteOr(settings.exposureCompensationEV, 0.0f), -10.0f, 10.0f);
    settings.saturation = std::clamp(finiteOr(settings.saturation, 1.0f), 0.0f, 3.0f);
    settings.contrast = std::clamp(finiteOr(settings.contrast, 1.0f), 0.0f, 3.0f);
    settings.focusDistance = std::clamp(
        finiteOr(settings.focusDistance, 5.0f), 0.05f, 10000.0f);
    settings.focusRange = std::clamp(finiteOr(settings.focusRange, 2.0f), 0.05f, 10000.0f);
    settings.blurStrength = std::clamp(finiteOr(settings.blurStrength, 0.6f), 0.0f, 1.0f);
    return settings;
}

} // namespace

PhotoModeRuntime& PhotoModeRuntime::Instance() {
    static PhotoModeRuntime runtime;
    return runtime;
}

void PhotoModeRuntime::Activate(const PhotoModeSettings& settings) {
    m_settings = Sanitize(settings);
    m_active = true;
    ++m_revision;
}

void PhotoModeRuntime::Deactivate() {
    if (!m_active) return;
    m_active = false;
    ++m_revision;
}

void PhotoModeRuntime::Reset() {
    m_settings = {};
    m_screenshotFilename.clear();
    m_screenshotRequested = false;
    if (m_active) ++m_revision;
    m_active = false;
}

void PhotoModeRuntime::SetSettings(const PhotoModeSettings& settings) {
    const PhotoModeSettings clean = Sanitize(settings);
    m_settings = clean;
    ++m_revision;
}

void PhotoModeRuntime::RequestScreenshot(std::string filename) {
    m_screenshotFilename = std::move(filename);
    m_screenshotRequested = true;
}

bool PhotoModeRuntime::ConsumeScreenshotRequest(std::string* filename) {
    if (!m_screenshotRequested) return false;
    if (filename) *filename = std::move(m_screenshotFilename);
    m_screenshotFilename.clear();
    m_screenshotRequested = false;
    return true;
}

} // namespace engine
