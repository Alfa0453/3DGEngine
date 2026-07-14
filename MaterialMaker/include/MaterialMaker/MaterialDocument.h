#pragma once

#include <array>
#include <string>

namespace material_maker {

struct MaterialDocument {
    std::string name = "NewMaterial";

    std::array<float, 3> albedo{0.8f, 0.8f, 0.8f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    std::array<float, 3> emissive{0.0f, 0.0f, 0.0f};
    float emissiveStrength = 1.0f;   // multiplier -> HDR emissive that blooms (>1)

    std::string albedoMap;
    std::string normalMap;
    std::string metalRoughMap;
};

std::string SanitizeFileStem(const std::string& name);
std::string ToJson(const MaterialDocument& material);
std::string ToCppInitializer(const MaterialDocument& material);
bool SaveMaterialFile(const MaterialDocument& material, const std::string& outputDirectory, std::string* writtenPath, std::string* error);
bool LoadMaterialFile(const std::string& path, MaterialDocument* material, std::string* error);

} // namespace material_maker