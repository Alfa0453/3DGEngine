#include "engine/graphics/Screenshot.h"

#include <glad/glad.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace engine {
namespace {

void Write16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8u);
}

void Write32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned int i = 0; i < 4; ++i)
        bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8u));
}

} // namespace

bool CaptureFramebufferBmp(const std::string& path, int width, int height,
                           std::string* error) {
    if (width <= 0 || height <= 0 || path.empty()) {
        if (error) *error = "invalid screenshot path or dimensions";
        return false;
    }
    std::error_code ec;
    const std::filesystem::path destination(path);
    if (!destination.parent_path().empty())
        std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
        if (error) *error = "could not create screenshot directory: " + ec.message();
        return false;
    }

    const std::size_t pixelBytes = static_cast<std::size_t>(width)
        * static_cast<std::size_t>(height) * 4u;
    std::vector<std::uint8_t> pixels(pixelBytes);
    GLint previousPack = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPack);
    while (glGetError() != GL_NO_ERROR) {} // discard errors from earlier render work
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, pixels.data());
    glPixelStorei(GL_PACK_ALIGNMENT, previousPack);
    if (glGetError() != GL_NO_ERROR) {
        if (error) *error = "OpenGL could not read the presented frame";
        return false;
    }

    std::vector<std::uint8_t> header(54u, 0u);
    header[0] = 'B'; header[1] = 'M';
    Write32(header, 2, static_cast<std::uint32_t>(header.size() + pixelBytes));
    Write32(header, 10, static_cast<std::uint32_t>(header.size()));
    Write32(header, 14, 40u);
    Write32(header, 18, static_cast<std::uint32_t>(width));
    Write32(header, 22, static_cast<std::uint32_t>(height)); // bottom-up matches glReadPixels
    Write16(header, 26, 1u);
    Write16(header, 28, 32u);
    Write32(header, 34, static_cast<std::uint32_t>(pixelBytes));

    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (error) *error = "could not open screenshot output file";
        return false;
    }
    output.write(reinterpret_cast<const char*>(header.data()),
                 static_cast<std::streamsize>(header.size()));
    output.write(reinterpret_cast<const char*>(pixels.data()),
                 static_cast<std::streamsize>(pixels.size()));
    if (!output) {
        if (error) *error = "could not finish writing screenshot";
        return false;
    }
    return true;
}

std::string MakeTimestampedScreenshotPath(const std::string& directory,
                                          const std::string& prefix) {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    std::ostringstream filename;
    filename << (prefix.empty() ? "Photo" : prefix) << '_'
             << std::put_time(&local, "%Y%m%d_%H%M%S") << '_'
             << std::setw(3) << std::setfill('0') << milliseconds << ".bmp";
    return (std::filesystem::path(directory) / filename.str()).string();
}

} // namespace engine
