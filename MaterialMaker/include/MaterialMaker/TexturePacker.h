#pragma once

#include <string>

namespace material_maker {

// Result of a channel-pack operation.
struct PackResult {
    bool        ok = false;
    std::string outputPath;
    std::string error;
};

// Pack separate grayscale maps into one ORM texture following the glTF convention
// (R = ambient occlusion, G = roughness, B = metallic, A = 255), written as an
// uncompressed 32-bit TGA at `outputPath` (the engine loads TGA natively).
//
// `metallicPath` and `roughnessPath` are required and must share dimensions;
// `aoPath` is optional (empty => the AO channel is filled with 255). Each source
// is read from its red channel (grayscale maps store the value in every channel).
// PNG and JPG sources are supported. Returns ok=false with an error on failure.
PackResult PackMetalRoughAO(const std::string& metallicPath,
                            const std::string& roughnessPath,
                            const std::string& aoPath,
                            const std::string& outputPath);

} // namespace material_maker
