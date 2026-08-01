#include <engine/scene/LevelStreamingManager.h>
#include <engine/scene/WorldManifest.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

bool Near(float a, float b) {
    return glm::abs(a - b) < 0.0001f;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    engine::WorldManifest source;
    source.id = engine::AssetHandle::Generate();
    source.persistentScenePath = "Persistent.runtime.scene";

    engine::LevelRef level;
    level.scenePath = "Dungeon.runtime.scene";
    level.sceneId = engine::AssetHandle::Generate();
    level.boundsMin = {-10.0f, -2.0f, -5.0f};
    level.boundsMax = {10.0f, 2.0f, 5.0f};
    level.worldTransform[3] = glm::vec4(100.0f, 4.0f, -30.0f, 1.0f);
    level.loadRadius = 20.0f;
    level.unloadRadius = 30.0f;
    source.levels.push_back(level);

    const fs::path path =
        fs::temp_directory_path() / "3dg_world_streaming_test.3dgworld";
    std::string error;
    Check(engine::SaveWorldManifest(path.string(), source, &error),
          "save world manifest");
    engine::WorldManifest loaded;
    Check(engine::LoadWorldManifest(path.string(), &loaded, &error)
          && loaded.id == source.id
          && loaded.persistentScenePath == source.persistentScenePath
          && loaded.levels.size() == 1
          && loaded.levels[0].sceneId == level.sceneId
          && Near(loaded.levels[0].worldTransform[3].x, 100.0f),
          "world manifest round trip");

    const glm::vec3 center = loaded.levels[0].WorldBoundsCenter();
    Check(Near(center.x, 100.0f) && Near(center.y, 4.0f)
          && Near(center.z, -30.0f),
          "placed world bounds center");
    Check(engine::LevelStreamingManager::WantsResident(
              loaded.levels[0], false, center + glm::vec3(19.0f, 0.0f, 0.0f))
          && !engine::LevelStreamingManager::WantsResident(
              loaded.levels[0], false, center + glm::vec3(21.0f, 0.0f, 0.0f))
          && engine::LevelStreamingManager::WantsResident(
              loaded.levels[0], true, center + glm::vec3(29.0f, 0.0f, 0.0f))
          && !engine::LevelStreamingManager::WantsResident(
              loaded.levels[0], true, center + glm::vec3(31.0f, 0.0f, 0.0f)),
          "distance streaming hysteresis");

    loaded.levels[0].rule = engine::LevelStreamRule::AlwaysLoaded;
    Check(engine::LevelStreamingManager::WantsResident(
              loaded.levels[0], false, glm::vec3(100000.0f)),
          "always-loaded policy");
    loaded.levels[0].rule = engine::LevelStreamRule::Manual;
    Check(!engine::LevelStreamingManager::WantsResident(
              loaded.levels[0], false, center)
          && engine::LevelStreamingManager::WantsResident(
              loaded.levels[0], true, glm::vec3(100000.0f)),
          "manual policy preserves explicit state");

    std::error_code ec;
    fs::remove(path, ec);
    std::cout << "world streaming tests passed\n";
    return 0;
}
