#include "MaterialMaker/TexturePacker.h"

#include <engine/graphics/ImageDecode.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace material_maker {
namespace {

std::string LowerExt(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    std::string e = (dot == std::string::npos) ? std::string() : path.substr(dot + 1);
    for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

// Decode a PNG or JPG source to RGBA (top row first). Throws on anything else.
engine::image::Image DecodeAny(const std::string& path) {
    const std::string ext = LowerExt(path);
    if (ext == "png") {
        return engine::image::DecodePNG(path);
    }
    if (ext == "jpg" || ext == "jpeg") {
        return engine::image::DecodeJPEG(path);
    }
    throw std::runtime_error("unsupported source format (use PNG or JPG): " + path);
}

// Red channel at (x, y) — grayscale maps store the value in every channel.
unsigned char Red(const engine::image::Image& im, int x, int y) {
    const std::size_t i = (static_cast<std::size_t>(y) * im.width + x) * 4;
    return im.rgba[i];
}

// Write tightly-packed bottom-up BGRA as an uncompressed 32-bit TGA.
bool WriteTGA32(const std::string& path, int w, int h,
                const std::vector<unsigned char>& bgraBottomUp, std::string* error) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        if (error) *error = "could not open output file for writing: " + path;
        return false;
    }
    unsigned char header[18] = {0};
    header[2]  = 2;                                        // uncompressed true-colour
    header[12] = static_cast<unsigned char>(w & 0xFF);
    header[13] = static_cast<unsigned char>((w >> 8) & 0xFF);
    header[14] = static_cast<unsigned char>(h & 0xFF);
    header[15] = static_cast<unsigned char>((h >> 8) & 0xFF);
    header[16] = 32;                                       // bits per pixel
    header[17] = 8;                                        // 8 alpha bits, bottom-up origin
    out.write(reinterpret_cast<const char*>(header), sizeof(header));
    out.write(reinterpret_cast<const char*>(bgraBottomUp.data()),
              static_cast<std::streamsize>(bgraBottomUp.size()));
    return static_cast<bool>(out);
}

} // namespace

PackResult PackMetalRoughAO(const std::string& metallicPath, const std::string& roughnessPath,
                            const std::string& aoPath, const std::string& outputPath) {
    PackResult result;
    try {
        if (metallicPath.empty() || roughnessPath.empty()) {
            result.error = "metallic and roughness sources are required.";
            return result;
        }
        const engine::image::Image metal = DecodeAny(metallicPath);
        const engine::image::Image rough = DecodeAny(roughnessPath);
        if (metal.width != rough.width || metal.height != rough.height) {
            result.error = "metallic and roughness images must have the same size.";
            return result;
        }

        const bool hasAo = !aoPath.empty();
        engine::image::Image ao;
        if (hasAo) {
            ao = DecodeAny(aoPath);
            if (ao.width != metal.width || ao.height != metal.height) {
                result.error = "AO image must match the metallic/roughness size.";
                return result;
            }
        }

        const int w = metal.width;
        const int h = metal.height;
        std::vector<unsigned char> out(static_cast<std::size_t>(w) * h * 4);
        for (int y = 0; y < h; ++y) {
            const int fileRow = h - 1 - y;   // TGA is written bottom-up
            for (int x = 0; x < w; ++x) {
                const std::size_t d = (static_cast<std::size_t>(fileRow) * w + x) * 4;
                out[d + 0] = Red(metal, x, y);                 // B = metallic
                out[d + 1] = Red(rough, x, y);                 // G = roughness
                out[d + 2] = hasAo ? Red(ao, x, y) : 255;      // R = ambient occlusion
                out[d + 3] = 255;                              // A
            }
        }

        if (!WriteTGA32(outputPath, w, h, out, &result.error)) {
            return result;
        }
        result.ok = true;
        result.outputPath = outputPath;
        return result;
    } catch (const std::exception& e) {
        result.error = e.what();
        return result;
    }
}

} // namespace material_maker
