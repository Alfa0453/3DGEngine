#include <engine/assets/AssetRegistry.h>
#include <engine/assets/PortalAsset.h>
#include <engine/ecs/Components.h>
#include <engine/ecs/Registry.h>
#include <engine/gameplay/PortalSystem.h>
#include <engine/physics/PhysicsComponents.h>

#include <cmath>
#include <filesystem>
#include <iostream>

namespace {
int failures = 0;
void Check(bool value, const char* message) { if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; } }
bool Near(float a, float b, float e = .001f) { return std::abs(a - b) <= e; }
}

int main() {
    engine::PortalAssetData asset;
    asset.header.id = engine::AssetHandle::Generate(); asset.name = "BluePortal";
    asset.destinationObject = "BlueExit"; asset.requiredAccessTag = "BlueKey";
    asset.arrivalOffset = {0.f, .2f, 2.f}; asset.exitAudioId = engine::AssetHandle::Generate();
    std::string error;
    Check(engine::ValidatePortalAsset(asset, &error), "valid same-level portal");
    const auto root = std::filesystem::temp_directory_path() / "3dg_portal_test";
    std::filesystem::create_directories(root);
    const auto path = root / "BluePortal.3dgportal";
    Check(engine::SavePortalAsset(path.string(), asset, &error), "save portal");
    engine::PortalAssetData loaded;
    Check(engine::LoadPortalAsset(path.string(), &loaded, &error), "load portal");
    Check(loaded.header.id == asset.header.id && loaded.header.dependencies.size() == 1,
          "identity and dependencies round trip");
    engine::AssetRegistry assets;
    Check(assets.RebuildFromContent(root.string(), &error), "scan portal registry");
    const auto* entry = assets.Find(asset.header.id);
    Check(entry && entry->type == engine::AssetType::Portal, "portal registry type");

    engine::ecs::Registry registry;
    const auto portal = registry.Create(); registry.Add<engine::ecs::Transform>(portal, {});
    const auto exit = registry.Create();
    engine::ecs::Transform exitTransform; exitTransform.position = {10.f, 1.f, 5.f};
    registry.Add<engine::ecs::Transform>(exit, exitTransform);
    registry.Add<engine::ecs::RuntimeName>(exit, {"BlueExit"});
    const auto traveler = registry.Create(); registry.Add<engine::ecs::Transform>(traveler, {});
    registry.Add<engine::PortalTravelerComponent>(traveler, {});
    engine::ecs::RigidBody body; body.velocity = {3.f, 0.f, 0.f};
    registry.Add<engine::ecs::RigidBody>(traveler, body);
    Check(engine::ConfigurePortal(registry, portal, loaded, path.string()), "configure portal");
    Check(!engine::ActivatePortal(registry, portal, traveler), "access tag enforced");
    Check(engine::ActivatePortal(registry, portal, traveler, "BlueKey"), "matching access teleports");
    const auto& pose = registry.Get<engine::ecs::Transform>(traveler);
    Check(Near(pose.position.x, 10.f) && Near(pose.position.y, 1.2f) && Near(pose.position.z, 7.f),
          "arrival transform applies destination and offset");
    Check(Near(registry.Get<engine::ecs::RigidBody>(traveler).velocity.x, 0.f), "velocity cleared");
    Check(!engine::ActivatePortal(registry, portal, traveler, "BlueKey"), "cooldown blocks spam");
    engine::UpdatePortals(registry, loaded.cooldown);
    Check(engine::PortalReady(registry, portal), "cooldown expires");
    const auto events = engine::ConsumePortalEvents(registry, portal);
    Check(events.size() == 2 && events.front().type == engine::PortalEventType::AccessDenied &&
          events.back().type == engine::PortalEventType::Teleported, "portal events emitted");

    engine::PortalAssetData level; level.name = "DungeonDoor";
    level.mode = engine::PortalMode::LevelTransition; level.destinationLevel = "Scenes/Dungeon.runtime.scene";
    Check(engine::ConfigurePortal(registry, portal, level), "configure level portal");
    Check(engine::ActivatePortal(registry, portal, traveler), "request level transition");
    const auto levelEvents = engine::ConsumePortalEvents(registry, portal);
    Check(levelEvents.size() == 1 && levelEvents[0].type == engine::PortalEventType::LevelTransitionRequested &&
          levelEvents[0].levelPath == level.destinationLevel, "level request carries destination");

    std::filesystem::remove_all(root);
    if (failures) return 1;
    std::cout << "Portal asset tests passed\n"; return 0;
}
