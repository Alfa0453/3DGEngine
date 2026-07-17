#include "MaterialMaker/TexturePacker.h"

#include <engine/graphics/ImageDecode.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <filesystem>
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

engine::image::Image DecodeTGA(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    unsigned char header[18]{};
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    const int width = header[12] | (header[13] << 8);
    const int height = header[14] | (header[15] << 8);
    const int channels = header[16] / 8;
    if (!file || header[2] != 2 || width <= 0 || height <= 0 ||
        (channels != 3 && channels != 4)) {
        throw std::runtime_error("unsupported or invalid TGA: " + path);
    }
    file.seekg(header[0], std::ios::cur);
    std::vector<unsigned char> raw(static_cast<std::size_t>(width) * height * channels);
    file.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    if (!file) throw std::runtime_error("truncated TGA: " + path);

    engine::image::Image image;
    image.width = width;
    image.height = height;
    image.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    const bool topOrigin = (header[17] & 0x20) != 0;
    for (int y = 0; y < height; ++y) {
        const int sourceY = topOrigin ? y : height - 1 - y;
        for (int x = 0; x < width; ++x) {
            const std::size_t s = (static_cast<std::size_t>(sourceY) * width + x) * channels;
            const std::size_t d = (static_cast<std::size_t>(y) * width + x) * 4;
            image.rgba[d + 0] = raw[s + 2];
            image.rgba[d + 1] = raw[s + 1];
            image.rgba[d + 2] = raw[s + 0];
            image.rgba[d + 3] = channels == 4 ? raw[s + 3] : 255;
        }
    }
    return image;
}

// Decode a supported source to RGBA (top row first).
engine::image::Image DecodeAny(const std::string& path) {
    const std::string ext = LowerExt(path);
    if (ext == "png") {
        return engine::image::DecodePNG(path);
    }
    if (ext == "jpg" || ext == "jpeg") {
        return engine::image::DecodeJPEG(path);
    }
    if (ext == "tga") {
        return DecodeTGA(path);
    }
    throw std::runtime_error("unsupported source format (use PNG, JPG or TGA): " + path);
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
        if (metallicPath.empty() && roughnessPath.empty() && aoPath.empty()) {
            result.error = "at least one ORM source is required.";
            return result;
        }
        const bool hasMetal = !metallicPath.empty();
        const bool hasRough = !roughnessPath.empty();
        const bool hasAo = !aoPath.empty();
        engine::image::Image metal, rough, ao;
        if (hasMetal) metal = DecodeAny(metallicPath);
        if (hasRough) rough = DecodeAny(roughnessPath);
        if (hasAo) ao = DecodeAny(aoPath);
        const engine::image::Image& reference = hasMetal ? metal : (hasRough ? rough : ao);
        const int w = reference.width;
        const int h = reference.height;
        if (w <= 0 || h <= 0 || w > 65535 || h > 65535) {
            result.error = "ORM source dimensions are invalid or exceed the TGA limit.";
            return result;
        }
        auto matches = [&](const engine::image::Image& image) {
            return image.width == w && image.height == h;
        };
        if ((hasMetal && !matches(metal)) || (hasRough && !matches(rough)) ||
            (hasAo && !matches(ao))) {
            result.error = "all provided ORM images must have the same size.";
            return result;
        }
        std::vector<unsigned char> out(static_cast<std::size_t>(w) * h * 4);
        for (int y = 0; y < h; ++y) {
            const int fileRow = h - 1 - y;   // TGA is written bottom-up
            for (int x = 0; x < w; ++x) {
                const std::size_t d = (static_cast<std::size_t>(fileRow) * w + x) * 4;
                out[d + 0] = hasMetal ? Red(metal, x, y) : 255; // B = metallic
                out[d + 1] = hasRough ? Red(rough, x, y) : 255; // G = roughness
                out[d + 2] = hasAo ? Red(ao, x, y) : 255;      // R = ambient occlusion
                out[d + 3] = 255;                              // A
            }
        }

        const std::filesystem::path parent = std::filesystem::path(outputPath).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
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

PackResult PackCombinedMetalRoughAO(const std::string& combinedPath,
                                    const std::string& aoPath,
                                    const std::string& outputPath) {
    PackResult result;
    try {
        if (combinedPath.empty()) {
            result.error = "a combined metallic-roughness source is required.";
            return result;
        }
        const engine::image::Image combined = DecodeAny(combinedPath);
        const bool hasAo = !aoPath.empty();
        engine::image::Image ao;
        if (hasAo) ao = DecodeAny(aoPath);
        const int w = combined.width, h = combined.height;
        if (w <= 0 || h <= 0 || w > 65535 || h > 65535) {
            result.error = "ORM source dimensions are invalid or exceed the TGA limit.";
            return result;
        }
        if (hasAo && (ao.width != w || ao.height != h)) {
            result.error = "AO image must match the combined metallic-roughness size.";
            return result;
        }
        std::vector<unsigned char> out(static_cast<std::size_t>(w) * h * 4);
        for (int y = 0; y < h; ++y) {
            const int fileRow = h - 1 - y;
            for (int x = 0; x < w; ++x) {
                const std::size_t s = (static_cast<std::size_t>(y) * w + x) * 4;
                const std::size_t d = (static_cast<std::size_t>(fileRow) * w + x) * 4;
                out[d + 0] = combined.rgba[s + 2];             // B = metallic
                out[d + 1] = combined.rgba[s + 1];             // G = roughness
                out[d + 2] = hasAo ? Red(ao, x, y) : 255;      // R = AO
                out[d + 3] = 255;
            }
        }
        const std::filesystem::path parent = std::filesystem::path(outputPath).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
        if (!WriteTGA32(outputPath, w, h, out, &result.error)) return result;
        result.ok = true;
        result.outputPath = outputPath;
        return result;
    } catch (const std::exception& e) {
        result.error = e.what();
        return result;
    }
}

} // namespace material_maker
