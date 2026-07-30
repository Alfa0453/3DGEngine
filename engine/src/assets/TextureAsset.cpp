#include "engine/assets/TextureAsset.h"

#include "engine/assets/AssetRegistry.h"
#include "engine/assets/StaticMeshAsset.h"
#include "engine/graphics/ImageDecode.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <type_traits>

namespace engine {
namespace {

constexpr std::uint64_t kMaximumTextureBytes = 1024ull * 1024ull * 1024ull;

void SetError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

template<class T>
bool WriteUnsigned(std::ostream& output, T value) {
    static_assert(std::is_unsigned<T>::value, "unsigned integer required");
    for (std::size_t i = 0; i < sizeof(T); ++i)
        output.put(static_cast<char>((value >> (i * 8u)) & 0xffu));
    return static_cast<bool>(output);
}

template<class T>
bool ReadUnsigned(std::istream& input, T* value) {
    static_assert(std::is_unsigned<T>::value, "unsigned integer required");
    if (!value) return false;
    *value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        const int byte = input.get();
        if (byte == std::char_traits<char>::eof()) return false;
        *value |= static_cast<T>(static_cast<unsigned char>(byte)) << (i * 8u);
    }
    return true;
}

std::string LowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

void FlipRows(std::vector<std::uint8_t>* rgba, std::uint32_t width,
              std::uint32_t height) {
    const std::size_t row = static_cast<std::size_t>(width) * 4u;
    for (std::uint32_t y = 0; y < height / 2u; ++y) {
        std::swap_ranges(
            rgba->begin() + static_cast<std::ptrdiff_t>(y * row),
            rgba->begin() + static_cast<std::ptrdiff_t>((y + 1u) * row),
            rgba->begin()
                + static_cast<std::ptrdiff_t>((height - 1u - y) * row));
    }
}

bool LoadTga(const std::string& path, TextureAssetData* asset,
             std::string* error) {
    std::ifstream input(path, std::ios::binary);
    unsigned char header[18]{};
    input.read(reinterpret_cast<char*>(header), sizeof(header));
    const int idLength = header[0];
    const int imageType = header[2];
    const std::uint32_t width =
        static_cast<std::uint32_t>(header[12] | (header[13] << 8));
    const std::uint32_t height =
        static_cast<std::uint32_t>(header[14] | (header[15] << 8));
    const int depth = header[16];
    if (!input || imageType != 2 || width == 0 || height == 0
        || (depth != 24 && depth != 32)) {
        SetError(error, "Texture import supports uncompressed 24/32-bit TGA.");
        return false;
    }
    const std::uint64_t pixelCount =
        static_cast<std::uint64_t>(width) * height;
    if (pixelCount > kMaximumTextureBytes / 4u) {
        SetError(error, "Texture dimensions are too large.");
        return false;
    }
    input.seekg(idLength, std::ios::cur);
    const std::size_t channels = static_cast<std::size_t>(depth / 8);
    std::vector<std::uint8_t> source(
        static_cast<std::size_t>(pixelCount) * channels);
    input.read(reinterpret_cast<char*>(source.data()),
               static_cast<std::streamsize>(source.size()));
    if (!input) {
        SetError(error, "TGA pixel data is truncated.");
        return false;
    }
    asset->width = width;
    asset->height = height;
    asset->rgba.resize(static_cast<std::size_t>(pixelCount) * 4u);
    const bool topOrigin = (header[17] & 0x20u) != 0;
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint32_t sourceY = topOrigin ? height - 1u - y : y;
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t s =
                (static_cast<std::size_t>(sourceY) * width + x) * channels;
            const std::size_t d =
                (static_cast<std::size_t>(y) * width + x) * 4u;
            asset->rgba[d] = source[s + 2u];
            asset->rgba[d + 1u] = source[s + 1u];
            asset->rgba[d + 2u] = source[s];
            asset->rgba[d + 3u] = channels == 4u ? source[s + 3u] : 255u;
        }
    }
    return true;
}

bool Validate(const TextureAssetData& asset, std::string* error) {
    const std::uint64_t expected =
        static_cast<std::uint64_t>(asset.width) * asset.height * 4u;
    if (asset.header.type != AssetType::Texture || !asset.header.id.Valid()
        || asset.width == 0 || asset.height == 0
        || expected > kMaximumTextureBytes || asset.rgba.size() != expected) {
        SetError(error, "Texture asset dimensions, identity, or payload are invalid.");
        return false;
    }
    return true;
}

} // namespace

bool SaveTextureAsset(const std::string& path, TextureAssetData asset,
                      std::string* error) {
    asset.header.type = AssetType::Texture;
    asset.header.assetVersion = kTextureAssetVersion;
    asset.header.payloadSize = 12u + asset.rgba.size();
    if (!Validate(asset, error)) return false;
    const std::filesystem::path destination(path);
    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
        SetError(error, "Could not create texture asset folder: " + ec.message());
        return false;
    }
    const std::filesystem::path temporary = destination.string() + ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    const std::uint32_t flags =
        (asset.smooth ? 1u : 0u) | (asset.srgb ? 2u : 0u);
    if (!output
        || !WriteNativeAssetHeader(output, asset.header, error)
        || !WriteUnsigned(output, asset.width)
        || !WriteUnsigned(output, asset.height)
        || !WriteUnsigned(output, flags)) {
        output.close();
        std::filesystem::remove(temporary, ec);
        if (error && error->empty()) *error = "Could not write texture asset.";
        return false;
    }
    output.write(reinterpret_cast<const char*>(asset.rgba.data()),
                 static_cast<std::streamsize>(asset.rgba.size()));
    output.close();
    if (!output) {
        std::filesystem::remove(temporary, ec);
        SetError(error, "Could not finish writing texture asset.");
        return false;
    }
    std::filesystem::remove(destination, ec);
    ec.clear();
    std::filesystem::rename(temporary, destination, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        SetError(error, "Could not commit texture asset: " + ec.message());
        return false;
    }
    SetError(error, {});
    return true;
}

bool LoadTextureAsset(const std::string& path, TextureAssetData* asset,
                      std::string* error) {
    if (!asset) {
        SetError(error, "Texture asset output is null.");
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    TextureAssetData loaded;
    std::uint32_t flags = 0;
    if (!input || !ReadNativeAssetHeader(input, &loaded.header, error)
        || loaded.header.type != AssetType::Texture
        || loaded.header.assetVersion != kTextureAssetVersion
        || !ReadUnsigned(input, &loaded.width)
        || !ReadUnsigned(input, &loaded.height)
        || !ReadUnsigned(input, &flags)) {
        if (error && error->empty()) *error = "Texture asset header is invalid.";
        return false;
    }
    const std::uint64_t bytes =
        static_cast<std::uint64_t>(loaded.width) * loaded.height * 4u;
    if (bytes > kMaximumTextureBytes
        || loaded.header.payloadSize != 12u + bytes) {
        SetError(error, "Texture asset payload size is invalid.");
        return false;
    }
    loaded.smooth = (flags & 1u) != 0;
    loaded.srgb = (flags & 2u) != 0;
    loaded.rgba.resize(static_cast<std::size_t>(bytes));
    input.read(reinterpret_cast<char*>(loaded.rgba.data()),
               static_cast<std::streamsize>(loaded.rgba.size()));
    if (!input || !Validate(loaded, error)) return false;
    *asset = std::move(loaded);
    SetError(error, {});
    return true;
}

bool ImportTextureSource(const std::string& sourcePath, TextureAssetData* asset,
                         std::string* error) {
    return ImportTextureSource(
        sourcePath, TextureImportOptions{}, asset, error);
}

bool ImportTextureSource(const std::string& sourcePath,
                         const TextureImportOptions& options,
                         TextureAssetData* asset,
                         std::string* error) {
    if (!asset) {
        SetError(error, "Texture import output is null.");
        return false;
    }
    TextureAssetData imported;
    try {
        const std::string extension = LowerExtension(sourcePath);
        if (extension == ".png" || extension == ".jpg"
            || extension == ".jpeg") {
            image::Image decoded = extension == ".png"
                ? image::DecodePNG(sourcePath) : image::DecodeJPEG(sourcePath);
            imported.width = static_cast<std::uint32_t>(decoded.width);
            imported.height = static_cast<std::uint32_t>(decoded.height);
            imported.rgba = std::move(decoded.rgba);
            FlipRows(&imported.rgba, imported.width, imported.height);
        } else if (extension == ".tga") {
            if (!LoadTga(sourcePath, &imported, error)) return false;
        } else {
            SetError(error, "Texture import supports PNG, JPEG, and TGA sources.");
            return false;
        }
    } catch (const std::exception& exception) {
        SetError(error, exception.what());
        return false;
    }
    imported.header.type = AssetType::Texture;
    imported.header.id = AssetHandle::Generate();
    imported.header.assetVersion = kTextureAssetVersion;
    imported.header.importerVersion = kTextureImporterVersion;
    imported.smooth = options.smooth;
    imported.srgb = options.srgb;
    *asset = std::move(imported);
    SetError(error, {});
    return true;
}

bool ImportTextureToAsset(const std::string& sourcePath,
                          const std::string& destinationPath,
                          const std::string& contentRoot,
                          AssetRegistry* registry,
                          TextureImportResult* result,
                          std::string* error) {
    return ImportTextureToAsset(
        sourcePath, destinationPath, contentRoot, TextureImportOptions{},
        registry, result, error);
}

bool ImportTextureToAsset(const std::string& sourcePath,
                          const std::string& destinationPath,
                          const std::string& contentRoot,
                          const TextureImportOptions& options,
                          AssetRegistry* registry,
                          TextureImportResult* result,
                          std::string* error) {
    TextureAssetData imported;
    if (!ImportTextureSource(sourcePath, options, &imported, error)) return false;
    TextureAssetData previous;
    std::string ignored;
    if (LoadTextureAsset(destinationPath, &previous, &ignored))
        imported.header.id = previous.header.id;
    imported.header.sourceHash = HashAssetSourceFile(sourcePath, error);
    if (imported.header.sourceHash == 0 && error && !error->empty()) return false;
    if (!SaveTextureAsset(destinationPath, imported, error)) return false;

    std::error_code ec;
    const std::filesystem::path relative = std::filesystem::relative(
        destinationPath, contentRoot, ec);
    if (ec || relative.empty() || relative.is_absolute()
        || *relative.begin() == "..") {
        SetError(error, "Texture destination must be inside Content.");
        return false;
    }
    AssetRegistry updated = registry ? *registry : AssetRegistry{};
    AssetRegistryEntry entry;
    entry.id = imported.header.id;
    entry.type = AssetType::Texture;
    entry.virtualPath =
        AssetRegistry::NormalizeVirtualPath(relative.generic_string());
    entry.sourcePath =
        std::filesystem::absolute(sourcePath).lexically_normal().string();
    entry.sourceHash = imported.header.sourceHash;
    entry.importerVersion = kTextureImporterVersion;
    if (!updated.Register(entry, error)
        || !updated.Save(AssetRegistry::DefaultRegistryPath(contentRoot), error))
        return false;
    if (registry) *registry = std::move(updated);
    if (result) {
        result->id = imported.header.id;
        result->outputPath = destinationPath;
        result->sourceHash = imported.header.sourceHash;
        result->width = imported.width;
        result->height = imported.height;
    }
    SetError(error, {});
    return true;
}

} // namespace engine
