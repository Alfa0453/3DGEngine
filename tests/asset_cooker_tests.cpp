#include <engine/assets/AssetCooker.h>
#include <engine/assets/AssetRegistry.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

void WriteText(const std::filesystem::path& path, const std::string& text) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream output(path, std::ios::binary);
    output << text;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path root =
        fs::temp_directory_path() / "3dg_asset_cooker_tests";
    const fs::path content = root / "Project" / "Content";
    const fs::path scene = content / "Scenes" / "Level.runtime.scene";
    const fs::path output = root / "Cooked" / "TestGame";
    std::error_code ec;
    fs::remove_all(root, ec);

    const engine::AssetHandle textureId = engine::AssetHandle::Generate();
    const engine::AssetHandle shaderId = engine::AssetHandle::Generate();
    const engine::AssetHandle materialId = engine::AssetHandle::Generate();
    const engine::AssetHandle meshId = engine::AssetHandle::Generate();
    const engine::AssetHandle particleId = engine::AssetHandle::Generate();
    const engine::AssetHandle particleEffectId = engine::AssetHandle::Generate();
    const engine::AssetHandle hudId = engine::AssetHandle::Generate();
    const engine::AssetHandle behaviorId = engine::AssetHandle::Generate();
    const engine::AssetHandle audioId = engine::AssetHandle::Generate();
    const engine::AssetHandle unusedId = engine::AssetHandle::Generate();
    const engine::AssetHandle sceneId = engine::AssetHandle::Generate();

    engine::AssetRegistry registry;
    engine::AssetRegistryEntry texture{
        textureId, engine::AssetType::Texture,
        "/Game/Textures/Wizard.3dgtex"};
    engine::AssetRegistryEntry material{
        materialId, engine::AssetType::Material,
        "/Game/Materials/Wizard.3dgmat"};
    material.dependencies = {textureId, shaderId};
    engine::AssetRegistryEntry shader{
        shaderId, engine::AssetType::Shader,
        "/Game/Shaders/Wizard.3dgshader"};
    engine::AssetRegistryEntry mesh{
        meshId, engine::AssetType::StaticMesh,
        "/Game/Meshes/Wizard.3dgmesh"};
    mesh.dependencies = {materialId};
    engine::AssetRegistryEntry particle{
        particleId, engine::AssetType::Particle,
        "/Game/Particles/Fireball.particle"};
    particle.dependencies = {textureId, shaderId};
    engine::AssetRegistryEntry particleEffect{
        particleEffectId, engine::AssetType::ParticleEffect,
        "/Game/Particles/FireballImpact.particlefx"};
    particleEffect.dependencies = {particleId};
    engine::AssetRegistryEntry hud{
        hudId, engine::AssetType::Hud,
        "/Game/UI/Gameplay.hud"};
    hud.dependencies = {textureId, shaderId};
    engine::AssetRegistryEntry behavior{
        behaviorId, engine::AssetType::BehaviorTree,
        "/Game/AI/Wizard.btgraph"};
    engine::AssetRegistryEntry audio{
        audioId, engine::AssetType::Audio,
        "/Game/Audio/Fire.3dgaudio"};
    engine::AssetRegistryEntry unused{
        unusedId, engine::AssetType::Audio,
        "/Game/Audio/Unused.3dgaudio"};
    std::string error;
    Check(registry.Register(texture, &error)
          && registry.Register(shader, &error)
          && registry.Register(material, &error)
          && registry.Register(mesh, &error)
          && registry.Register(particle, &error)
          && registry.Register(particleEffect, &error)
          && registry.Register(hud, &error)
          && registry.Register(behavior, &error)
          && registry.Register(audio, &error)
          && registry.Register(unused, &error),
          "register cook test assets");

    WriteText(content / "Textures" / "Wizard.3dgtex", "texture");
    WriteText(content / "Shaders" / "Wizard.3dgshader", "shader");
    WriteText(content / "Materials" / "Wizard.3dgmat", "material");
    WriteText(content / "Meshes" / "Wizard.3dgmesh", "mesh");
    WriteText(content / "Particles" / "Fireball.particle", "particle");
    WriteText(content / "Particles" / "FireballImpact.particlefx", "effect");
    WriteText(content / "UI" / "Gameplay.hud", "hud");
    WriteText(content / "AI" / "Wizard.btgraph", "behavior");
    WriteText(content / "Audio" / "Fire.3dgaudio", "audio");
    WriteText(content / "Audio" / "Unused.3dgaudio", "unused");
    WriteText(scene,
        "3DGRuntimeScene 73 " + sceneId.ToString() + "\n"
        "ASSET_DEPS 5 " + meshId.ToString() + " "
            + particleEffectId.ToString() + " " + hudId.ToString()
            + " " + behaviorId.ToString() + " " + audioId.ToString()
            + "\n");

    engine::AssetCookResult result;
    Check(engine::AssetCooker::CookRuntimeScene(
              content.string(), scene.string(), output.string(),
              registry, &result, &error),
          "cook runtime scene dependency closure");
    Check(result.assets.size() == 9
          && fs::is_regular_file(
              output / "Content" / "Meshes" / "Wizard.3dgmesh")
          && fs::is_regular_file(
              output / "Content" / "Materials" / "Wizard.3dgmat")
          && fs::is_regular_file(
              output / "Content" / "Textures" / "Wizard.3dgtex")
          && fs::is_regular_file(
              output / "Content" / "Shaders" / "Wizard.3dgshader")
          && fs::is_regular_file(
              output / "Content" / "Particles" / "Fireball.particle")
          && fs::is_regular_file(
              output / "Content" / "Particles" / "FireballImpact.particlefx")
          && fs::is_regular_file(
              output / "Content" / "UI" / "Gameplay.hud")
          && fs::is_regular_file(
              output / "Content" / "AI" / "Wizard.btgraph")
          && fs::is_regular_file(
              output / "Content" / "Audio" / "Fire.3dgaudio")
          && !fs::exists(output / "Content" / "Audio" / "Unused.3dgaudio"),
          "cook includes mesh and particle dependency chains and excludes unrelated assets");
    Check(fs::is_regular_file(
              output / "Content" / "Scenes" / "Level.runtime.scene")
          && fs::is_regular_file(output / "Content" / "AssetRegistry.3dgdb")
          && fs::is_regular_file(output / "CookManifest.3dgmanifest")
          && fs::is_regular_file(output / "player.cfg"),
          "cook emits runtime scene, reduced registry, manifest and config");

    engine::AssetRegistry cookedRegistry;
    Check(cookedRegistry.Load(
              (output / "Content" / "AssetRegistry.3dgdb").string(), &error)
          && cookedRegistry.Find(sceneId)
          && cookedRegistry.Find(meshId)
          && cookedRegistry.Find(materialId)
          && cookedRegistry.Find(textureId)
          && cookedRegistry.Find(shaderId)
          && cookedRegistry.Find(particleId)
          && cookedRegistry.Find(particleEffectId)
          && cookedRegistry.Find(hudId)
          && cookedRegistry.Find(behaviorId)
          && cookedRegistry.Find(audioId)
          && !cookedRegistry.Find(unusedId),
          "cooked registry contains only the scene dependency closure");

    engine::AssetRegistry missingRegistry = registry;
    engine::AssetRegistryEntry broken = *missingRegistry.Find(materialId);
    broken.dependencies.push_back(engine::AssetHandle::Generate());
    Check(missingRegistry.Register(std::move(broken), &error)
          && !engine::AssetCooker::CookRuntimeScene(
              content.string(), scene.string(),
              (root / "Cooked" / "Broken").string(),
              missingRegistry, nullptr, &error)
          && error.find("missing from the registry") != std::string::npos,
          "cook fails with a useful error for a missing dependency");

    fs::remove_all(root, ec);
    std::cout << "asset cooker tests passed\n";
    return 0;
}
