#include "engine/assets/ForgeMaterialImporter.h"

#include "engine/assets/AssetRegistry.h"
#include "engine/assets/StaticMeshAsset.h"
#include "engine/assets/TextureAsset.h"
#include "engine/graphics/ImageDecode.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace engine {
namespace {

constexpr char kPackageMagic[8] = {'3', 'D', 'G', 'T', 'E', 'X', '1', '\0'};
constexpr const char* kPackageSchema = "3dg.texture-container/1.0";
constexpr std::uint32_t kPackageAlignment = 16;
constexpr std::uint64_t kMaximumHeaderBytes = 8ull * 1024ull * 1024ull;
constexpr std::uint64_t kMaximumRecordBytes = 512ull * 1024ull * 1024ull;
constexpr std::uint64_t kMaximumTotalBytes = 1024ull * 1024ull * 1024ull;
constexpr std::size_t kMaximumRecords = 4096;
constexpr std::uint32_t kMaximumMipLevels = 32;

void SetError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

std::uint32_t ReadLe32(const unsigned char* bytes) {
    return static_cast<std::uint32_t>(bytes[0])
        | (static_cast<std::uint32_t>(bytes[1]) << 8u)
        | (static_cast<std::uint32_t>(bytes[2]) << 16u)
        | (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

std::uint32_t Crc32(const unsigned char* data, std::size_t size) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> values{};
        for (std::uint32_t index = 0; index < values.size(); ++index) {
            std::uint32_t value = index;
            for (int bit = 0; bit < 8; ++bit)
                value = (value >> 1u) ^ (0xedb88320u & (0u - (value & 1u)));
            values[index] = value;
        }
        return values;
    }();
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < size; ++i)
        crc = table[(crc ^ data[i]) & 0xffu] ^ (crc >> 8u);
    return crc ^ 0xffffffffu;
}

std::uint32_t ParseHex32(const std::string& text) {
    if (text.size() != 8) throw std::runtime_error("Texture record CRC-32 is invalid.");
    std::uint32_t result = 0;
    for (char c : text) {
        result <<= 4u;
        if (c >= '0' && c <= '9') result |= static_cast<std::uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') result |= static_cast<std::uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') result |= static_cast<std::uint32_t>(c - 'A' + 10);
        else throw std::runtime_error("Texture record CRC-32 is invalid.");
    }
    return result;
}

struct JsonValue {
    enum class Type { Null, Boolean, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue> object;
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : m_text(text) {}

    JsonValue Parse() {
        JsonValue result = ParseValue(0);
        SkipSpace();
        if (m_position != m_text.size()) Fail("trailing data");
        return result;
    }

private:
    void SkipSpace() {
        while (m_position < m_text.size()
               && std::isspace(static_cast<unsigned char>(m_text[m_position])))
            ++m_position;
    }

    [[noreturn]] void Fail(const char* detail) const {
        throw std::runtime_error(std::string("Texture package JSON has ") + detail + ".");
    }

    bool Consume(char expected) {
        SkipSpace();
        if (m_position >= m_text.size() || m_text[m_position] != expected) return false;
        ++m_position;
        return true;
    }

    JsonValue ParseValue(unsigned depth) {
        if (depth > 64) Fail("excessive nesting");
        SkipSpace();
        if (m_position >= m_text.size()) Fail("a truncated value");
        const char c = m_text[m_position];
        if (c == '{') return ParseObject(depth + 1);
        if (c == '[') return ParseArray(depth + 1);
        if (c == '"') {
            JsonValue result;
            result.type = JsonValue::Type::String;
            result.string = ParseString();
            return result;
        }
        if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber();
        if (m_text.compare(m_position, 4, "true") == 0) {
            m_position += 4;
            JsonValue result;
            result.type = JsonValue::Type::Boolean;
            result.boolean = true;
            return result;
        }
        if (m_text.compare(m_position, 5, "false") == 0) {
            m_position += 5;
            JsonValue result;
            result.type = JsonValue::Type::Boolean;
            return result;
        }
        if (m_text.compare(m_position, 4, "null") == 0) {
            m_position += 4;
            return {};
        }
        Fail("an invalid value");
    }

    static void AppendUtf8(std::string* output, std::uint32_t codepoint) {
        if (codepoint <= 0x7fu) output->push_back(static_cast<char>(codepoint));
        else if (codepoint <= 0x7ffu) {
            output->push_back(static_cast<char>(0xc0u | (codepoint >> 6u)));
            output->push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
        } else if (codepoint <= 0xffffu && !(codepoint >= 0xd800u && codepoint <= 0xdfffu)) {
            output->push_back(static_cast<char>(0xe0u | (codepoint >> 12u)));
            output->push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
            output->push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
        } else {
            throw std::runtime_error("Texture package JSON has an unsupported Unicode escape.");
        }
    }

    std::uint32_t ParseHex4() {
        if (m_position + 4 > m_text.size()) Fail("a truncated Unicode escape");
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = m_text[m_position++];
            value <<= 4u;
            if (c >= '0' && c <= '9') value |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<std::uint32_t>(c - 'A' + 10);
            else Fail("an invalid Unicode escape");
        }
        return value;
    }

    std::string ParseString() {
        if (!Consume('"')) Fail("an invalid string");
        std::string result;
        while (m_position < m_text.size()) {
            const unsigned char c = static_cast<unsigned char>(m_text[m_position++]);
            if (c == '"') return result;
            if (c < 0x20u) Fail("an unescaped control character");
            if (c != '\\') {
                result.push_back(static_cast<char>(c));
                continue;
            }
            if (m_position >= m_text.size()) Fail("a truncated escape");
            switch (m_text[m_position++]) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u': AppendUtf8(&result, ParseHex4()); break;
                default: Fail("an invalid escape");
            }
        }
        Fail("an unterminated string");
    }

    JsonValue ParseNumber() {
        SkipSpace();
        const char* begin = m_text.c_str() + m_position;
        char* end = nullptr;
        const double value = std::strtod(begin, &end);
        if (end == begin || !std::isfinite(value)) Fail("an invalid number");
        m_position += static_cast<std::size_t>(end - begin);
        JsonValue result;
        result.type = JsonValue::Type::Number;
        result.number = value;
        return result;
    }

    JsonValue ParseArray(unsigned depth) {
        if (!Consume('[')) Fail("an invalid array");
        JsonValue result;
        result.type = JsonValue::Type::Array;
        if (Consume(']')) return result;
        for (;;) {
            result.array.push_back(ParseValue(depth));
            if (Consume(']')) return result;
            if (!Consume(',')) Fail("an invalid array separator");
        }
    }

    JsonValue ParseObject(unsigned depth) {
        if (!Consume('{')) Fail("an invalid object");
        JsonValue result;
        result.type = JsonValue::Type::Object;
        if (Consume('}')) return result;
        for (;;) {
            SkipSpace();
            if (m_position >= m_text.size() || m_text[m_position] != '"')
                Fail("an invalid object key");
            std::string key = ParseString();
            if (!Consume(':')) Fail("an invalid object separator");
            if (!result.object.emplace(std::move(key), ParseValue(depth)).second)
                Fail("a duplicate object key");
            if (Consume('}')) return result;
            if (!Consume(',')) Fail("an invalid object separator");
        }
    }

    const std::string& m_text;
    std::size_t m_position = 0;
};

const JsonValue& RequireField(const JsonValue& object, const char* key,
                              JsonValue::Type type) {
    if (object.type != JsonValue::Type::Object)
        throw std::runtime_error("Texture package JSON root or record is not an object.");
    const auto found = object.object.find(key);
    if (found == object.object.end() || found->second.type != type)
        throw std::runtime_error(std::string("Texture package field '") + key + "' is missing or invalid.");
    return found->second;
}

const JsonValue* OptionalField(const JsonValue& object, const char* key,
                               JsonValue::Type type) {
    if (object.type != JsonValue::Type::Object) return nullptr;
    const auto found = object.object.find(key);
    if (found == object.object.end()) return nullptr;
    if (found->second.type != type)
        throw std::runtime_error(std::string("Texture package field '") + key + "' is invalid.");
    return &found->second;
}

std::uint64_t UnsignedField(const JsonValue& object, const char* key) {
    const double value = RequireField(object, key, JsonValue::Type::Number).number;
    if (value < 0.0 || value > static_cast<double>(std::numeric_limits<std::uint64_t>::max())
        || std::floor(value) != value)
        throw std::runtime_error(std::string("Texture package field '") + key + "' is not an unsigned integer.");
    return static_cast<std::uint64_t>(value);
}

struct PendingRecord {
    ForgeTextureRecord record;
    std::string byteOrder;
    std::string encoding;
    std::uint64_t offset = 0;
    std::uint64_t encodedBytes = 0;
    std::uint64_t rawBytes = 0;
    std::uint32_t crc32 = 0;
};

bool ReadExact(std::ifstream& input, std::uint64_t offset, std::size_t size,
               std::vector<unsigned char>* output) {
    output->resize(size);
    input.clear();
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) return false;
    input.read(reinterpret_cast<char*>(output->data()), static_cast<std::streamsize>(size));
    return input.good() || input.gcount() == static_cast<std::streamsize>(size);
}

std::string SanitizeStem(const std::string& name) {
    std::string result;
    for (unsigned char c : name) {
        if (std::isalnum(c) || c == '_' || c == '-') result.push_back(static_cast<char>(c));
        else if (std::isspace(c) && !result.empty() && result.back() != '_') result.push_back('_');
    }
    while (!result.empty() && result.back() == '_') result.pop_back();
    return result.empty() ? "material" : result;
}

std::string EscapeJson(const std::string& value) {
    std::ostringstream output;
    for (unsigned char c : value) {
        switch (c) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (c < 0x20u) {
                    output << "\\u" << std::hex << std::setw(4)
                           << std::setfill('0') << static_cast<unsigned>(c)
                           << std::dec;
                } else output << static_cast<char>(c);
        }
    }
    return output.str();
}

bool IsInside(const std::filesystem::path& candidate,
              const std::filesystem::path& root,
              std::filesystem::path* relative,
              std::string* error) {
    std::error_code ec;
    const std::filesystem::path absoluteCandidate =
        std::filesystem::absolute(candidate, ec).lexically_normal();
    if (ec) {
        SetError(error, "Could not resolve the material destination: " + ec.message());
        return false;
    }
    const std::filesystem::path absoluteRoot =
        std::filesystem::absolute(root, ec).lexically_normal();
    if (ec) {
        SetError(error, "Could not resolve the Content folder: " + ec.message());
        return false;
    }
    const std::filesystem::path result = absoluteCandidate.lexically_relative(absoluteRoot);
    if (result.empty() || result.is_absolute()) {
        SetError(error, "Forge material destination must be inside Content.");
        return false;
    }
    for (const std::filesystem::path& part : result) {
        if (part == "..") {
            SetError(error, "Forge material destination must be inside Content.");
            return false;
        }
    }
    if (relative) *relative = result;
    return true;
}

std::uint8_t ReadSample8(const ForgeTextureRecord& record,
                         std::uint32_t x, std::uint32_t y,
                         std::uint32_t channel = 0) {
    const std::uint64_t sample =
        (static_cast<std::uint64_t>(y) * record.width + x) * record.channels + channel;
    if (record.bitDepth == 8) return record.pixels[static_cast<std::size_t>(sample)];
    const std::size_t byte = static_cast<std::size_t>(sample * 2u);
    const std::uint32_t value = static_cast<std::uint32_t>(record.pixels[byte])
        | (static_cast<std::uint32_t>(record.pixels[byte + 1u]) << 8u);
    return static_cast<std::uint8_t>((value + 128u) / 257u);
}

std::vector<std::uint8_t> ToRgba(const ForgeTextureRecord& record,
                                 bool invertGreen) {
    std::vector<std::uint8_t> rgba(
        static_cast<std::size_t>(record.width) * record.height * 4u);
    for (std::uint32_t y = 0; y < record.height; ++y) {
        const std::uint32_t sourceY = record.height - 1u - y;
        for (std::uint32_t x = 0; x < record.width; ++x) {
            const std::size_t destination =
                (static_cast<std::size_t>(y) * record.width + x) * 4u;
            if (record.channels == 1) {
                const std::uint8_t value = ReadSample8(record, x, sourceY);
                rgba[destination] = value;
                rgba[destination + 1u] = value;
                rgba[destination + 2u] = value;
            } else {
                rgba[destination] = ReadSample8(record, x, sourceY, 0);
                const std::uint8_t green = ReadSample8(record, x, sourceY, 1);
                rgba[destination + 1u] = invertGreen
                    ? static_cast<std::uint8_t>(255u - green) : green;
                rgba[destination + 2u] = ReadSample8(record, x, sourceY, 2);
            }
            rgba[destination + 3u] = 255u;
        }
    }
    return rgba;
}

std::vector<std::uint8_t> BuildOrm(const ForgeTextureRecord& ao,
                                   const ForgeTextureRecord& roughness,
                                   const ForgeTextureRecord& metallic) {
    if (ao.width != roughness.width || ao.height != roughness.height
        || ao.width != metallic.width || ao.height != metallic.height)
        throw std::runtime_error("Forge AO, roughness, and metallic maps have different dimensions.");
    std::vector<std::uint8_t> rgba(
        static_cast<std::size_t>(ao.width) * ao.height * 4u);
    for (std::uint32_t y = 0; y < ao.height; ++y) {
        const std::uint32_t sourceY = ao.height - 1u - y;
        for (std::uint32_t x = 0; x < ao.width; ++x) {
            const std::size_t destination =
                (static_cast<std::size_t>(y) * ao.width + x) * 4u;
            rgba[destination] = ReadSample8(ao, x, sourceY);
            rgba[destination + 1u] = ReadSample8(roughness, x, sourceY);
            rgba[destination + 2u] = ReadSample8(metallic, x, sourceY);
            rgba[destination + 3u] = 255u;
        }
    }
    return rgba;
}

std::vector<TextureMipData> ConvertMipChain(
    const ForgeTexturePackage& package, const std::string& name,
    bool invertGreen) {
    std::vector<TextureMipData> result;
    for (std::uint32_t level = 1;; ++level) {
        const ForgeTextureRecord* record = package.Find(name, level);
        if (!record) break;
        TextureMipData mip;
        mip.width = record->width;
        mip.height = record->height;
        mip.rgba = ToRgba(*record, invertGreen);
        result.push_back(std::move(mip));
    }
    return result;
}

std::vector<TextureMipData> BuildOrmMipChain(
    const ForgeTexturePackage& package) {
    std::vector<TextureMipData> result;
    for (std::uint32_t level = 1;; ++level) {
        const ForgeTextureRecord* ao = package.Find("ambient_occlusion", level);
        const ForgeTextureRecord* roughness = package.Find("roughness", level);
        const ForgeTextureRecord* metallic = package.Find("metallic", level);
        if (!ao && !roughness && !metallic) break;
        if (!ao || !roughness || !metallic)
            throw std::runtime_error("Forge ORM source mip chains have different lengths.");
        TextureMipData mip;
        mip.width = ao->width;
        mip.height = ao->height;
        mip.rgba = BuildOrm(*ao, *roughness, *metallic);
        result.push_back(std::move(mip));
    }
    return result;
}

bool SaveCookedTexture(const std::filesystem::path& path,
                       const std::vector<std::uint8_t>& rgba,
                       std::vector<TextureMipData> mipmaps,
                       std::uint32_t width, std::uint32_t height,
                       bool srgb, std::uint64_t sourceHash,
                       AssetHandle* id, std::string* error) {
    TextureAssetData asset;
    TextureAssetData previous;
    std::string ignored;
    if (LoadTextureAsset(path.string(), &previous, &ignored))
        asset.header.id = previous.header.id;
    if (!asset.header.id.Valid()) asset.header.id = AssetHandle::Generate();
    asset.header.type = AssetType::Texture;
    asset.header.importerVersion = kForgeMaterialImporterVersion;
    asset.header.sourceHash = sourceHash;
    asset.width = width;
    asset.height = height;
    asset.smooth = true;
    asset.srgb = srgb;
    asset.rgba = rgba;
    asset.mipmaps = std::move(mipmaps);
    if (!SaveTextureAsset(path.string(), std::move(asset), error)) return false;
    *id = LoadTextureAsset(path.string(), &previous, &ignored)
        ? previous.header.id : AssetHandle{};
    if (!id->Valid()) {
        SetError(error, "Cooked texture identity could not be verified.");
        return false;
    }
    return true;
}

bool ReadExistingMaterialId(const std::filesystem::path& path,
                            AssetHandle* id) {
    std::ifstream input(path);
    std::string magic;
    int version = 0;
    std::string text;
    return input >> magic >> version >> text
        && magic == "3DG_MATERIAL" && AssetHandle::Parse(text, id);
}

bool WriteMaterial(const std::filesystem::path& path,
                   const std::string& name,
                   AssetHandle materialId,
                   const ForgeMaterialImportResult& imported,
                   std::string* error) {
    const std::filesystem::path temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        SetError(error, "Could not open cooked material for writing.");
        return false;
    }
    auto fileName = [](const std::string& value) {
        return std::filesystem::path(value).filename().generic_string();
    };
    output << "3DG_MATERIAL 5 " << materialId.ToString() << '\n';
    output << "{\n"
           << "  \"schema\": \"3DGEngine.PbrMaterial\",\n"
           << "  \"version\": 5,\n"
           << "  \"assetId\": \"" << materialId.ToString() << "\",\n"
           << "  \"name\": \"" << EscapeJson(name) << "\",\n"
           << "  \"albedo\": [1.0000, 1.0000, 1.0000],\n"
           << "  \"metallic\": 1.0000,\n"
           << "  \"roughness\": 1.0000,\n"
           << "  \"ao\": 1.0000,\n"
           << "  \"emissive\": [0.0000, 0.0000, 0.0000],\n"
           << "  \"emissiveColor\": [0.0000, 0.0000, 0.0000],\n"
           << "  \"emissiveStrength\": 1.0000,\n"
           << "  \"blendMode\": 0,\n"
           << "  \"opacity\": 1.0000,\n"
           << "  \"alphaCutoff\": 0.5000,\n"
           << "  \"uvScale\": [1.0000, 1.0000],\n"
           << "  \"uvOffset\": [0.0000, 0.0000],\n"
           << "  \"uvRotation\": 0.0000,\n"
           << "  \"worldSpaceUv\": 0,\n"
           << "  \"normalStrength\": 1.0000,\n"
           << "  \"heightScale\": 0.0500,\n"
           << "  \"clearcoat\": 0.0000,\n"
           << "  \"clearcoatRoughness\": 0.1000,\n"
           << "  \"transmission\": 0.0000,\n"
           << "  \"ior\": 1.5000,\n"
           << "  \"thickness\": 0.0000,\n"
           << "  \"anisotropy\": 0.0000,\n"
           << "  \"anisotropyRotation\": 0.0000,\n"
           << "  \"sheenColor\": [0.0000, 0.0000, 0.0000],\n"
           << "  \"sheenRoughness\": 0.5000,\n"
           << "  \"specularLevel\": 0.5000,\n"
           << "  \"subsurface\": 0.0000,\n"
           << "  \"subsurfaceColor\": [1.0000, 1.0000, 1.0000],\n"
           << "  \"shader\": \"\",\n"
           << "  \"shaderAssetId\": \"00000000000000000000000000000000\",\n"
           << "  \"shaderParameters\": [],\n"
           << "  \"maps\": {\n"
           << "    \"albedo\": \"" << EscapeJson(fileName(imported.albedoMapPath)) << "\",\n"
           << "    \"normal\": \"" << EscapeJson(fileName(imported.normalMapPath)) << "\",\n"
           << "    \"metalRough\": \"" << EscapeJson(fileName(imported.metalRoughMapPath)) << "\",\n"
           << "    \"height\": \"" << EscapeJson(fileName(imported.heightMapPath)) << "\"\n"
           << "  },\n"
           << "  \"mapAssetIds\": {\n"
           << "    \"albedoAssetId\": \"" << imported.albedoMapId.ToString() << "\",\n"
           << "    \"normalAssetId\": \"" << imported.normalMapId.ToString() << "\",\n"
           << "    \"metalRoughAssetId\": \"" << imported.metalRoughMapId.ToString() << "\",\n"
           << "    \"heightAssetId\": \"" << imported.heightMapId.ToString() << "\"\n"
           << "  },\n"
           << "  \"engineMapping\": {\n"
           << "    \"component\": \"engine::ecs::PbrMaterial\",\n"
           << "    \"metalRoughMap\": \"glTF ORM convention: G = roughness, B = metallic\"\n"
           << "  }\n"
           << "}\n";
    std::vector<AssetHandle> dependencies{
        imported.albedoMapId, imported.normalMapId,
        imported.metalRoughMapId, imported.heightMapId};
    output << "ASSET_DEPS " << dependencies.size();
    for (AssetHandle dependency : dependencies)
        output << ' ' << dependency.ToString();
    output << '\n';
    output.close();
    if (!output) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        SetError(error, "Could not finish writing cooked material.");
        return false;
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        SetError(error, "Could not commit cooked material: " + ec.message());
        return false;
    }
    return true;
}

bool RegisterCookedAsset(AssetRegistry* registry,
                         const std::filesystem::path& path,
                         const std::filesystem::path& contentRoot,
                         AssetHandle id, AssetType type,
                         const std::string& sourcePath,
                         std::uint64_t sourceHash,
                         std::vector<AssetHandle> dependencies,
                         std::string* error) {
    std::filesystem::path relative;
    if (!IsInside(path, contentRoot, &relative, error)) return false;
    AssetRegistryEntry entry;
    entry.id = id;
    entry.type = type;
    entry.virtualPath = AssetRegistry::NormalizeVirtualPath(relative.generic_string());
    entry.sourcePath = sourcePath;
    entry.sourceHash = sourceHash;
    entry.importerVersion = kForgeMaterialImporterVersion;
    entry.dependencies = std::move(dependencies);
    return registry->Register(std::move(entry), error);
}

} // namespace

const ForgeTextureRecord* ForgeTexturePackage::Find(
    const std::string& name, std::uint32_t level) const {
    for (const ForgeTextureRecord& record : records)
        if (record.name == name && record.level == level) return &record;
    return nullptr;
}

bool LoadForgeTexturePackage(const std::string& path,
                             ForgeTexturePackage* package,
                             std::string* error) {
    if (!package) {
        SetError(error, "Forge texture package output is null.");
        return false;
    }
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Could not open Forge texture package: " + path);
        std::error_code ec;
        const std::uint64_t fileSize = std::filesystem::file_size(path, ec);
        if (ec || fileSize < 20u)
            throw std::runtime_error("Forge texture package is truncated.");

        unsigned char prefix[20]{};
        input.read(reinterpret_cast<char*>(prefix), sizeof(prefix));
        if (!input || !std::equal(std::begin(kPackageMagic), std::end(kPackageMagic), prefix))
            throw std::runtime_error("File is not a Material Forge texture package.");
        const std::uint32_t headerLength = ReadLe32(prefix + 8);
        const std::uint32_t headerCrc = ReadLe32(prefix + 12);
        const std::uint32_t payloadOffset = ReadLe32(prefix + 16);
        const std::uint64_t headerEnd = 20ull + headerLength;
        if (headerLength == 0 || headerLength > kMaximumHeaderBytes
            || headerEnd > fileSize || payloadOffset < headerEnd
            || payloadOffset > fileSize || payloadOffset % kPackageAlignment != 0)
            throw std::runtime_error("Forge texture package header bounds are invalid.");

        std::vector<unsigned char> headerBytes;
        if (!ReadExact(input, 20u, headerLength, &headerBytes))
            throw std::runtime_error("Forge texture package header is truncated.");
        if (Crc32(headerBytes.data(), headerBytes.size()) != headerCrc)
            throw std::runtime_error("Forge texture package header checksum failed.");
        std::vector<unsigned char> padding;
        if (!ReadExact(input, headerEnd,
                       static_cast<std::size_t>(payloadOffset - headerEnd), &padding)
            || std::any_of(padding.begin(), padding.end(), [](unsigned char value) { return value != 0; }))
            throw std::runtime_error("Forge texture package header padding is invalid.");

        const std::string headerText(headerBytes.begin(), headerBytes.end());
        const JsonValue root = JsonParser(headerText).Parse();
        if (RequireField(root, "schema", JsonValue::Type::String).string != kPackageSchema
            || UnsignedField(root, "alignment") != kPackageAlignment)
            throw std::runtime_error("Forge texture package schema is unsupported.");
        ForgeTexturePackage loaded;
        loaded.materialName = RequireField(root, "material", JsonValue::Type::String).string;
        if (loaded.materialName.empty())
            throw std::runtime_error("Forge texture package material name is empty.");
        if (const JsonValue* metadata = OptionalField(root, "metadata", JsonValue::Type::Object)) {
            if (const JsonValue* normalY = OptionalField(*metadata, "normal_y", JsonValue::Type::String))
                loaded.normalY = normalY->string;
            if (const JsonValue* tileable = OptionalField(*metadata, "tileable", JsonValue::Type::Boolean))
                loaded.tileable = tileable->boolean;
        }
        if (loaded.normalY != "positive" && loaded.normalY != "negative")
            throw std::runtime_error("Forge texture package normal convention is invalid.");

        const JsonValue& jsonRecords = RequireField(root, "records", JsonValue::Type::Array);
        if (jsonRecords.array.empty() || jsonRecords.array.size() > kMaximumRecords)
            throw std::runtime_error("Forge texture package record table is invalid.");
        std::vector<PendingRecord> pending;
        pending.reserve(jsonRecords.array.size());
        std::set<std::pair<std::string, std::uint32_t>> identities;
        std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
        std::uint64_t totalRaw = 0;
        const std::uint64_t payloadBytes = fileSize - payloadOffset;
        for (const JsonValue& jsonRecord : jsonRecords.array) {
            PendingRecord item;
            item.record.name = RequireField(jsonRecord, "name", JsonValue::Type::String).string;
            const std::uint64_t level = UnsignedField(jsonRecord, "level");
            const std::uint64_t width = UnsignedField(jsonRecord, "width");
            const std::uint64_t height = UnsignedField(jsonRecord, "height");
            const std::uint64_t channels = UnsignedField(jsonRecord, "channels");
            const std::uint64_t bitDepth = UnsignedField(jsonRecord, "bit_depth");
            item.byteOrder = RequireField(jsonRecord, "byte_order", JsonValue::Type::String).string;
            item.encoding = RequireField(jsonRecord, "encoding", JsonValue::Type::String).string;
            item.offset = UnsignedField(jsonRecord, "offset");
            item.encodedBytes = UnsignedField(jsonRecord, "encoded_bytes");
            item.rawBytes = UnsignedField(jsonRecord, "raw_bytes");
            item.crc32 = ParseHex32(RequireField(jsonRecord, "crc32", JsonValue::Type::String).string);
            if (item.record.name.empty() || level >= kMaximumMipLevels
                || width == 0 || height == 0
                || width > std::numeric_limits<std::uint32_t>::max()
                || height > std::numeric_limits<std::uint32_t>::max()
                || (channels != 1 && channels != 3)
                || (bitDepth != 8 && bitDepth != 16)
                || (bitDepth == 16 && channels != 1)
                || (bitDepth == 16 && item.byteOrder != "little")
                || (bitDepth == 8 && item.byteOrder != "not_applicable")
                || (item.encoding != "raw" && item.encoding != "zlib")
                || item.offset % kPackageAlignment != 0
                || item.encodedBytes == 0 || item.encodedBytes > kMaximumRecordBytes
                || item.encodedBytes > std::numeric_limits<std::size_t>::max()
                || item.rawBytes > std::numeric_limits<std::size_t>::max()
                || item.offset > payloadBytes
                || item.encodedBytes > payloadBytes - item.offset)
                throw std::runtime_error("Forge texture package record values are invalid.");
            const std::uint64_t bytesPerSample = bitDepth / 8u;
            if (width > kMaximumRecordBytes / height
                || width * height > kMaximumRecordBytes / channels
                || width * height * channels > kMaximumRecordBytes / bytesPerSample)
                throw std::runtime_error("Forge texture package record dimensions are too large.");
            const std::uint64_t expected = width * height * channels * bytesPerSample;
            if (item.rawBytes != expected || item.rawBytes > kMaximumRecordBytes
                || (item.encoding == "raw" && item.encodedBytes != item.rawBytes))
                throw std::runtime_error("Forge texture package record size is invalid.");
            item.record.level = static_cast<std::uint32_t>(level);
            item.record.width = static_cast<std::uint32_t>(width);
            item.record.height = static_cast<std::uint32_t>(height);
            item.record.channels = static_cast<std::uint32_t>(channels);
            item.record.bitDepth = static_cast<std::uint32_t>(bitDepth);
            if (!identities.emplace(item.record.name, item.record.level).second)
                throw std::runtime_error("Forge texture package contains duplicate records.");
            if (totalRaw > kMaximumTotalBytes - item.rawBytes)
                throw std::runtime_error("Forge texture package decoded payload is too large.");
            totalRaw += item.rawBytes;
            ranges.emplace_back(item.offset, item.offset + item.encodedBytes);
            pending.push_back(std::move(item));
        }
        std::sort(ranges.begin(), ranges.end());
        for (std::size_t i = 1; i < ranges.size(); ++i)
            if (ranges[i].first < ranges[i - 1].second)
                throw std::runtime_error("Forge texture package records overlap.");

        std::map<std::string, std::vector<const PendingRecord*>> chains;
        for (const PendingRecord& item : pending) chains[item.record.name].push_back(&item);
        for (auto& chain : chains) {
            std::sort(chain.second.begin(), chain.second.end(),
                [](const PendingRecord* a, const PendingRecord* b) {
                    return a->record.level < b->record.level;
                });
            for (std::size_t i = 0; i < chain.second.size(); ++i) {
                const ForgeTextureRecord& current = chain.second[i]->record;
                if (current.level != i)
                    throw std::runtime_error("Forge texture package has a discontinuous mip chain.");
                if (i == 0) continue;
                const ForgeTextureRecord& previous = chain.second[i - 1]->record;
                if (current.width != std::max(1u, previous.width / 2u)
                    || current.height != std::max(1u, previous.height / 2u)
                    || current.channels != previous.channels
                    || current.bitDepth != previous.bitDepth)
                    throw std::runtime_error("Forge texture package has an invalid mip chain.");
            }
        }

        loaded.records.reserve(pending.size());
        for (PendingRecord& item : pending) {
            std::vector<unsigned char> encoded;
            if (!ReadExact(input, static_cast<std::uint64_t>(payloadOffset) + item.offset,
                           static_cast<std::size_t>(item.encodedBytes), &encoded))
                throw std::runtime_error("Forge texture package payload is truncated.");
            item.record.pixels = item.encoding == "zlib"
                ? image::InflateZlib(encoded.data(), encoded.size(),
                                     static_cast<std::size_t>(item.rawBytes))
                : std::move(encoded);
            if (item.record.pixels.size() != item.rawBytes)
                throw std::runtime_error("Forge texture package payload size is invalid.");
            if (Crc32(item.record.pixels.data(), item.record.pixels.size()) != item.crc32)
                throw std::runtime_error("Forge texture package payload checksum failed.");
            loaded.records.push_back(std::move(item.record));
        }
        *package = std::move(loaded);
        SetError(error, {});
        return true;
    } catch (const std::exception& exception) {
        SetError(error, exception.what());
        return false;
    }
}

bool ImportForgeMaterialPackage(const std::string& sourcePath,
                                const std::string& destinationDirectory,
                                const std::string& contentRoot,
                                AssetRegistry* registry,
                                ForgeMaterialImportResult* result,
                                std::string* error) {
    try {
        ForgeTexturePackage package;
        if (!LoadForgeTexturePackage(sourcePath, &package, error)) return false;
        const ForgeTextureRecord* albedo = package.Find("base_color");
        const ForgeTextureRecord* normal = package.Find("normal");
        const ForgeTextureRecord* ao = package.Find("ambient_occlusion");
        const ForgeTextureRecord* roughness = package.Find("roughness");
        const ForgeTextureRecord* metallic = package.Find("metallic");
        const ForgeTextureRecord* height = package.Find("height");
        if (!height) height = package.Find("height_preview");
        if (!albedo || !normal || !ao || !roughness || !metallic || !height)
            throw std::runtime_error("Forge package is missing one or more required PBR base maps.");
        auto sameDimensions = [&](const ForgeTextureRecord* record) {
            return record->width == albedo->width && record->height == albedo->height;
        };
        if (!sameDimensions(normal) || !sameDimensions(ao) || !sameDimensions(roughness)
            || !sameDimensions(metallic) || !sameDimensions(height))
            throw std::runtime_error("Forge package base maps have different dimensions.");

        const std::filesystem::path destination(destinationDirectory);
        std::filesystem::path ignoredRelative;
        if (!IsInside(destination, contentRoot, &ignoredRelative, error)) return false;
        std::error_code ec;
        std::filesystem::create_directories(destination, ec);
        if (ec) throw std::runtime_error("Could not create Forge material folder: " + ec.message());
        const std::string stem = SanitizeStem(package.materialName);
        ForgeMaterialImportResult imported;
        imported.width = albedo->width;
        imported.height = albedo->height;
        imported.sourceRecordCount = package.records.size();
        imported.albedoMapPath = (destination / (stem + "_basecolor.3dgtex")).string();
        imported.normalMapPath = (destination / (stem + "_normal.3dgtex")).string();
        imported.metalRoughMapPath = (destination / (stem + "_orm.3dgtex")).string();
        imported.heightMapPath = (destination / (stem + "_height.3dgtex")).string();
        imported.materialPath = (destination / (stem + ".3dgmat")).string();

        std::string hashError;
        const std::uint64_t sourceHash = HashAssetSourceFile(sourcePath, &hashError);
        if (sourceHash == 0 && !hashError.empty()) throw std::runtime_error(hashError);
        if (!SaveCookedTexture(imported.albedoMapPath, ToRgba(*albedo, false),
                               ConvertMipChain(package, "base_color", false),
                               albedo->width, albedo->height, true, sourceHash,
                               &imported.albedoMapId, error)
            || !SaveCookedTexture(imported.normalMapPath,
                                  ToRgba(*normal, package.normalY == "negative"),
                                  ConvertMipChain(
                                      package, "normal",
                                      package.normalY == "negative"),
                                  normal->width, normal->height, false, sourceHash,
                                  &imported.normalMapId, error)
            || !SaveCookedTexture(imported.metalRoughMapPath,
                                  BuildOrm(*ao, *roughness, *metallic),
                                  BuildOrmMipChain(package),
                                  ao->width, ao->height, false, sourceHash,
                                  &imported.metalRoughMapId, error)
            || !SaveCookedTexture(imported.heightMapPath, ToRgba(*height, false),
                                  ConvertMipChain(package, height->name, false),
                                  height->width, height->height, false, sourceHash,
                                  &imported.heightMapId, error))
            return false;

        if (!ReadExistingMaterialId(imported.materialPath, &imported.materialId))
            imported.materialId = AssetHandle::Generate();
        if (!WriteMaterial(imported.materialPath, package.materialName,
                           imported.materialId, imported, error))
            return false;

        AssetRegistry updated = registry ? *registry : AssetRegistry{};
        const std::filesystem::path registryPath =
            AssetRegistry::DefaultRegistryPath(contentRoot);
        if (!registry && std::filesystem::exists(registryPath, ec)) {
            if (!updated.Load(registryPath.string(), error)) return false;
        }
        const std::string absoluteSource =
            std::filesystem::absolute(sourcePath, ec).lexically_normal().string();
        if (ec) throw std::runtime_error("Could not resolve Forge package source path: " + ec.message());
        if (!RegisterCookedAsset(&updated, imported.albedoMapPath, contentRoot,
                                 imported.albedoMapId, AssetType::Texture,
                                 absoluteSource, sourceHash, {}, error)
            || !RegisterCookedAsset(&updated, imported.normalMapPath, contentRoot,
                                    imported.normalMapId, AssetType::Texture,
                                    absoluteSource, sourceHash, {}, error)
            || !RegisterCookedAsset(&updated, imported.metalRoughMapPath, contentRoot,
                                    imported.metalRoughMapId, AssetType::Texture,
                                    absoluteSource, sourceHash, {}, error)
            || !RegisterCookedAsset(&updated, imported.heightMapPath, contentRoot,
                                    imported.heightMapId, AssetType::Texture,
                                    absoluteSource, sourceHash, {}, error)
            || !RegisterCookedAsset(
                &updated, imported.materialPath, contentRoot,
                imported.materialId, AssetType::Material,
                absoluteSource, sourceHash,
                {imported.albedoMapId, imported.normalMapId,
                 imported.metalRoughMapId, imported.heightMapId}, error)
            || !updated.Save(registryPath.string(), error))
            return false;
        if (registry) *registry = std::move(updated);
        if (result) *result = std::move(imported);
        SetError(error, {});
        return true;
    } catch (const std::exception& exception) {
        SetError(error, exception.what());
        return false;
    }
}

} // namespace engine
