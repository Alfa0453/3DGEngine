#pragma once

#include <string>

namespace engine {

// Captures the currently bound read framebuffer as a 32-bit BMP. Call after
// final scene presentation and before UI drawing for a clean gameplay image.
bool CaptureFramebufferBmp(const std::string& path, int width, int height,
                           std::string* error = nullptr);

// Returns Photo_YYYYMMDD_HHMMSS_mmm.bmp in the supplied directory.
std::string MakeTimestampedScreenshotPath(const std::string& directory,
                                          const std::string& prefix = "Photo");

} // namespace engine
