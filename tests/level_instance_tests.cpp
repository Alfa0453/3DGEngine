#include "LevelInstancePanel.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
bool Require(bool value, const char* message) {
    if (!value) std::cerr << message << '\n';
    return value;
}
}

int main() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "3dg_level_instance_test";
    std::error_code ec; fs::remove_all(root, ec); fs::create_directories(root, ec);
    const fs::path persistent = root / "Persistent.scene";
    const fs::path source = root / "Room.scene";
    std::ofstream(persistent) << "scene";
    std::ofstream(source) << "scene";

    engine::WorldManifest world;
    world.persistentScenePath = persistent.string();
    engine::LevelRef valid; valid.scenePath = source.string();
    valid.loadRadius = 80.0f; valid.unloadRadius = 20.0f;
    valid.boundsMin = {3, 4, 5}; valid.boundsMax = {-3, -4, -5};
    LevelInstancePanel::Normalize(valid);
    if (!Require(valid.unloadRadius == 80.0f, "streaming hysteresis was not normalized")) return 1;
    if (!Require(valid.boundsMin == glm::vec3(-3,-4,-5)
                 && valid.boundsMax == glm::vec3(3,4,5), "bounds were not normalized")) return 1;
    world.levels.push_back(valid);
    if (!Require(LevelInstancePanel::Validate(world, {}, persistent.string()).empty(),
                 "valid linked level was rejected")) return 1;
    const fs::path worldPath = root / "World.3dgworld";
    world.levels[0].enabled = false; world.levels[0].dataLayer = "Interiors";
    std::string error;
    if (!Require(engine::SaveWorldManifest(worldPath.string(), world, &error), "world v2 save failed")) return 1;
    engine::WorldManifest loaded;
    if (!Require(engine::LoadWorldManifest(worldPath.string(), &loaded, &error), "world v2 load failed")) return 1;
    if (!Require(!loaded.levels[0].enabled && loaded.levels[0].dataLayer == "Interiors",
                 "instance visibility/data layer did not round trip")) return 1;

    world.levels[0].scenePath = persistent.string();
    if (!Require(!LevelInstancePanel::Validate(world, {}, persistent.string()).empty(),
                 "self-referencing level was not detected")) return 1;
    world.levels[0].scenePath = (root / "Missing.scene").string();
    if (!Require(!LevelInstancePanel::Validate(world, {}, persistent.string()).empty(),
                 "missing source was not detected")) return 1;

    fs::remove_all(root, ec);
    return 0;
}
