#include "ProceduralBuildingPanel.h"

#include <cstdlib>
#include <iostream>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    ProceduralBuildingPanel panel;
    panel.SetFootprint({{-4.0f, -3.0f}, {4.0f, -3.0f},
                        {4.0f, 3.0f}, {-4.0f, 3.0f}});
    panel.SetStoreys(1);
    panel.SetOpenings({});
    auto parts = panel.GenerateParts();
    Require(parts.size() == 7, "one-storey shell should contain floor, ceiling, four walls, roof");

    ProceduralBuildingPanel::Opening door;
    door.segment = 0;
    door.storey = 0;
    door.width = 1.2f;
    door.height = 2.2f;
    panel.SetOpenings({door});
    parts = panel.GenerateParts();
    Require(parts.size() == 9, "door must split one wall into left, right, and lintel pieces");

    panel.SetOpenings({});
    panel.SetStoreys(2);
    parts = panel.GenerateParts();
    Require(parts.size() == 13, "two-storey shell should regenerate deterministic geometry");
    Require(parts.back().suffix == "Roof", "roof should remain the final generated surface");

    panel.SetFootprint({{0.0f, 0.0f}, {0.01f, 0.0f}, {0.0f, 0.01f}});
    Require(panel.GenerateParts().empty(), "degenerate footprints must not generate geometry");
    std::cout << "Procedural building tests passed\n";
    return 0;
}
