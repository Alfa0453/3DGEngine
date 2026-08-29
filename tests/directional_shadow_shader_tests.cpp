#include "engine/graphics/DirectionalShadowShader.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

bool Require(bool condition, const char* message) {
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

} // namespace

int main() {
    bool ok = true;
    const std::string source =
        "#version 330 core\n//__DIRECTIONAL_SHADOW_IMPLEMENTATION__\nvoid main() {}\n";
    const std::string composed = engine::ComposeDirectionalShadowShader(source);

    ok &= Require(composed.find("//__DIRECTIONAL_SHADOW_IMPLEMENTATION__") == std::string::npos,
                  "composition must consume the insertion marker");
    ok &= Require(composed.find("DIRECTIONAL_BLOCKER_SAMPLES = 16") != std::string::npos,
                  "blocker search must use 16 disk samples");
    ok &= Require(composed.find("DIRECTIONAL_FILTER_SAMPLES = 24") != std::string::npos,
                  "final filtering must use 24 disk samples");
    ok &= Require(composed.find("DIRECTIONAL_POISSON") != std::string::npos,
                  "shared shader must contain the irregular disk distribution");
    ok &= Require(composed.find("stableCell") != std::string::npos,
                  "sample rotation must be stable in shadow-map texel space");
    ok &= Require(composed.find("uCascadeWorldTexelSize") != std::string::npos,
                  "filter radius must be normalized by cascade world texel size");
    ok &= Require(composed.find("cascadeLength * 0.08") != std::string::npos,
                  "cascade selection must include the 8 percent transition region");
    ok &= Require(composed.find("DirectionalReceiverBias") != std::string::npos,
                  "receiver bias must be derived in world-space per cascade");
    ok &= Require(composed.find("ClampDirectionalShadowUv") != std::string::npos,
                  "PCSS samples must use stable clamped border handling");
    ok &= Require(composed.find("if (nextValid)") != std::string::npos,
                  "cascade blending must not blend toward an invalid lit sample");
    ok &= Require(composed.find("DirectionalCascadeDebugColor") != std::string::npos,
                  "the shared shadow code must expose cascade debug colours");
    ok &= Require(composed.find("for (int x = -2") == std::string::npos,
                  "the old square-grid kernel must not return");

    bool rejectedMissingMarker = false;
    try {
        (void)engine::ComposeDirectionalShadowShader("#version 330 core\n");
    } catch (const std::invalid_argument&) {
        rejectedMissingMarker = true;
    }
    ok &= Require(rejectedMissingMarker, "a missing marker must fail loudly");

    return ok ? 0 : 1;
}
