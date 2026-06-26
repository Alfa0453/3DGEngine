#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace engine::image {

// A decoded image, always expanded to 8-bit RGBA (4 channels, top row first).
struct Image {
    int width  = 0;
    int height = 0;
    std::vector<unsigned char> rgba;    // width*height*4 bytes
};

// Decode a PNG file to RGBA. Supports 8-bit non-interlaced PNGs of every colour
// type (grayscale, RGB, palette, grayscale+alpha, RGBA), including a palette
// tRNS alpha chunk. Throws std::runtime_error on anything unsupported/corrupt.
Image DecodePNG(const std::string& path);
Image DecodePNGFromMemeory(const unsigned char* data, std::size_t size);

// Decode a baseline JPEG file to RGBA. (Added in the JPEG milestone.)
Image DecodeJPEG(const std::string& path);
Image DecodeJPEGFromMemory(const unsigned char* data, std::size_t size);

} // namespace engine::image