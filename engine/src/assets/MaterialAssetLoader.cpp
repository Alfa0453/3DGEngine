#include "engine/assets/MaterialAssetLoader.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace engine {
namespace {

std::string ReadTextFile(const std::string& path, std::string* error) {
    std::ifstream in(path);
    if (!in) {
        if (error) {
            *error = "Could not open material file: " + path;
        }
        return {};
    }

    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::string UnescapeJsonString(std::string value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            ++i;
            switch (value[i]) {
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            default: out.push_back(value[i]); break;
            }
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

bool FindStringFrom(const std::string& text, const std::string& key, std::size_t start, std::string* value) {
    const std::string marker = "\"" + key + "\"";
    std::size_t pos = text.find(marker, start);
    if (pos == std::string::npos) {
        return false;
    }
    pos = text.find(':', pos + marker.size());
    if (pos == std::string::npos) {
        return false;
    }
    pos = text.find('"', pos + 1);
    if (pos == std::string::npos) {
        return false;
    }

    std::string raw;
    bool escaped = false;
    for (++pos; pos < text.size(); ++pos) {
        const char c = text[pos];
        if (!escaped && c == '"') {
            if (value) {
                *value = UnescapeJsonString(raw);
            }
            return true;
        }
        escaped = !escaped && c == '\\';
        raw.push_back(c);
        if (c != '\\') {
            escaped = false;
        }
    }
    return false;
}

bool FindString(const std::string& text, const std::string& key, std::string* value) {
    return FindStringFrom(text, key, 0, value);
}

bool FindFloat(const std::string& text, const std::string& key, float* value) {
    const std::string marker = "\"" + key + "\"";
    std::size_t pos = text.find(marker);
    if (pos == std::string::npos) {
        return false;
    }
    pos = text.find(':', pos + marker.size());
    if (pos == std::string::npos) {
        return false;
    }

    char* end = nullptr;
    const float parsed = std::strtof(text.c_str() + pos + 1, &end);
    if (end == text.c_str() + pos + 1) {
        return false;
    }
    if (value) {
        *value = parsed;
    }
    return true;
}

bool FindVec3(const std::string& text, const std::string& key, glm::vec3* value) {
    const std::string marker = "\"" + key + "\"";
    std::size_t pos = text.find(marker);
    if (pos == std::string::npos) {
        return false;
    }
    pos = text.find('[', pos + marker.size());
    if (pos == std::string::npos) {
        return false;
    }

    char* end = nullptr;
    const char* start = text.c_str() + pos + 1;
    const float x = std::strtof(start, &end);
    if (end == start) {
        return false;
    }
    const float y = std::strtof(end + 1, &end);
    const float z = std::strtof(end + 1, &end);
    if (value) {
        *value = glm::vec3(x, y, z);
    }
    return true;
}

bool FindVec2(const std::string& text, const std::string& key, glm::vec2* value) {
    const std::string marker = "\"" + key + "\"";
    std::size_t pos = text.find(marker);
    if (pos == std::string::npos || (pos = text.find('[', pos + marker.size())) == std::string::npos) return false;
    char* end = nullptr;
    const char* start = text.c_str() + pos + 1;
    const float x = std::strtof(start, &end); if (end == start) return false;
    start = end + 1; const float y = std::strtof(start, &end); if (end == start) return false;
    if (value) *value = glm::vec2(x, y);
    return true;
}

std::string ResolveMapPath(const std::string& materialPath, const std::string& mapPath) {
    if (mapPath.empty()) {
        return {};
    }

    const std::filesystem::path path(mapPath);
    if (path.is_absolute()) {
        return path.string();
    }
    return (std::filesystem::path(materialPath).parent_path() / path).string();
}

} // namespace

bool LoadMaterialAssetFile(const std::string& path, RuntimeMaterialAsset* material, std::string* error) {
    if (!material) {
        if (error) {
            *error = "Material output pointer was null.";
        }
        return false;
    }

    const std::string text = ReadTextFile(path, error);
    if (text.empty()) {
        return false;
    }

    RuntimeMaterialAsset loaded;
    std::string schema;
    if (!FindString(text, "schema", &schema) || schema != "3DGEngine.PbrMaterial") {
        if (error) {
            *error = "Material file has an unknown schema.";
        }
        return false;
    }

    FindString(text, "name", &loaded.name);
    FindVec3(text, "albedo", &loaded.material.albedo);
    FindFloat(text, "metallic", &loaded.material.metallic);
    FindFloat(text, "roughness", &loaded.material.roughness);
    FindFloat(text, "ao", &loaded.material.ao);
    FindVec3(text, "emissive", &loaded.material.emissive);
    float blendMode = 0.0f;
    if (FindFloat(text, "blendMode", &blendMode))
        loaded.material.blendMode = static_cast<ecs::PbrMaterial::BlendMode>(static_cast<int>(blendMode));
    FindFloat(text, "opacity", &loaded.material.opacity);
    FindFloat(text, "alphaCutoff", &loaded.material.alphaCutoff);
    FindVec2(text, "uvScale", &loaded.material.uvScale);
    FindVec2(text, "uvOffset", &loaded.material.uvOffset);
    FindFloat(text, "uvRotation", &loaded.material.uvRotation);
    FindFloat(text, "normalStrength", &loaded.material.normalStrength);
    FindFloat(text, "heightScale", &loaded.material.heightScale);
    FindFloat(text, "clearcoat", &loaded.material.clearcoat);
    FindFloat(text, "clearcoatRoughness", &loaded.material.clearcoatRoughness);
    FindFloat(text, "transmission", &loaded.material.transmission);
    FindFloat(text, "ior", &loaded.material.ior);
    FindFloat(text, "thickness", &loaded.material.thickness);
    FindFloat(text, "anisotropy", &loaded.material.anisotropy);
    FindFloat(text, "anisotropyRotation", &loaded.material.anisotropyRotation);
    FindVec3(text, "sheenColor", &loaded.material.sheenColor);
    FindFloat(text, "sheenRoughness", &loaded.material.sheenRoughness);
    FindFloat(text, "specularLevel", &loaded.material.specularLevel);
    FindFloat(text, "subsurface", &loaded.material.subsurface);
    FindVec3(text, "subsurfaceColor", &loaded.material.subsurfaceColor);

    std::string albedoMap;
    std::string normalMap;
    std::string metalRoughMap;
    std::string heightMap;
    const std::size_t mapsStart = text.find("\"maps\"");
    if (mapsStart != std::string::npos) {
        FindStringFrom(text, "albedo", mapsStart, &albedoMap);
        FindStringFrom(text, "normal", mapsStart, &normalMap);
        FindStringFrom(text, "metalRough", mapsStart, &metalRoughMap);
        FindStringFrom(text, "height", mapsStart, &heightMap);
    }
    loaded.albedoMapPath = ResolveMapPath(path, albedoMap);
    loaded.normalMapPath = ResolveMapPath(path, normalMap);
    loaded.metalRoughMapPath = ResolveMapPath(path, metalRoughMap);
    loaded.heightMapPath = ResolveMapPath(path, heightMap);
    std::string shaderPath;
    if (FindString(text, "shader", &shaderPath))
        loaded.shaderPath = ResolveMapPath(path, shaderPath);
    std::size_t parameterCursor = 0;
    while ((parameterCursor = text.find("\"parameterName\"", parameterCursor))
           != std::string::npos) {
        RuntimeShaderParameter parameter;
        if (!FindStringFrom(text, "parameterName", parameterCursor, &parameter.name)
            || !FindStringFrom(text, "parameterValue", parameterCursor, &parameter.value)) break;
        const std::size_t typeKey = text.find("\"parameterType\"", parameterCursor);
        const std::size_t colon = typeKey == std::string::npos
            ? std::string::npos : text.find(':', typeKey);
        if (colon == std::string::npos) break;
        parameter.type = static_cast<int>(
            std::strtol(text.c_str() + colon + 1, nullptr, 10));
        if (parameter.type == 7)
            parameter.value = ResolveMapPath(path, parameter.value);
        loaded.shaderParameters.push_back(std::move(parameter));
        parameterCursor = colon + 1;
    }

    *material = loaded;
    if (error) {
        error->clear();
    }
    return true;
}

} // namespace engine
