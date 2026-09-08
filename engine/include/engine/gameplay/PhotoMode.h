#pragma once

#include <cstdint>
#include <string>

namespace engine {

// Runtime state shared by a photo-mode controller script and the active host
// (Editor Play or the standalone player). It deliberately contains no editor
// or input policy so plugins remain responsible for deciding how it is used.
struct PhotoModeSettings {
    float fieldOfView = 45.0f;
    float moveSpeed = 6.0f;
    float lookSensitivity = 0.10f;
    float exposureCompensationEV = 0.0f;
    float saturation = 1.0f;
    float contrast = 1.0f;
    bool depthOfField = false;
    float focusDistance = 5.0f;
    float focusRange = 2.0f;
    float blurStrength = 0.6f;
};

class PhotoModeRuntime {
public:
    static PhotoModeRuntime& Instance();

    void Activate(const PhotoModeSettings& settings = {});
    void Deactivate();
    void Reset();
    bool Active() const { return m_active; }

    void SetSettings(const PhotoModeSettings& settings);
    const PhotoModeSettings& Settings() const { return m_settings; }
    std::uint64_t Revision() const { return m_revision; }

    // Capture requests are consumed by whichever host owns the backbuffer.
    void RequestScreenshot(std::string filename = {});
    bool ConsumeScreenshotRequest(std::string* filename = nullptr);

private:
    PhotoModeSettings m_settings{};
    std::string m_screenshotFilename;
    std::uint64_t m_revision = 0;
    bool m_active = false;
    bool m_screenshotRequested = false;
};

} // namespace engine
