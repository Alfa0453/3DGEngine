#include "engine/assets/RuntimeShaderManager.h"
#include "engine/assets/ShaderAsset.h"
#include "engine/assets/ShaderGraphCompiler.h"
#include "engine/scene/RuntimeSceneLoader.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void Expect(bool condition, const char* message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAILED: " << message << '\n';
    }
}

engine::ShaderAsset ValidAsset()
{
    engine::ShaderAsset asset;
    asset.id = 42;
    asset.name = "Test Surface";
    asset.domain = engine::ShaderDomain::Surface;
    asset.nodes = {
        {1, "Float", "Roughness", 10.0f, 20.0f},
        {2, "SurfaceOutput", "Surface", 300.0f, 20.0f}
    };
    asset.pins = {
        {10, 1, "Value", engine::ShaderValueType::Float, false, false},
        {20, 2, "Roughness", engine::ShaderValueType::Float, true, true}
    };
    asset.links = {{30, 10, 20}};
    asset.parameters = {
        {40, "Roughness", engine::ShaderValueType::Float, "0.5"}
    };
    return asset;
}

bool ContainsIssue(const std::vector<engine::ShaderAssetIssue>& issues,
                   const std::string& text)
{
    for (const auto& issue : issues)
        if (issue.message.find(text) != std::string::npos) return true;
    return false;
}

} // namespace

int main()
{
    engine::ShaderAsset asset = ValidAsset();
    asset.nodes[0].comment = "Feeds the output";
    asset.nodes[0].groupId = 77;
    asset.nodes[0].value = "0.45";
    Expect(!engine::ShaderAssetHasErrors(engine::ValidateShaderAsset(asset)),
           "valid graph must pass validation");

    const std::uint64_t originalHash = engine::HashShaderAsset(asset);
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "3dg_shader_asset_test.3dgshader";
    std::string error;
    Expect(engine::SaveShaderAsset(path.string(), asset, &error),
           "valid asset must save");
    engine::ShaderAsset loaded;
    Expect(engine::LoadShaderAsset(path.string(), &loaded, &error),
           "saved asset must load");
    Expect(engine::HashShaderAsset(loaded) == originalHash,
           "shader asset round-trip must be deterministic");
    Expect(loaded.nodes[0].comment == "Feeds the output"
           && loaded.nodes[0].groupId == 77
           && loaded.nodes[0].value == "0.45",
           "graph comments, groups, and values must survive serialization");
    std::error_code removeError;
    std::filesystem::remove(path, removeError);

    engine::ShaderAsset missingInput = ValidAsset();
    missingInput.links.clear();
    Expect(ContainsIssue(engine::ValidateShaderAsset(missingInput), "Required input"),
           "unconnected required input must be reported");

    engine::ShaderAsset mismatch = ValidAsset();
    mismatch.pins[0].type = engine::ShaderValueType::Texture2D;
    Expect(ContainsIssue(engine::ValidateShaderAsset(mismatch), "Cannot connect"),
           "incompatible linked pin types must be reported");

    engine::ShaderAsset duplicateParameter = ValidAsset();
    duplicateParameter.parameters.push_back(
        {41, "Roughness", engine::ShaderValueType::Float, "0.8"});
    Expect(ContainsIssue(engine::ValidateShaderAsset(duplicateParameter), "unique"),
           "duplicate parameter names must be reported");

    engine::ShaderAsset wrongDomainOutput = ValidAsset();
    wrongDomainOutput.domain = engine::ShaderDomain::Particle;
    Expect(ContainsIssue(engine::ValidateShaderAsset(wrongDomainOutput),
                         "no domain output"),
           "a graph must use the output node required by its domain");

    engine::ShaderAsset invalidType = ValidAsset();
    invalidType.pins[0].type = static_cast<engine::ShaderValueType>(99);
    Expect(ContainsIssue(engine::ValidateShaderAsset(invalidType), "type is invalid"),
           "invalid serialized value types must be rejected");

    engine::ShaderAsset cyclic = ValidAsset();
    cyclic.nodes.push_back({3, "Multiply", "Loop", 100.0f, 100.0f});
    cyclic.pins.push_back(
        {31, 3, "In", engine::ShaderValueType::Float, true, false});
    cyclic.pins.push_back(
        {32, 3, "Out", engine::ShaderValueType::Float, false, false});
    cyclic.links.push_back({33, 32, 31});
    Expect(ContainsIssue(engine::ValidateShaderAsset(cyclic), "cycles"),
           "graph cycles must be reported");

    const auto fallback =
        engine::ShaderFallbackSources(engine::ShaderDomain::Surface);
    Expect(fallback.first.find("#version") != std::string::npos
           && fallback.second.find("1.0,0.0,1.0") != std::string::npos,
           "surface fallback must be valid-looking and visibly magenta");

    engine::ShaderAsset generatedAsset = ValidAsset();
    generatedAsset.nodes.push_back(
        {99, "ConstantFloat", "Unreachable", 0.0f, 0.0f, {}, 0, false, "0.2"});
    generatedAsset.pins.push_back(
        {100, 99, "Result", engine::ShaderValueType::Float, false, false});
    const auto generatedA = engine::GenerateShaderSource(generatedAsset);
    const auto generatedB = engine::GenerateShaderSource(generatedAsset);
    Expect(generatedA.success && generatedA.vertex == generatedB.vertex
           && generatedA.fragment == generatedB.fragment,
           "identical graphs must generate deterministic shader source");
    Expect(generatedA.fragment.find("node:99") == std::string::npos,
           "unreachable graph nodes must not be emitted");

    engine::ShaderAsset objectColor = ValidAsset();
    objectColor.nodes[0].type = "ObjectColor";
    objectColor.nodes[0].name = "Object Color";
    objectColor.pins[0].type = engine::ShaderValueType::Color;
    objectColor.pins[1].name = "Base Color";
    objectColor.pins[1].type = engine::ShaderValueType::Color;
    const auto objectColorSource = engine::GenerateShaderSource(objectColor);
    Expect(objectColorSource.success
           && objectColorSource.fragment.find("uniform vec4 uObjectColor")
               != std::string::npos
           && objectColorSource.fragment.find("uObjectColor")
               != std::string::npos,
           "surface Object Color must use the per-object renderer uniform");

    engine::ShaderAsset folded;
    folded.id = 500;
    folded.name = "Folded";
    folded.nodes = {
        {1, "ConstantFloat", "A", 0, 0, {}, 0, false, "0.2"},
        {2, "ConstantFloat", "B", 0, 0, {}, 0, false, "0.3"},
        {3, "Add", "Add", 0, 0},
        {4, "SurfaceOutput", "Surface", 0, 0}
    };
    folded.pins = {
        {10, 1, "Result", engine::ShaderValueType::Float, false, false},
        {11, 2, "Result", engine::ShaderValueType::Float, false, false},
        {12, 3, "A", engine::ShaderValueType::Float, true, false},
        {13, 3, "B", engine::ShaderValueType::Float, true, false},
        {14, 3, "Result", engine::ShaderValueType::Float, false, false},
        {15, 4, "Base Color", engine::ShaderValueType::Color, true, false}
    };
    folded.links = {{20, 10, 12}, {21, 11, 13}, {22, 14, 15}};
    const auto foldedSource = engine::GenerateShaderSource(folded);
    Expect(foldedSource.success
           && foldedSource.fragment.find("vec3(0.5)") != std::string::npos,
           "constant arithmetic should be folded during generation");
    Expect(!engine::GenerateShaderSource(cyclic).success,
           "cyclic graphs must not generate shader source");

    engine::ShaderAsset post;
    post.id = 700;
    post.name = "Post Process Test";
    post.domain = engine::ShaderDomain::PostProcess;
    post.nodes = {
        {1, "SceneColor", "Scene Color", 0.0f, 0.0f},
        {2, "PostProcessOutput", "Post Process Output", 200.0f, 0.0f}
    };
    post.pins = {
        {10, 1, "Color", engine::ShaderValueType::Color, false, false},
        {11, 2, "Color", engine::ShaderValueType::Color, true, true}
    };
    post.links = {{20, 10, 11}};
    const auto postSource = engine::GenerateShaderSource(post);
    Expect(postSource.success
           && postSource.vertex.find("in vec2 aPosition") != std::string::npos,
           "post-process graphs must generate a fullscreen vertex stage");
    Expect(postSource.fragment.find("uniform sampler2D uSceneColor")
               != std::string::npos
           && postSource.fragment.find("texture(uSceneColor,vUV)")
               != std::string::npos
           && postSource.fragment.find("uSceneDepth") != std::string::npos
           && postSource.fragment.find("uSceneNormal") != std::string::npos
           && postSource.fragment.find("uSceneVelocity") != std::string::npos
           && postSource.fragment.find("uTexelSize") != std::string::npos,
           "post-process graphs must expose colour, depth, normal, velocity, and texel inputs");

    engine::ShaderAsset postSampling;
    postSampling.id = 710;
    postSampling.name = "Resolution Independent Sampling";
    postSampling.domain = engine::ShaderDomain::PostProcess;
    postSampling.nodes = {
        {1, "ScreenUV", "Screen UV", 0.0f, 0.0f},
        {2, "ConstantVec2", "Pixel Offset", 0.0f, 80.0f, {}, 0, false,
         "vec2(1.0,0.0)"},
        {3, "PixelOffset", "Offset UV", 120.0f, 0.0f},
        {4, "SceneColorSample", "Sample Scene", 240.0f, 0.0f},
        {5, "PostProcessOutput", "Post Process Output", 360.0f, 0.0f}
    };
    postSampling.pins = {
        {10, 1, "UV", engine::ShaderValueType::Vec2, false, false},
        {11, 2, "Texel", engine::ShaderValueType::Vec2, false, false},
        {12, 3, "UV", engine::ShaderValueType::Vec2, true, true},
        {13, 3, "Pixels", engine::ShaderValueType::Vec2, true, true},
        {14, 3, "Result", engine::ShaderValueType::Vec2, false, false},
        {15, 4, "UV", engine::ShaderValueType::Vec2, true, true},
        {16, 4, "Color", engine::ShaderValueType::Color, false, false},
        {17, 5, "Color", engine::ShaderValueType::Color, true, true}
    };
    postSampling.links = {
        {20, 10, 12}, {21, 11, 13}, {22, 14, 15}, {23, 16, 17}
    };
    const auto samplingSource =
        engine::GenerateShaderSource(postSampling);
    Expect(samplingSource.success
           && samplingSource.fragment.find("*uTexelSize")
               != std::string::npos
           && samplingSource.fragment.find("texture(uSceneColor,")
               != std::string::npos,
           "post-process pixel-offset sampling must be resolution independent");

    engine::ShaderAsset particle;
    particle.id = 720;
    particle.name = "Particle Test";
    particle.domain = engine::ShaderDomain::Particle;
    particle.nodes = {
        {1, "ParticleColor", "Particle Color", 0.0f, 0.0f},
        {2, "ParticleOutput", "Particle Output", 200.0f, 0.0f}
    };
    particle.pins = {
        {10, 1, "Color", engine::ShaderValueType::Color, false, false},
        {11, 2, "Color", engine::ShaderValueType::Color, true, true}
    };
    particle.links = {{20, 10, 11}};
    const auto particleSource = engine::GenerateShaderSource(particle);
    Expect(particleSource.success
           && particleSource.vertex.find("iNormalizedAge") != std::string::npos
           && particleSource.fragment.find("vParticleColor") != std::string::npos,
           "particle graphs must generate the instanced particle attribute contract");

    engine::ShaderAsset unlit;
    unlit.id = 730;
    unlit.name = "UI Test";
    unlit.domain = engine::ShaderDomain::Unlit;
    unlit.nodes = {
        {1, "WidgetColor", "Widget Color", 0.0f, 0.0f},
        {2, "UnlitOutput", "Unlit Output", 200.0f, 0.0f}
    };
    unlit.pins = {
        {10, 1, "Color", engine::ShaderValueType::Color, false, false},
        {11, 2, "Color", engine::ShaderValueType::Color, true, true}
    };
    unlit.links = {{20, 10, 11}};
    const auto unlitSource = engine::GenerateShaderSource(unlit);
    Expect(unlitSource.success
           && unlitSource.vertex.find("uProjection") != std::string::npos
           && unlitSource.fragment.find("uWidgetColor") != std::string::npos,
           "unlit graphs must generate the HUD vertex and widget binding contract");

    engine::ShaderAsset illegalDomain = ValidAsset();
    illegalDomain.nodes.push_back({90, "ParticleAge", "Age", 0.0f, 0.0f});
    Expect(ContainsIssue(engine::ValidateShaderAsset(illegalDomain), "Particle input"),
           "domain-specific inputs must be rejected before GLSL compilation");

    const std::filesystem::path runtimeScenePath =
        std::filesystem::temp_directory_path()
        / "3dg_post_process_runtime_scene_test.scene";
    {
        std::ofstream runtimeScene(runtimeScenePath);
        runtimeScene << "3DGRuntimeScene 46\n"
            "post_effect \"Content/Shaders/Test Post.3dgshader\" 1 2 "
            "\"Intensity\" 0 \"1.25\" \"Enabled\" 2 \"true\"\n";
    }
    engine::RuntimeSceneLoader::Scene runtimeScene;
    Expect(engine::RuntimeSceneLoader::Load(
               runtimeScenePath.string(), &runtimeScene, &error),
           "runtime scene loader must accept post-process stack records");
    Expect(runtimeScene.environment.postProcessEffects.size() == 1
           && runtimeScene.environment.postProcessEffects[0].enabled
           && runtimeScene.environment.postProcessEffects[0].parameters.size()
               == 2
           && runtimeScene.environment.postProcessEffects[0].parameters[0].value
               == "1.25",
           "runtime post-process effect order and parameter values must survive loading");
    std::filesystem::remove(runtimeScenePath, removeError);

    const auto hashA = engine::RuntimeShaderManager::HashSources("a", "bc");
    const auto hashB = engine::RuntimeShaderManager::HashSources("ab", "c");
    Expect(hashA != hashB, "source hash must preserve stage boundaries");

    engine::ShaderCompileReport report;
    auto missingFiles = engine::Shader::TryFromFiles(
        "__missing_vertex__.vert", "__missing_fragment__.frag", report);
    Expect(!missingFiles && !report.success && !report.diagnostics.empty()
           && report.diagnostics.front().stage == engine::ShaderStage::File,
           "safe file compilation must return a file diagnostic instead of throwing");

    if (failures == 0)
        std::cout << "Shader asset regression tests passed.\n";
    return failures == 0 ? 0 : 1;
}
