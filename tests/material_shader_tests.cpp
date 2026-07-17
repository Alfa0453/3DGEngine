#include "MaterialMaker/MaterialDocument.h"
#include "engine/assets/MaterialAssetLoader.h"
#include "engine/assets/ShaderAsset.h"

#include <filesystem>
#include <iostream>
#include <string>

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

    std::filesystem::remove_all(root, filesystemError);

    if (failures == 0)
        std::cout << "Material shader regression tests passed.\n";
    return failures == 0 ? 0 : 1;
}
