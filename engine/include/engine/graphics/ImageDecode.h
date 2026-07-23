#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace engine::image {

// A decoded image, always expanded to 8-bit RGBA (4 channels, top row first).
struct Image {
    int width  = 0;
    int height = 0;
    std::vector<unsigned char> rgba;    // width*height*4 bytes
    // Original PNG luminance normalized to the full 16-bit range. This preserves
    // 16-bit heightfields while rgba remains the normal 8-bit GPU texture payload.
    int sourceBitDepth = 8;
    std::vector<std::uint16_t> luminance16;
};

// Decode a non-interlaced PNG to 8-bit RGBA. Supports 16/8-bit grayscale, RGB,
// grayscale+alpha and RGBA, plus 1/2/4/8-bit palette images and palette tRNS.
// Sixteen-bit samples are rounded to 8-bit for GPU upload.
Image DecodePNG(const std::string& path);
Image DecodePNGFromMemeory(const unsigned char* data, std::size_t size);

// Decode a baseline JPEG file to RGBA. (Added in the JPEG milestone.)
Image DecodeJPEG(const std::string& path);
Image DecodeJPEGFromMemory(const unsigned char* data, std::size_t size);

} // namespace engine::image
