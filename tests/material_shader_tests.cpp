#include "MaterialMaker/MaterialDocument.h"
#include "MaterialMaker/TexturePacker.h"
#include "engine/assets/MaterialAssetLoader.h"
#include "engine/assets/AssetRegistry.h"
#include "engine/assets/ShaderAsset.h"
#include "engine/assets/TextureAsset.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <fstream>

namespace {

int failures = 0;

void Expect(bool condition, const char* message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAILED: " << message << '\n';
    }
}

} // namespace

int main()
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "3dg_material_shader_test";
    const std::filesystem::path shaderDirectory = root / "Shaders";
    std::error_code filesystemError;
    std::filesystem::create_directories(shaderDirectory, filesystemError);

    engine::ShaderAsset shader;
    shader.id = 1001;
    shader.name = "Material Test Shader";
    shader.nodes = {{1, "SurfaceOutput", "Surface", 0.0f, 0.0f}};
    shader.parameters = {
        {2, "Roughness", engine::ShaderValueType::Float, "0.35"},
        {3, "Tint", engine::ShaderValueType::Color, "1,0.5,0.25,1"},
        {4, "UseDetail", engine::ShaderValueType::Bool, "true"},
        {5, "DetailMap", engine::ShaderValueType::Texture2D, ""}
    };

    const std::filesystem::path shaderPath =
        shaderDirectory / "material_test.3dgshader";
    std::string error;
    Expect(engine::SaveShaderAsset(shaderPath.string(), shader, &error),
           "shader asset must save before material integration test");

    material_maker::MaterialDocument source;
    source.name = "ShaderMaterial";
    source.shaderPath = shaderPath.string();
    source.shaderParameters = {
        {"Roughness", static_cast<int>(engine::ShaderValueType::Float), "0.72"},
        {"Tint", static_cast<int>(engine::ShaderValueType::Color), "0.2,0.4,0.8,1"},
        {"UseDetail", static_cast<int>(engine::ShaderValueType::Bool), "false"},
        {"DetailMap", static_cast<int>(engine::ShaderValueType::Texture2D),
         (root / "Textures" / "detail.png").string()}
    };

    std::string materialPath;
    Expect(material_maker::SaveMaterialFile(
               source, root.string(), &materialPath, &error),
           "material with a shader reference must save");

    material_maker::MaterialDocument editorLoaded;
    Expect(material_maker::LoadMaterialFile(
               materialPath, &editorLoaded, &error),
           "Material Maker must reload a shader-backed material");
    Expect(std::filesystem::path(editorLoaded.shaderPath).lexically_normal()
               == shaderPath.lexically_normal(),
           "Material Maker must resolve the stored relative shader path");
    Expect(editorLoaded.shaderParameters.size() == source.shaderParameters.size()
           && editorLoaded.shaderParameters[0].value == "0.72",
           "Material Maker must preserve reflected parameter overrides");

    engine::RuntimeMaterialAsset runtimeLoaded;
    Expect(engine::LoadMaterialAssetFile(
               materialPath, &runtimeLoaded, &error),
           "runtime material loader must accept shader-backed materials");
    Expect(std::filesystem::path(runtimeLoaded.shaderPath).lexically_normal()
               == shaderPath.lexically_normal(),
           "runtime loader must resolve the same shader asset");
    Expect(runtimeLoaded.shaderParameters.size() == source.shaderParameters.size()
           && runtimeLoaded.shaderParameters[2].name == "UseDetail"
           && runtimeLoaded.shaderParameters[2].value == "false",
           "runtime loader must preserve shader parameter names, types, and values");

    // A material saved inside Content converts raw image maps to native
    // .3dgtex assets and records stable, slot-specific dependencies.
    static const unsigned char png[] = {
        0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,
        0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x02,0x10,0x00,0x00,0x00,0x00,0x07,0x4D,0x8E,
        0xBB,0x00,0x00,0x00,0x12,0x49,0x44,0x41,0x54,0x78,0x9C,0x63,0x60,0x60,0x60,0x60,
        0x64,0x68,0x60,0xF8,0xFF,0x1F,0x00,0x05,0x0D,0x02,0x80,0x01,0x70,0xCA,0x8C,0x00,
        0x00,0x00,0x00,0x49,0x45,0x4E,0x44,0xAE,0x42,0x60,0x82
    };
    const std::filesystem::path content = root / "Project" / "Content";
    const std::filesystem::path textureSource =
        content / "Textures" / "albedo.png";
    const std::filesystem::path materialDirectory = content / "Materials";
    std::filesystem::create_directories(textureSource.parent_path(),
                                        filesystemError);
    std::filesystem::create_directories(materialDirectory, filesystemError);
    const std::filesystem::path nativeShaderPath =
        content / "Shaders" / "material_test.3dgshader";
    Expect(engine::SaveShaderAsset(
               nativeShaderPath.string(), shader, &error)
               && shader.assetId.Valid(),
           "Content shader saves with a stable registered identity");
    {
        std::ofstream output(textureSource, std::ios::binary);
        output.write(reinterpret_cast<const char*>(png), sizeof(png));
    }
    material_maker::MaterialDocument nativeSource;
    nativeSource.name = "NativeMaterial";
    nativeSource.albedoMap = textureSource.string();
    nativeSource.shaderPath = nativeShaderPath.string();
    std::string nativeMaterialPath;
    Expect(material_maker::SaveMaterialFile(
               nativeSource, materialDirectory.string(),
               &nativeMaterialPath, &error),
           "Content material must save and import its raw texture");
    material_maker::MaterialDocument nativeLoaded;
    Expect(material_maker::LoadMaterialFile(
               nativeMaterialPath, &nativeLoaded, &error)
           && nativeLoaded.assetId.Valid()
           && nativeLoaded.albedoMapAssetId.Valid()
           && nativeLoaded.shaderAssetId == shader.assetId
           && std::filesystem::path(nativeLoaded.albedoMap).extension()
                  == ".3dgtex",
           "native material reloads stable material and texture IDs");
    engine::TextureAssetData nativeTexture;
    Expect(engine::LoadTextureAsset(
               nativeLoaded.albedoMap, &nativeTexture, &error)
           && nativeTexture.header.id == nativeLoaded.albedoMapAssetId
           && nativeTexture.width == 2 && nativeTexture.height == 2
           && nativeTexture.rgba.size() == 16,
           "native texture preserves decoded dimensions, pixels, and identity");

    const std::filesystem::path nativeRoughness =
        content / "Textures" / "roughness.3dgtex";
    engine::TextureAssetData roughnessTexture;
    roughnessTexture.header.id = engine::AssetHandle::Generate();
    roughnessTexture.width = 2;
    roughnessTexture.height = 2;
    roughnessTexture.smooth = false;
    roughnessTexture.srgb = false;
    roughnessTexture.rgba = {
        32, 32, 32, 255, 64, 64, 64, 255,
        96, 96, 96, 255, 128, 128, 128, 255
    };
    Expect(engine::SaveTextureAsset(
               nativeRoughness.string(), roughnessTexture, &error),
           "native ORM source texture must save");
    const std::filesystem::path packedOrm =
        content / "Textures" / "packed_orm.tga";
    const material_maker::PackResult packed =
        material_maker::PackMetalRoughAO(
            {}, nativeRoughness.string(), {}, packedOrm.string());
    Expect(packed.ok && std::filesystem::exists(packedOrm),
           "Material Maker ORM packer must accept engine-owned textures");
    const engine::AssetHandle originalMaterialId = nativeLoaded.assetId;
    Expect(material_maker::SaveMaterialFile(
               nativeLoaded, materialDirectory.string(),
               &nativeMaterialPath, &error),
           "native material resave succeeds");
    material_maker::MaterialDocument resaved;
    Expect(material_maker::LoadMaterialFile(
               nativeMaterialPath, &resaved, &error)
           && resaved.assetId == originalMaterialId,
           "material resave preserves its stable ID");

    engine::AssetRegistry nativeRegistry;
    Expect(nativeRegistry.Load(
               engine::AssetRegistry::DefaultRegistryPath(content.string()),
               &error)
           && nativeRegistry.Find(originalMaterialId)
           && nativeRegistry.Find(originalMaterialId)->dependencies.size() == 2
           && nativeRegistry.Find(originalMaterialId)->dependencies[0]
                  == nativeLoaded.albedoMapAssetId
           && nativeRegistry.Find(originalMaterialId)->dependencies[1]
                  == nativeLoaded.shaderAssetId,
           "registry records material-to-texture and material-to-shader dependencies");
    const std::filesystem::path movedTexture =
        content / "Moved" / "albedo.3dgtex";
    std::filesystem::create_directories(movedTexture.parent_path(),
                                        filesystemError);
    std::filesystem::rename(nativeLoaded.albedoMap, movedTexture,
                            filesystemError);
    Expect(!filesystemError
           && nativeRegistry.Move(
               nativeLoaded.albedoMapAssetId,
               "/Game/Moved/albedo.3dgtex", &error)
           && nativeRegistry.Save(
               engine::AssetRegistry::DefaultRegistryPath(content.string()),
               &error),
           "native texture can move while preserving its ID");
    const std::filesystem::path movedShader =
        content / "Moved" / "material_test.3dgshader";
    filesystemError.clear();
    std::filesystem::rename(nativeShaderPath, movedShader, filesystemError);
    Expect(!filesystemError
           && nativeRegistry.Move(
               shader.assetId,
               "/Game/Moved/material_test.3dgshader", &error)
           && nativeRegistry.Save(
               engine::AssetRegistry::DefaultRegistryPath(content.string()),
               &error),
           "shader can move while preserving its ID");
    engine::RuntimeMaterialAsset movedRuntime;
    Expect(engine::LoadMaterialAssetFile(
               nativeMaterialPath, &movedRuntime, &error)
           && std::filesystem::path(movedRuntime.albedoMapPath)
                  .lexically_normal() == movedTexture.lexically_normal()
           && std::filesystem::path(movedRuntime.shaderPath)
                  .lexically_normal() == movedShader.lexically_normal(),
           "runtime material resolves moved texture and shader references by ID");

    std::filesystem::remove_all(root, filesystemError);

    if (failures == 0)
        std::cout << "Material shader regression tests passed.\n";
    return failures == 0 ? 0 : 1;
}
