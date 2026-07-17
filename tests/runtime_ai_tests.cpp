#include <engine/scene/RuntimeSceneLoader.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

bool Near(float a, float b) {
    return std::fabs(a - b) < 0.0001f;
}

} // namespace

int main() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "3dg_runtime_ai_test.3dgscene";
    {
        std::ofstream output(path);
        output << "3DGRuntimeScene 51\n"
               << "nav_bounds 4 1 -3 20 4 12 1 0 0 0\n"
               << "nav_agent \"Enemy Mage\" 4.5 25 0.8 0.2 "
                  "\"PlayerStart\" 18 55 \"AI/Mage.btgraph\" 2 1 2 "
                  "1 0 2 8 0 -5\n"
               << "trigger_action \"GateTrigger\" \"Gate\" 1 0 2 0 "
                  "\"Opening\" 1 0 1 1\n"
               << "camera_zone \"CaveZone\" \"CaveCamera\" 1 7 0.5\n"
               << "physics_joint 1 \"Bridge\" \"\" 1 0 4 0 2 0 80 3\n"
               << "terrain \"Landscape\" 2 10 3 42 4 2 4 0 1 2 3 4 0 1 2 3\n";
    }

    engine::RuntimeSceneLoader::Scene scene;
    std::string error;
    Check(engine::RuntimeSceneLoader::Load(path.string(), &scene, &error),
          "load runtime AI records");
    Check(scene.navBounds.size() == 1, "navigation bounds count");
    Check(Near(scene.navBounds[0].position.x, 4.0f)
          && Near(scene.navBounds[0].scale.z, 12.0f),
          "navigation bounds transform");
    Check(scene.navAgents.size() == 1, "navigation agent count");
    const auto& agent = scene.navAgents[0];
    Check(agent.entityName == "Enemy Mage" && agent.targetName == "PlayerStart",
          "agent entity references");
    Check(Near(agent.speed, 4.5f) && Near(agent.visionRange, 18.0f),
          "agent movement and perception settings");
    Check(agent.brainAsset == "AI/Mage.btgraph"
          && agent.team == 2 && agent.autoTarget,
          "agent behavior and faction settings");
    Check(agent.patrolPoints.size() == 2
          && Near(agent.patrolPoints[1].z, -5.0f),
          "agent patrol route");
    Check(scene.triggerActions.size() == 1
          && scene.triggerActions[0].cameraSequence == "Opening",
          "runtime trigger actions");
    Check(scene.cameraZones.size() == 1
          && scene.cameraZones[0].priority == 7,
          "runtime camera zones");
    Check(scene.physicsJoints.size() == 1
          && scene.physicsJoints[0].worldAnchor,
          "runtime physics joints");
    Check(scene.terrains.size() == 1
          && scene.terrains[0].heights.size() == 4
          && scene.terrains[0].paint.size() == 4,
          "runtime terrain data");

    std::filesystem::remove(path);
    std::cout << "runtime AI tests passed\n";
    return 0;
}
