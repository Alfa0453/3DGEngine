#include "MaterialMaker/MaterialDocument.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

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

std::string Vec3Cpp(const std::array<float, 3>& value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(4)
        << "glm::vec3(" << value[0] << "f, " << value[1] << "f, " << value[2] << "f)";
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

    std::string raw;
    bool escaped = false;
    for (++pos; pos < text.size(); ++pos) {
        const char c = text[pos];
        if (!escaped && c == '"') {
            if (value) {
                *value = raw;
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

    char* end = nullptr;
    const char* start = text.c_str() + pos + 1;
    const float x = std::strtof(start, &end);
    if (end == start) {
        return false;
    }
    const float y = std::strtof(end + 1, &end);
    const float z = std::strtof(end + 1, &end);
    if (value) {
        *value = {x, y, z};
    }
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
    out << "  \"version\": 1,\n";
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
    out << "  \"maps\": {\n";
    out << "    \"albedo\": \"" << EscapeJson(material.albedoMap) << "\",\n";
    out << "    \"normal\": \"" << EscapeJson(material.normalMap) << "\",\n";
    out << "    \"metalRough\": \"" << EscapeJson(material.metalRoughMap) << "\"\n";
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
    return out.str();
}

bool SaveMaterialFile(const MaterialDocument& material, const std::string& outputDirectory, std::string* writtenPath, std::string* error) {
    try {
        const std::filesystem::path directory(outputDirectory);
        std::filesystem::create_directories(directory);

        const std::filesystem::path filePath = directory / (SanitizeFileStem(material.name) + ".3dgmat");
        std::ofstream out(filePath);
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
        out << ToJson(stored);
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

    // Scope the top-level scalar/vector reads to the text BEFORE the "maps" object,
    // so a similarly-named key inside "maps" or "engineMapping" can never be picked
    // up by a first-occurrence scan (hardening against key collisions).
    const std::size_t mapsStart = text.find("\"maps\"");
    const std::string top = (mapsStart == std::string::npos) ? text : text.substr(0, mapsStart);

    MaterialDocument loaded;
    FindString(top, "name", &loaded.name);
    FindVec3(top, "albedo", &loaded.albedo);
    FindFloat(top, "metallic", &loaded.metallic);
    FindFloat(top, "roughness", &loaded.roughness);
    FindFloat(top, "ao", &loaded.ao);
    FindVec3(top, "emissive", &loaded.emissive);

    if (mapsStart != std::string::npos) {
        FindStringFrom(text, "albedo", mapsStart, &loaded.albedoMap);
        FindStringFrom(text, "normal", mapsStart, &loaded.normalMap);
        FindStringFrom(text, "metalRough", mapsStart, &loaded.metalRoughMap);
    }

    // Stored paths are relative to the material file; resolve them back to absolute
    // so the loaded material can open its textures regardless of the CWD.
    const std::string baseDir = std::filesystem::path(path).parent_path().string();
    loaded.albedoMap     = ResolveRelativePath(loaded.albedoMap, baseDir);
    loaded.normalMap     = ResolveRelativePath(loaded.normalMap, baseDir);
    loaded.metalRoughMap = ResolveRelativePath(loaded.metalRoughMap, baseDir);

    *material = loaded;
    if (error) error->clear();
    return true;
}

} // namespace material_maker
