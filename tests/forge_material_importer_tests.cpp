#include "engine/assets/AssetRegistry.h"
#include "engine/assets/ForgeMaterialImporter.h"
#include "engine/assets/MaterialAssetLoader.h"
#include "engine/assets/TextureAsset.h"
#include "engine/graphics/ImageDecode.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

std::uint32_t Crc32(const std::vector<unsigned char>& data) {
    std::uint32_t crc = 0xffffffffu;
    for (unsigned char value : data) {
        crc ^= value;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xffffffffu;
}

void AppendLe32(std::vector<unsigned char>* bytes, std::uint32_t value) {
    for (int i = 0; i < 4; ++i)
        bytes->push_back(static_cast<unsigned char>((value >> (i * 8u)) & 0xffu));
}

void WriteLe32(std::ostream& output, std::uint32_t value) {
    for (int i = 0; i < 4; ++i)
        output.put(static_cast<char>((value >> (i * 8u)) & 0xffu));
}

std::string Hex32(std::uint32_t value) {
    std::ostringstream text;
    text << std::hex << std::setw(8) << std::setfill('0') << value;
    return text.str();
}

struct Record {
    std::string name;
    unsigned level;
    unsigned width;
    unsigned height;
    unsigned channels;
    unsigned bitDepth;
    std::string byteOrder;
    std::string encoding;
    std::vector<unsigned char> raw;
    std::vector<unsigned char> encoded;
    std::size_t offset = 0;
};

std::vector<unsigned char> BuildPackage() {
    const std::vector<unsigned char> compressedAlbedo{
        0x78, 0x9c, 0x63, 0x64, 0x62, 0x66, 0x61, 0x65,
        0x63, 0xe7, 0xe0, 0xe4, 0xe2, 0xe6, 0x01, 0x00,
        0x01, 0x78, 0x00, 0x4f};
    std::vector<Record> records{
        {"base_color", 0, 2, 2, 3, 8, "not_applicable", "zlib",
         {1,2,3,4,5,6,7,8,9,10,11,12}, compressedAlbedo},
        {"base_color", 1, 1, 1, 3, 8, "not_applicable", "raw",
         {6,7,8}, {6,7,8}},
        {"normal", 0, 2, 2, 3, 8, "not_applicable", "raw",
         {1,10,3,4,20,6,7,30,9,10,40,12},
         {1,10,3,4,20,6,7,30,9,10,40,12}},
        {"normal", 1, 1, 1, 3, 8, "not_applicable", "raw",
         {20,50,80}, {20,50,80}},
        {"ambient_occlusion", 0, 2, 2, 1, 8, "not_applicable", "raw",
         {10,20,30,40}, {10,20,30,40}},
        {"ambient_occlusion", 1, 1, 1, 1, 8, "not_applicable", "raw",
         {25}, {25}},
        {"roughness", 0, 2, 2, 1, 8, "not_applicable", "raw",
         {50,60,70,80}, {50,60,70,80}},
        {"roughness", 1, 1, 1, 1, 8, "not_applicable", "raw",
         {65}, {65}},
        {"metallic", 0, 2, 2, 1, 8, "not_applicable", "raw",
         {90,100,110,120}, {90,100,110,120}},
        {"metallic", 1, 1, 1, 1, 8, "not_applicable", "raw",
         {105}, {105}},
        {"height", 0, 2, 2, 1, 16, "little", "raw",
         {0x00,0x00,0xff,0xff,0x80,0x80,0x40,0x40},
         {0x00,0x00,0xff,0xff,0x80,0x80,0x40,0x40}},
        {"height", 1, 1, 1, 1, 16, "little", "raw",
         {0x20,0x20}, {0x20,0x20}},
    };
    std::vector<unsigned char> payload;
    for (Record& record : records) {
        while (payload.size() % 16u != 0) payload.push_back(0);
        record.offset = payload.size();
        payload.insert(payload.end(), record.encoded.begin(), record.encoded.end());
    }
    std::ostringstream header;
    header << "{\"schema\":\"3dg.texture-container/1.0\","
           << "\"material\":\"Bridge QA\",\"alignment\":16,"
           << "\"metadata\":{\"normal_y\":\"negative\",\"tileable\":true},"
           << "\"records\":[";
    for (std::size_t i = 0; i < records.size(); ++i) {
        const Record& record = records[i];
        if (i) header << ',';
        header << "{\"name\":\"" << record.name << "\","
               << "\"level\":" << record.level << ','
               << "\"width\":" << record.width << ','
               << "\"height\":" << record.height << ','
               << "\"channels\":" << record.channels << ','
               << "\"bit_depth\":" << record.bitDepth << ','
               << "\"byte_order\":\"" << record.byteOrder << "\","
               << "\"encoding\":\"" << record.encoding << "\","
               << "\"offset\":" << record.offset << ','
               << "\"encoded_bytes\":" << record.encoded.size() << ','
               << "\"raw_bytes\":" << record.raw.size() << ','
               << "\"crc32\":\"" << Hex32(Crc32(record.raw)) << "\"}";
    }
    header << "]}";
    const std::string headerText = header.str();
    const std::vector<unsigned char> headerBytes(headerText.begin(), headerText.end());
    const std::size_t payloadOffset = (20u + headerBytes.size() + 15u) / 16u * 16u;
    std::vector<unsigned char> package{'3','D','G','T','E','X','1','\0'};
    AppendLe32(&package, static_cast<std::uint32_t>(headerBytes.size()));
    AppendLe32(&package, Crc32(headerBytes));
    AppendLe32(&package, static_cast<std::uint32_t>(payloadOffset));
    package.insert(package.end(), headerBytes.begin(), headerBytes.end());
    package.resize(payloadOffset, 0);
    package.insert(package.end(), payload.begin(), payload.end());
    return package;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    std::string error;
    const std::vector<unsigned char> compressed{
        0x78, 0x9c, 0x63, 0x64, 0x62, 0x66, 0x61, 0x65,
        0x63, 0xe7, 0xe0, 0xe4, 0xe2, 0xe6, 0x01, 0x00,
        0x01, 0x78, 0x00, 0x4f};
    const std::vector<unsigned char> inflated =
        engine::image::InflateZlib(compressed.data(), compressed.size(), 12);
    Check(inflated == std::vector<unsigned char>({1,2,3,4,5,6,7,8,9,10,11,12}),
          "bounded zlib decoder validates and expands a complete stream");

    const fs::path root = fs::temp_directory_path() / "3dg_forge_importer_tests";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path content = root / "Content";
    fs::create_directories(content, ec);
    const fs::path legacyPath = content / "legacy_v1.3dgtex";
    {
        engine::NativeAssetHeader header;
        header.type = engine::AssetType::Texture;
        header.id = engine::AssetHandle::Generate();
        header.assetVersion = engine::kLegacyTextureAssetVersion;
        header.payloadSize = 16u;
        std::ofstream output(legacyPath, std::ios::binary);
        Check(engine::WriteNativeAssetHeader(output, header, &error),
              "legacy texture header writes for compatibility fixture");
        WriteLe32(output, 1u);
        WriteLe32(output, 1u);
        WriteLe32(output, 3u);
        output.write("\x01\x02\x03\xff", 4);
    }
    engine::TextureAssetData legacyTexture;
    Check(engine::LoadTextureAsset(legacyPath.string(), &legacyTexture, &error)
          && legacyTexture.header.assetVersion == engine::kLegacyTextureAssetVersion
          && legacyTexture.mipmaps.empty() && legacyTexture.rgba.size() == 4,
          "texture asset version 2 loader remains compatible with version 1 assets");
    const fs::path source = root / "bridge_qa.3dgtexpack";
    const std::vector<unsigned char> packageBytes = BuildPackage();
    {
        std::ofstream output(source, std::ios::binary);
        output.write(reinterpret_cast<const char*>(packageBytes.data()),
                     static_cast<std::streamsize>(packageBytes.size()));
    }

    engine::ForgeTexturePackage package;
    Check(engine::LoadForgeTexturePackage(source.string(), &package, &error)
          && package.materialName == "Bridge QA"
          && package.normalY == "negative"
          && package.records.size() == 12
          && package.Find("base_color", 1)
          && package.Find("base_color", 1)->width == 1,
          "Forge source package validates compressed records and mip chains");

    engine::AssetRegistry registry;
    engine::ForgeMaterialImportResult imported;
    Check(engine::ImportForgeMaterialPackage(
              source.string(), (content / "Materials" / "BridgeQA").string(),
              content.string(), &registry, &imported, &error),
          "Forge package cooks into engine material and texture assets");
    Check(registry.Entries().size() == 5
          && registry.Find(imported.materialId)
          && registry.Find(imported.materialId)->dependencies.size() == 4
          && fs::is_regular_file(imported.materialPath),
          "Forge cook registers one material with four texture dependencies");

    engine::TextureAssetData albedo;
    engine::TextureAssetData normal;
    engine::TextureAssetData orm;
    engine::TextureAssetData height;
    Check(engine::LoadTextureAsset(imported.albedoMapPath, &albedo, &error)
          && engine::LoadTextureAsset(imported.normalMapPath, &normal, &error)
          && engine::LoadTextureAsset(imported.metalRoughMapPath, &orm, &error)
          && engine::LoadTextureAsset(imported.heightMapPath, &height, &error),
          "all cooked Forge textures load through the existing runtime format");
    Check(albedo.srgb && !normal.srgb && !orm.srgb && !height.srgb
          && albedo.rgba[0] == 7 && albedo.rgba[1] == 8 && albedo.rgba[2] == 9,
          "cook preserves color space and converts top-first package rows for OpenGL");
    Check(normal.rgba[0] == 7 && normal.rgba[1] == 225 && normal.rgba[2] == 9,
          "DirectX negative-Y normals are converted to the engine positive-Y convention");
    Check(orm.rgba[0] == 30 && orm.rgba[1] == 70 && orm.rgba[2] == 110
          && height.rgba[0] == 128,
          "cook rebuilds engine ORM channels and converts 16-bit height safely");
    Check(albedo.header.assetVersion == engine::kTextureAssetVersion
          && albedo.mipmaps.size() == 1 && normal.mipmaps.size() == 1
          && orm.mipmaps.size() == 1 && height.mipmaps.size() == 1
          && albedo.mipmaps[0].rgba[0] == 6
          && normal.mipmaps[0].rgba[1] == 205
          && orm.mipmaps[0].rgba[0] == 25
          && orm.mipmaps[0].rgba[1] == 65
          && orm.mipmaps[0].rgba[2] == 105,
          "map-aware Forge mip chains survive cooking in texture asset version 2");

    engine::RuntimeMaterialAsset material;
    Check(engine::LoadMaterialAssetFile(imported.materialPath, &material, &error)
          && material.id == imported.materialId
          && material.albedoMapAssetId == imported.albedoMapId
          && material.normalMapAssetId == imported.normalMapId
          && material.metalRoughMapAssetId == imported.metalRoughMapId
          && material.heightMapAssetId == imported.heightMapId,
          "cooked material loads with stable engine asset references");

    const engine::AssetHandle originalMaterial = imported.materialId;
    const engine::AssetHandle originalAlbedo = imported.albedoMapId;
    engine::ForgeMaterialImportResult reimported;
    Check(engine::ImportForgeMaterialPackage(
              source.string(), (content / "Materials" / "BridgeQA").string(),
              content.string(), &registry, &reimported, &error)
          && reimported.materialId == originalMaterial
          && reimported.albedoMapId == originalAlbedo
          && registry.Entries().size() == 5,
          "reimport preserves stable material and texture identities");

    const fs::path corrupt = root / "corrupt.3dgtexpack";
    std::vector<unsigned char> corruptBytes = packageBytes;
    corruptBytes.back() ^= 0x5au;
    {
        std::ofstream output(corrupt, std::ios::binary);
        output.write(reinterpret_cast<const char*>(corruptBytes.data()),
                     static_cast<std::streamsize>(corruptBytes.size()));
    }
    Check(!engine::LoadForgeTexturePackage(corrupt.string(), &package, &error)
          && error.find("checksum") != std::string::npos,
          "corrupt Forge package payloads are rejected before cooking");

    fs::remove_all(root, ec);
    std::cout << "forge material importer tests passed\n";
    return 0;
}
