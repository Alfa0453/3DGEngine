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
// At least one source is required; omitted channels are filled with 255 so the
// material's scalar factor remains effective. Provided maps must share dimensions.
// Each source
// is read from its red channel (grayscale maps store the value in every channel).
// PNG, JPG and uncompressed TGA sources are supported. Returns ok=false on failure.
PackResult PackMetalRoughAO(const std::string& metallicPath,
                            const std::string& roughnessPath,
                            const std::string& aoPath,
                            const std::string& outputPath);

// Normalize a glTF metallic-roughness texture (G = roughness, B = metallic) into
// an engine ORM map, filling R from a separate AO map or white when AO is absent.
PackResult PackCombinedMetalRoughAO(const std::string& combinedPath,
                                    const std::string& aoPath,
                                    const std::string& outputPath);

} // namespace material_maker
