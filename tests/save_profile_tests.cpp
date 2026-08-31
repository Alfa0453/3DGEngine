#include <engine/assets/AssetRegistry.h>
#include <engine/assets/SaveProfileAsset.h>
#include <engine/ecs/Components.h>
#include <engine/ecs/Registry.h>
#include <engine/gameplay/GameplayComponents.h>
#include <engine/gameplay/SaveGame.h>
#include <engine/gameplay/SaveProfileSystem.h>

#include <filesystem>
#include <iostream>

namespace {
int failures = 0;
void Check(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAILED: " << message << '\n'; }
}
}

int main() {
    namespace fs = std::filesystem;
    const fs::path oldPath = fs::current_path();
    const fs::path root = fs::temp_directory_path() / "3dg_save_profile_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    fs::current_path(root, ec);

    engine::SaveProfileAssetData profile;
    profile.header.id = engine::AssetHandle::Generate();
    profile.name = "WizardTrialSaves";
    profile.maximumSlots = 4;
    profile.defaultSlot = 1;
    profile.persistence.velocity = false;
    profile.respawn.healthFraction = 0.5f;
    engine::CheckpointDefinition checkpoint;
    checkpoint.name = "Courtyard";
    checkpoint.anchorObject = "CourtyardMarker";
    checkpoint.position = {2.0f, 1.0f, -3.0f};
    checkpoint.activationRadius = 3.0f;
    checkpoint.slot = 1;
    profile.checkpoints.push_back(checkpoint);
    const fs::path assetPath = root / "WizardTrial.3dgsaveprofile";
    std::string error;
    Check(engine::SaveSaveProfileAsset(assetPath.string(), profile, &error), "profile saves");
    engine::SaveProfileAssetData loaded;
    Check(engine::LoadSaveProfileAsset(assetPath.string(), &loaded, &error), "profile loads");
    Check(loaded.name == profile.name && loaded.maximumSlots == 4
        && loaded.checkpoints.size() == 1, "profile fields round trip");

    engine::AssetRegistry assets;
    Check(assets.RebuildFromContent(root.string(), &error), "registry scans save profiles");
    const auto* entry = assets.Find(profile.header.id);
    Check(entry && entry->type == engine::AssetType::SaveProfile,
          "registry identifies save profile");

    engine::ecs::Registry world;
    const auto player = world.Create();
    world.Add<engine::ecs::RuntimeName>(player, {"Player"});
    world.Add<engine::ecs::Transform>(player, {checkpoint.position});
    world.Add<engine::Health>(player, {80.0f, 100.0f, true, false});
    world.Add<engine::ecs::LinearVelocity>(player, {{4.0f, 0.0f, 0.0f}});
    Check(engine::ConfigureSaveProfile(assetPath.string(), &error), "runtime configures profile");
    std::string event;
    Check(engine::UpdateSaveProfile(world, player, "Wizard.scene", 0.016f, 42.0f, &event),
          "entering checkpoint creates save");
    Check(fs::exists(engine::SaveSlotPath(1)), "checkpoint slot exists");
    Check(event.find("Courtyard") != std::string::npos, "checkpoint event is descriptive");

    world.Get<engine::ecs::Transform>(player).position = {99.0f, 99.0f, 99.0f};
    world.Get<engine::Health>(player).hp = 0.0f;
    world.Get<engine::Health>(player).alive = false;
    world.Get<engine::ecs::LinearVelocity>(player).velocity = {8.0f, 1.0f, 0.0f};
    Check(engine::RespawnFromLastCheckpoint(world, player, &error), "respawn loads checkpoint");
    Check(world.Get<engine::ecs::Transform>(player).position == checkpoint.position,
          "respawn uses checkpoint transform");
    Check(world.Get<engine::Health>(player).hp == 50.0f && world.Get<engine::Health>(player).alive,
          "respawn applies authored health rule");
    Check(world.Get<engine::ecs::LinearVelocity>(player).velocity == glm::vec3(0.0f),
          "respawn clears movement");

    engine::ClearSaveProfile();
    fs::current_path(oldPath, ec);
    fs::remove_all(root, ec);
    if (failures) return 1;
    std::cout << "Save profile regression tests passed\n";
    return 0;
}
