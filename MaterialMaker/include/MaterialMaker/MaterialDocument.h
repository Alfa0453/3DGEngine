#pragma once

#include <array>
#include <string>
#include <vector>

namespace material_maker {

struct ShaderParameterDocument {
    std::string name;
    int type = 0;
    std::string value;
};

struct MaterialDocument {
    std::string name = "NewMaterial";

    std::array<float, 3> albedo{0.8f, 0.8f, 0.8f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    std::array<float, 3> emissive{0.0f, 0.0f, 0.0f};
    float emissiveStrength = 1.0f;   // multiplier -> HDR emissive that blooms (>1)
    int blendMode = 0;               // 0 opaque, 1 masked, 2 transparent
    float opacity = 1.0f;
    float alphaCutoff = 0.5f;
    std::array<float, 2> uvScale{1.0f, 1.0f};
    std::array<float, 2> uvOffset{0.0f, 0.0f};
    float uvRotation = 0.0f;
    float normalStrength = 1.0f;
    float heightScale = 0.0f;
    float clearcoat = 0.0f;
    float clearcoatRoughness = 0.1f;
    float transmission = 0.0f;
    float ior = 1.5f;
    float thickness = 0.0f;
    float anisotropy = 0.0f;
    float anisotropyRotation = 0.0f;
    std::array<float, 3> sheenColor{0.0f, 0.0f, 0.0f};
    float sheenRoughness = 0.5f;
    float specularLevel = 0.5f;
    float subsurface = 0.0f;
    std::array<float, 3> subsurfaceColor{1.0f, 1.0f, 1.0f};

    std::string albedoMap;
    std::string normalMap;
    std::string metalRoughMap;
    std::string heightMap;
    std::string shaderPath;
    std::vector<ShaderParameterDocument> shaderParameters;
};

std::string SanitizeFileStem(const std::string& name);
std::string ToJson(const MaterialDocument& material);
std::string ToCppInitializer(const MaterialDocument& material);
bool SaveMaterialFile(const MaterialDocument& material, const std::string& outputDirectory, std::string* writtenPath, std::string* error);
bool LoadMaterialFile(const std::string& path, MaterialDocument* material, std::string* error);

} // namespace material_maker
