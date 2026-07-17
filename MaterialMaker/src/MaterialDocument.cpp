#include "MaterialMaker/MaterialDocument.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace material_maker {
namespace {

std::string EscapeJson(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char c : value) {
        switch (c) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += c; break;
        }
    }
    return escaped;
}

std::string Vec3Json(const std::array<float, 3>& value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(4)
        << '[' << value[0] << ", " << value[1] << ", " << value[2] << ']';
    return out.str();
}

std::string Vec2Json(const std::array<float, 2>& value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(4) << '[' << value[0] << ", " << value[1] << ']';
    return out.str();
}

std::string Vec3Cpp(const std::array<float, 3>& value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(4)
        << "glm::vec3(" << value[0] << "f, " << value[1] << "f, " << value[2] << "f)";
    return out.str();
}

std::string Vec2Cpp(const std::array<float, 2>& value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(4)
        << "glm::vec2(" << value[0] << "f, " << value[1] << "f)";
    return out.str();
}

std::string Identifier(const std::string& name) {
    std::string result = "mat_";
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        result += std::isalnum(u) ? static_cast<char>(c) : '_';
    }
    if (result == "mat_") {
        result += "material";
    }
    return result;
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

    std::string decoded;
    bool escaped = false;
    for (++pos; pos < text.size(); ++pos) {
        const char c = text[pos];
        if (!escaped && c == '"') {
            if (value) {
                *value = decoded;
            }
            return true;
        }
        if (escaped) {
            switch (c) {
            case '"': decoded.push_back('"'); break;
            case '\\': decoded.push_back('\\'); break;
            case '/': decoded.push_back('/'); break;
            case 'b': decoded.push_back('\b'); break;
            case 'f': decoded.push_back('\f'); break;
            case 'n': decoded.push_back('\n'); break;
            case 'r': decoded.push_back('\r'); break;
            case 't': decoded.push_back('\t'); break;
            default: return false; // Unicode escapes are not emitted by this tool.
            }
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else {
            decoded.push_back(c);
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
    if (end == text.c_str() + pos + 1 || !std::isfinite(parsed)) {
        return false;
    }
    while (*end && std::isspace(static_cast<unsigned char>(*end))) ++end;
    if (*end != ',' && *end != '}' && *end != ']') return false;
    if (value) {
        *value = parsed;
    }
    return true;
}

bool FindVec3(const std::string& text, const std::string& key, std::array<float, 3>* value) {
    const std::string marker = "\"" + key + "\"";
    std::size_t pos = text.find(marker);
    if (pos == std::string::npos) {
        return false;
    }
    pos = text.find('[', pos + marker.size());
    if (pos == std::string::npos) {
        return false;
    }

    const char* cursor = text.c_str() + pos + 1;
    auto parseComponent = [&](float* component, bool requireComma) {
        while (*cursor && std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
        char* end = nullptr;
        *component = std::strtof(cursor, &end);
        if (end == cursor) return false;
        cursor = end;
        while (*cursor && std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
        if (requireComma) {
            if (*cursor != ',') return false;
            ++cursor;
        }
        return true;
    };
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (!parseComponent(&x, true) || !parseComponent(&y, true) || !parseComponent(&z, false)) {
        return false;
    }
    while (*cursor && std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
    if (*cursor != ']') return false;
    if (value) {
        *value = {x, y, z};
    }
    return true;
}

bool FindVec2(const std::string& text, const std::string& key, std::array<float, 2>* value) {
    const std::string marker = "\"" + key + "\"";
    std::size_t pos = text.find(marker);
    if (pos == std::string::npos || (pos = text.find('[', pos + marker.size())) == std::string::npos) return false;
    const char* cursor = text.c_str() + pos + 1;
    auto parse = [&](float* component, bool comma) {
        while (*cursor && std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
        char* end = nullptr; *component = std::strtof(cursor, &end);
        if (end == cursor || !std::isfinite(*component)) return false;
        cursor = end; while (*cursor && std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
        if (comma) { if (*cursor != ',') return false; ++cursor; }
        return true;
    };
    float x = 0.0f, y = 0.0f;
    if (!parse(&x, true) || !parse(&y, false)) return false;
    while (*cursor && std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
    if (*cursor != ']') return false;
    if (value) *value = {x, y};
    return true;
}

} // namespace

namespace {

// Make `path` relative to `baseDir` for on-disk storage, so a saved material stays
// valid when the project moves. Falls back to the original (absolute) path when a
// relative form does not exist — e.g. a different Windows drive.
std::string MakeRelativePath(const std::string& path, const std::string& baseDir) {
    if (path.empty() || baseDir.empty()) {
        return path;
    }
    std::error_code ec;
    const std::filesystem::path rel = std::filesystem::relative(path, baseDir, ec);
    if (ec || rel.empty()) {
        return path;
    }
    return rel.generic_string();   // forward slashes: portable in the JSON
}

// Resolve a stored (possibly relative) path back to an absolute one against the
// material file's directory, so the loaded material can open its textures.
std::string ResolveRelativePath(const std::string& path, const std::string& baseDir) {
    if (path.empty()) {
        return path;
    }
    const std::filesystem::path p(path);
    if (p.is_absolute() || baseDir.empty()) {
        return path;
    }
    std::error_code ec;
    std::filesystem::path abs = std::filesystem::weakly_canonical(std::filesystem::path(baseDir) / p, ec);
    if (ec || abs.empty()) {
        abs = std::filesystem::path(baseDir) / p;
    }
    return abs.string();
}

bool ReplaceFileAtomically(const std::filesystem::path& temporary,
                           const std::filesystem::path& destination,
                           std::string* error) {
#if defined(_WIN32)
    if (MoveFileExW(temporary.c_str(), destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
        return true;
    }
    if (error) *error = "Could not replace material file (Windows error " +
                        std::to_string(GetLastError()) + ").";
    return false;
#else
    std::error_code ec;
    std::filesystem::rename(temporary, destination, ec);
    if (!ec) return true;
    if (error) *error = "Could not replace material file: " + ec.message();
    return false;
#endif
}

} // namespace

std::string SanitizeFileStem(const std::string& name) {
    std::string result;
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '_' || c == '-') {
            result += c;
        } else if (std::isspace(u)) {
            result += '_';
        }
    }
    return result.empty() ? "material" : result;
}

std::string ToJson(const MaterialDocument& material) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(4);
    out << "{\n";
    out << "  \"schema\": \"3DGEngine.PbrMaterial\",\n";
    out << "  \"version\": 4,\n";
    out << "  \"name\": \"" << EscapeJson(material.name) << "\",\n";
    out << "  \"albedo\": " << Vec3Json(material.albedo) << ",\n";
    out << "  \"metallic\": " << material.metallic << ",\n";
    out << "  \"roughness\": " << material.roughness << ",\n";
    out << "  \"ao\": " << material.ao << ",\n";
    // Bake the strength multiplier into the emissive the engine consumes, so a
    // value above 1 blooms. (The raw colour + strength are an authoring convenience.)
    const std::array<float, 3> emissiveHdr{material.emissive[0] * material.emissiveStrength,
                                           material.emissive[1] * material.emissiveStrength,
                                           material.emissive[2] * material.emissiveStrength};
    out << "  \"emissive\": " << Vec3Json(emissiveHdr) << ",\n";
    out << "  \"emissiveColor\": " << Vec3Json(material.emissive) << ",\n";
    out << "  \"emissiveStrength\": " << material.emissiveStrength << ",\n";
    out << "  \"blendMode\": " << material.blendMode << ",\n";
    out << "  \"opacity\": " << material.opacity << ",\n";
    out << "  \"alphaCutoff\": " << material.alphaCutoff << ",\n";
    out << "  \"uvScale\": " << Vec2Json(material.uvScale) << ",\n";
    out << "  \"uvOffset\": " << Vec2Json(material.uvOffset) << ",\n";
    out << "  \"uvRotation\": " << material.uvRotation << ",\n";
    out << "  \"normalStrength\": " << material.normalStrength << ",\n";
    out << "  \"heightScale\": " << material.heightScale << ",\n";
    out << "  \"clearcoat\": " << material.clearcoat << ",\n";
    out << "  \"clearcoatRoughness\": " << material.clearcoatRoughness << ",\n";
    out << "  \"transmission\": " << material.transmission << ",\n";
    out << "  \"ior\": " << material.ior << ",\n";
    out << "  \"thickness\": " << material.thickness << ",\n";
    out << "  \"anisotropy\": " << material.anisotropy << ",\n";
    out << "  \"anisotropyRotation\": " << material.anisotropyRotation << ",\n";
    out << "  \"sheenColor\": " << Vec3Json(material.sheenColor) << ",\n";
    out << "  \"sheenRoughness\": " << material.sheenRoughness << ",\n";
    out << "  \"specularLevel\": " << material.specularLevel << ",\n";
    out << "  \"subsurface\": " << material.subsurface << ",\n";
    out << "  \"subsurfaceColor\": " << Vec3Json(material.subsurfaceColor) << ",\n";
    out << "  \"shader\": \"" << EscapeJson(material.shaderPath) << "\",\n";
    out << "  \"shaderParameters\": [\n";
    for (std::size_t i = 0; i < material.shaderParameters.size(); ++i) {
        const auto& parameter = material.shaderParameters[i];
        out << "    {\"parameterName\": \"" << EscapeJson(parameter.name)
            << "\", \"parameterType\": " << parameter.type
            << ", \"parameterValue\": \"" << EscapeJson(parameter.value) << "\"}"
            << (i + 1 < material.shaderParameters.size() ? "," : "") << "\n";
    }
    out << "  ],\n";
    out << "  \"maps\": {\n";
    out << "    \"albedo\": \"" << EscapeJson(material.albedoMap) << "\",\n";
    out << "    \"normal\": \"" << EscapeJson(material.normalMap) << "\",\n";
    out << "    \"metalRough\": \"" << EscapeJson(material.metalRoughMap) << "\",\n";
    out << "    \"height\": \"" << EscapeJson(material.heightMap) << "\"\n";
    out << "  },\n";
    out << "  \"engineMapping\": {\n";
    out << "    \"component\": \"engine::ecs::PbrMaterial\",\n";
    out << "    \"metalRoughMap\": \"glTF ORM convention: G = roughness, B = metallic\"\n";
    out << "  }\n";
    out << "}\n";
    return out.str();
}

std::string ToCppInitializer(const MaterialDocument& material) {
    const std::string variable = Identifier(material.name);
    std::ostringstream out;
    out << std::fixed << std::setprecision(4);
    out << "engine::ecs::PbrMaterial " << variable << ";\n";
    out << variable << ".albedo = " << Vec3Cpp(material.albedo) << ";\n";
    out << variable << ".metallic = " << material.metallic << "f;\n";
    out << variable << ".roughness = " << material.roughness << "f;\n";
    out << variable << ".ao = " << material.ao << "f;\n";
    const std::array<float, 3> emissiveHdr{material.emissive[0] * material.emissiveStrength,
                                           material.emissive[1] * material.emissiveStrength,
                                           material.emissive[2] * material.emissiveStrength};
    out << variable << ".emissive = " << Vec3Cpp(emissiveHdr) << ";\n";
    out << variable << ".opacity = " << material.opacity << "f;\n";
    out << variable << ".alphaCutoff = " << material.alphaCutoff << "f;\n";
    out << variable << ".blendMode = static_cast<engine::ecs::PbrMaterial::BlendMode>(" << material.blendMode << ");\n";
    out << variable << ".uvScale = " << Vec2Cpp(material.uvScale) << ";\n";
    out << variable << ".uvOffset = " << Vec2Cpp(material.uvOffset) << ";\n";
    out << variable << ".uvRotation = " << material.uvRotation << "f;\n";
    out << variable << ".normalStrength = " << material.normalStrength << "f;\n";
    out << variable << ".heightScale = " << material.heightScale << "f;\n";
    out << variable << ".clearcoat = " << material.clearcoat << "f;\n";
    out << variable << ".clearcoatRoughness = " << material.clearcoatRoughness << "f;\n";
    out << variable << ".transmission = " << material.transmission << "f;\n";
    out << variable << ".ior = " << material.ior << "f;\n";
    out << variable << ".thickness = " << material.thickness << "f;\n";
    out << variable << ".anisotropy = " << material.anisotropy << "f;\n";
    out << variable << ".anisotropyRotation = " << material.anisotropyRotation << "f;\n";
    out << variable << ".sheenColor = " << Vec3Cpp(material.sheenColor) << ";\n";
    out << variable << ".sheenRoughness = " << material.sheenRoughness << "f;\n";
    out << variable << ".specularLevel = " << material.specularLevel << "f;\n";
    out << variable << ".subsurface = " << material.subsurface << "f;\n";
    out << variable << ".subsurfaceColor = " << Vec3Cpp(material.subsurfaceColor) << ";\n";
    return out.str();
}

bool SaveMaterialFile(const MaterialDocument& material, const std::string& outputDirectory, std::string* writtenPath, std::string* error) {
    try {
        const std::filesystem::path directory(outputDirectory);
        if (directory.empty()) {
            if (error) *error = "Material output directory is empty.";
            return false;
        }
        std::filesystem::create_directories(directory);

        const std::filesystem::path filePath = directory / (SanitizeFileStem(material.name) + ".3dgmat");
        std::filesystem::path temporary = filePath;
        temporary += ".tmp";
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "Could not open material file for writing.";
            return false;
        }

        // Store texture paths relative to the material file so the material stays
        // valid when the project moves.
        MaterialDocument stored = material;
        stored.albedoMap     = MakeRelativePath(stored.albedoMap, directory.string());
        stored.normalMap     = MakeRelativePath(stored.normalMap, directory.string());
        stored.metalRoughMap = MakeRelativePath(stored.metalRoughMap, directory.string());
        stored.heightMap     = MakeRelativePath(stored.heightMap, directory.string());
        stored.shaderPath    = MakeRelativePath(stored.shaderPath, directory.string());
        out << ToJson(stored);
        out.flush();
        if (!out.good()) {
            out.close();
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            if (error) *error = "Could not finish writing the material file.";
            return false;
        }
        out.close();
        if (!ReplaceFileAtomically(temporary, filePath, error)) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
        if (writtenPath) *writtenPath = filePath.string();
        if (error) error->clear();
        return true;
    } catch (const std::exception& ex) {
        if (error) *error = ex.what();
        return false;
    }
}

bool LoadMaterialFile(const std::string& path, MaterialDocument* material, std::string* error) {
    if (!material) {
        if (error) *error = "Material output pointer was null.";
        return false;
    }

    std::ifstream in(path);
    if (!in) {
        if (error) *error = "Could not open material file for reading.";
        return false;
    }

    std::ostringstream out;
    out << in.rdbuf();
    const std::string text = out.str();

    std::string schema;
    if (!FindString(text, "schema", &schema) || schema != "3DGEngine.PbrMaterial") {
        if (error) *error = "Material file has an unknown schema.";
        return false;
    }

    float versionValue = 1.0f;
    FindFloat(text, "version", &versionValue);
    const int version = static_cast<int>(versionValue);
    if (version < 1 || version > 4 || versionValue != static_cast<float>(version)) {
        if (error) *error = "Material file uses an unsupported version.";
        return false;
    }

    // Scope the top-level scalar/vector reads to the text BEFORE the "maps" object,
    // so a similarly-named key inside "maps" or "engineMapping" can never be picked
    // up by a first-occurrence scan (hardening against key collisions).
    const std::size_t mapsStart = text.find("\"maps\"");
    const std::string top = (mapsStart == std::string::npos) ? text : text.substr(0, mapsStart);

    MaterialDocument loaded;
    std::array<float, 3> runtimeEmissive{};
    if (!FindString(top, "name", &loaded.name) ||
        !FindVec3(top, "albedo", &loaded.albedo) ||
        !FindFloat(top, "metallic", &loaded.metallic) ||
        !FindFloat(top, "roughness", &loaded.roughness) ||
        !FindFloat(top, "ao", &loaded.ao) ||
        !FindVec3(top, "emissive", &runtimeEmissive)) {
        if (error) *error = "Material file has missing or malformed PBR properties.";
        return false;
    }
    if (version >= 2) {
        if (!FindVec3(top, "emissiveColor", &loaded.emissive) ||
            !FindFloat(top, "emissiveStrength", &loaded.emissiveStrength)) {
            if (error) *error = "Material file is missing version 2 emissive authoring data.";
            return false;
        }
    } else {
        // Version 1 stored only the final HDR emissive value. Keep it unchanged
        // with a unit multiplier; the UI no longer clamps loaded HDR colours.
        loaded.emissive = runtimeEmissive;
        loaded.emissiveStrength = 1.0f;
    }
    if (version >= 3) {
        float blendMode = 0.0f;
        if (!FindFloat(top, "blendMode", &blendMode) || !FindFloat(top, "opacity", &loaded.opacity) ||
            !FindFloat(top, "alphaCutoff", &loaded.alphaCutoff) || !FindVec2(top, "uvScale", &loaded.uvScale) ||
            !FindVec2(top, "uvOffset", &loaded.uvOffset) || !FindFloat(top, "uvRotation", &loaded.uvRotation) ||
            !FindFloat(top, "normalStrength", &loaded.normalStrength) || !FindFloat(top, "heightScale", &loaded.heightScale) ||
            !FindFloat(top, "clearcoat", &loaded.clearcoat) || !FindFloat(top, "clearcoatRoughness", &loaded.clearcoatRoughness) ||
            !FindFloat(top, "transmission", &loaded.transmission) || !FindFloat(top, "ior", &loaded.ior) ||
            !FindFloat(top, "thickness", &loaded.thickness) || !FindFloat(top, "anisotropy", &loaded.anisotropy) ||
            !FindFloat(top, "anisotropyRotation", &loaded.anisotropyRotation) ||
            !FindVec3(top, "sheenColor", &loaded.sheenColor) || !FindFloat(top, "sheenRoughness", &loaded.sheenRoughness) ||
            !FindFloat(top, "specularLevel", &loaded.specularLevel) || !FindFloat(top, "subsurface", &loaded.subsurface) ||
            !FindVec3(top, "subsurfaceColor", &loaded.subsurfaceColor)) {
            if (error) *error = "Material file has missing or malformed version 3 properties.";
            return false;
        }
        loaded.blendMode = static_cast<int>(blendMode);
        if (blendMode != static_cast<float>(loaded.blendMode) || loaded.blendMode < 0 || loaded.blendMode > 2) {
            if (error) *error = "Material file has an invalid blend mode.";
            return false;
        }
    }
    if (version >= 4) {
        FindString(top, "shader", &loaded.shaderPath);
        std::size_t cursor = 0;
        while ((cursor = text.find("\"parameterName\"", cursor)) != std::string::npos) {
            ShaderParameterDocument parameter;
            if (!FindStringFrom(text, "parameterName", cursor, &parameter.name)
                || !FindStringFrom(text, "parameterValue", cursor, &parameter.value)) break;
            const std::size_t typeKey = text.find("\"parameterType\"", cursor);
            const std::size_t colon = typeKey == std::string::npos
                ? std::string::npos : text.find(':', typeKey);
            if (colon == std::string::npos) break;
            parameter.type = static_cast<int>(std::strtol(text.c_str() + colon + 1, nullptr, 10));
            loaded.shaderParameters.push_back(std::move(parameter));
            cursor = colon + 1;
        }
    }

    if (mapsStart != std::string::npos) {
        FindStringFrom(text, "albedo", mapsStart, &loaded.albedoMap);
        FindStringFrom(text, "normal", mapsStart, &loaded.normalMap);
        FindStringFrom(text, "metalRough", mapsStart, &loaded.metalRoughMap);
        FindStringFrom(text, "height", mapsStart, &loaded.heightMap);
    }

    // Stored paths are relative to the material file; resolve them back to absolute
    // so the loaded material can open its textures regardless of the CWD.
    const std::string baseDir = std::filesystem::path(path).parent_path().string();
    loaded.albedoMap     = ResolveRelativePath(loaded.albedoMap, baseDir);
    loaded.normalMap     = ResolveRelativePath(loaded.normalMap, baseDir);
    loaded.metalRoughMap = ResolveRelativePath(loaded.metalRoughMap, baseDir);
    loaded.heightMap     = ResolveRelativePath(loaded.heightMap, baseDir);
    loaded.shaderPath    = ResolveRelativePath(loaded.shaderPath, baseDir);

    *material = loaded;
    if (error) error->clear();
    return true;
}

} // namespace material_maker
