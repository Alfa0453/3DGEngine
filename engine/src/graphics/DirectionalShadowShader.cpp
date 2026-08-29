#include "engine/graphics/DirectionalShadowShader.h"

#include <stdexcept>
#include <string_view>

namespace engine {
namespace {

constexpr std::string_view kMarker = "//__DIRECTIONAL_SHADOW_IMPLEMENTATION__";

constexpr std::string_view kDirectionalShadowGlsl = R"GLSL(
const int DIRECTIONAL_BLOCKER_SAMPLES = 16;
const int DIRECTIONAL_FILTER_SAMPLES = 24;

// Irregular unit-disk samples. The centre sample preserves contact detail and
// the remaining points avoid the repeated square clusters of the former 5x5 grid.
const vec2 DIRECTIONAL_POISSON[24] = vec2[](
    vec2( 0.000000,  0.000000), vec2(-0.613392,  0.617481),
    vec2( 0.170019, -0.040254), vec2(-0.299417,  0.791925),
    vec2( 0.645680,  0.493210), vec2(-0.651784,  0.717887),
    vec2( 0.421003,  0.027070), vec2(-0.817194, -0.271096),
    vec2(-0.705374, -0.668203), vec2( 0.977050, -0.108615),
    vec2( 0.063326,  0.142369), vec2( 0.203528,  0.214331),
    vec2(-0.667531,  0.326090), vec2(-0.098422, -0.295755),
    vec2(-0.885922,  0.215369), vec2( 0.566637,  0.605213),
    vec2( 0.039766, -0.396100), vec2( 0.751946,  0.453352),
    vec2( 0.078707, -0.715323), vec2(-0.075838, -0.529344),
    vec2( 0.724479, -0.580798), vec2( 0.222999, -0.215125),
    vec2(-0.467574, -0.405438), vec2(-0.248268, -0.814753)
);

float DirectionalShadowHash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

mat2 DirectionalShadowRotation(vec2 uv, int layer) {
    // Hash the stabilized shadow-map texel, not the frame number. The pattern
    // therefore remains fixed in light space while the camera moves.
    vec2 mapSize = vec2(textureSize(uCascadeMaps, 0).xy);
    vec2 stableCell = floor(uv * mapSize);
    float angle = DirectionalShadowHash(stableCell + vec2(37.0, 101.0) * float(layer))
                * 6.28318530718;
    float c = cos(angle), s = sin(angle);
    return mat2(c, -s, s, c);
}

vec2 ClampDirectionalShadowUv(vec2 uv) {
    ivec2 size = textureSize(uCascadeMaps, 0).xy;
    vec2 halfTexel = 0.5 / vec2(size);
    return clamp(uv, halfTexel, vec2(1.0) - halfTexel);
}

float BilinearShadowCompare(vec2 uv, int layer, float receiverDepth) {
    uv = ClampDirectionalShadowUv(uv);
    ivec2 size = textureSize(uCascadeMaps, 0).xy;
    vec2 texelPosition = uv * vec2(size) - vec2(0.5);
    ivec2 base = ivec2(floor(texelPosition));
    vec2 blend = fract(texelPosition);
    ivec2 maxCoord = size - ivec2(1);
    ivec2 p00 = clamp(base, ivec2(0), maxCoord);
    ivec2 p10 = clamp(base + ivec2(1, 0), ivec2(0), maxCoord);
    ivec2 p01 = clamp(base + ivec2(0, 1), ivec2(0), maxCoord);
    ivec2 p11 = clamp(base + ivec2(1, 1), ivec2(0), maxCoord);
    float s00 = receiverDepth > texelFetch(uCascadeMaps, ivec3(p00, layer), 0).r ? 1.0 : 0.0;
    float s10 = receiverDepth > texelFetch(uCascadeMaps, ivec3(p10, layer), 0).r ? 1.0 : 0.0;
    float s01 = receiverDepth > texelFetch(uCascadeMaps, ivec3(p01, layer), 0).r ? 1.0 : 0.0;
    float s11 = receiverDepth > texelFetch(uCascadeMaps, ivec3(p11, layer), 0).r ? 1.0 : 0.0;
    return mix(mix(s00, s10, blend.x), mix(s01, s11, blend.x), blend.y);
}

float DirectionalReceiverBias(int layer, float NdotL) {
    float slope = clamp(1.0 - NdotL, 0.0, 1.0);
    float worldBias = max(uCascadeWorldTexelSize[layer], 0.000001)
                    * mix(0.35, 1.25, slope);
    worldBias = clamp(worldBias, 0.00025, 0.025);
    return worldBias / max(uCascadeDepthRange[layer], 0.000001);
}

float DirectionalNormalOffset(int layer, float NdotL) {
    float slope = clamp(1.0 - NdotL, 0.0, 1.0);
    return clamp(max(uCascadeWorldTexelSize[layer], 0.000001)
                 * mix(0.45, 1.35, slope), 0.00025, 0.025);
}

float SampleDirectionalCascade(vec3 worldPosition, vec3 normal, float NdotL,
                               int layer, out bool valid) {
    vec3 shadowWorldPos = worldPosition + normal * DirectionalNormalOffset(layer, NdotL);
    vec4 lightPosition = uCascadeVP[layer] * vec4(shadowWorldPos, 1.0);
    valid = abs(lightPosition.w) > 0.000001;
    if (!valid) return 0.0;
    vec3 projected = lightPosition.xyz / lightPosition.w * 0.5 + 0.5;
    if (projected.z <= 0.0 || projected.z > 1.0 ||
        any(lessThan(projected.xy, vec2(0.0))) ||
        any(greaterThan(projected.xy, vec2(1.0)))) { valid = false; return 0.0; }

    float receiverDepth = projected.z;
    float bias = DirectionalReceiverBias(layer, NdotL);
    vec2 texel = 1.0 / vec2(textureSize(uCascadeMaps, 0).xy);
    mat2 rotation = DirectionalShadowRotation(projected.xy, layer);

    // Keep the blocker footprint approximately constant in world space. A
    // minimum sub-texel footprint is retained for the coarser far cascades.
    float softness = clamp(uShadowSoftness, 0.1, 12.0);
    float worldTexel = max(uCascadeWorldTexelSize[layer], 0.000001);
    float referenceWorldTexel = max(uCascadeWorldTexelSize[0], 0.000001);
    float searchRadius = clamp(softness * referenceWorldTexel / worldTexel, 0.75, 4.0);

    float blockerSum = 0.0;
    int blockerCount = 0;
    int blockerSamples = clamp(uShadowBlockerSamples - layer * 2, 4,
                               DIRECTIONAL_BLOCKER_SAMPLES);
    for (int i = 0; i < DIRECTIONAL_BLOCKER_SAMPLES; ++i) {
        if (i >= blockerSamples) break;
        vec2 offset = rotation * DIRECTIONAL_POISSON[i] * texel * searchRadius;
        vec2 sampleUv = ClampDirectionalShadowUv(projected.xy + offset);
        float sampleDepth = texture(uCascadeMaps, vec3(sampleUv, float(layer))).r;
        if (sampleDepth < receiverDepth - bias) {
            blockerSum += sampleDepth;
            ++blockerCount;
        }
    }
    if (blockerCount == 0) return 0.0;

    float averageBlockerDepth = blockerSum / float(blockerCount);
    // Orthographic cascade depth is linear. Convert the normalized separation
    // back into world units before expressing the penumbra in cascade texels.
    // 0.010 is the directional light angular-size mapping for the existing
    // 0.1..12 softness control; it replaces the aggressive arbitrary x300 step.
    float receiverDistanceWorld = max(receiverDepth - averageBlockerDepth, 0.0)
                                * max(uCascadeDepthRange[layer], 0.000001);
    float penumbraWorld = receiverDistanceWorld * softness * 0.010;
    float filterRadius = clamp(penumbraWorld / worldTexel, 0.75, 10.0);

    float shadow = 0.0;
    int filterSamples = clamp(uShadowFilterSamples - layer * 3, 6,
                              DIRECTIONAL_FILTER_SAMPLES);
    for (int i = 0; i < DIRECTIONAL_FILTER_SAMPLES; ++i) {
        if (i >= filterSamples) break;
        vec2 offset = rotation * DIRECTIONAL_POISSON[i] * texel * filterRadius;
        shadow += BilinearShadowCompare(projected.xy + offset, layer,
                                        receiverDepth - bias);
    }
    return clamp(shadow / float(filterSamples), 0.0, 1.0);
}

float ShadowFactor(float NdotL, vec3 N) {
    float viewDepth = abs((uView * vec4(vWorldPos, 1.0)).z);
    if (viewDepth > uCascadeSplits[3]) return 0.0;

    int layer = 3;
    for (int i = 0; i < 4; ++i) {
        if (viewDepth < uCascadeSplits[i]) { layer = i; break; }
    }

    bool valid = false;
    float shadow = SampleDirectionalCascade(vWorldPos, N, NdotL, layer, valid);
    if (!valid) {
        for (int fallback = 1; fallback < 4; ++fallback) {
            int fallbackLayer = layer + fallback;
            if (fallbackLayer >= 4) break;
            shadow = SampleDirectionalCascade(vWorldPos, N, NdotL, fallbackLayer, valid);
            if (valid) { layer = fallbackLayer; break; }
        }
    }
    if (!valid) return 0.0;
    if (layer < 3) {
        float cascadeNear = (layer == 0) ? 0.0 : uCascadeSplits[layer - 1];
        float cascadeLength = max(uCascadeSplits[layer] - cascadeNear, 0.0001);
        float blendStart = uCascadeSplits[layer] - cascadeLength * 0.08;
        float blend = smoothstep(blendStart, uCascadeSplits[layer], viewDepth);
        if (blend > 0.0) {
            bool nextValid = false;
            float nextShadow = SampleDirectionalCascade(vWorldPos, N, NdotL,
                                                         layer + 1, nextValid);
            if (nextValid) shadow = mix(shadow, nextShadow, blend);
        }
    }
    return shadow;
}

int DirectionalCascadeIndex(vec3 worldPosition) {
    float viewDepth = abs((uView * vec4(worldPosition, 1.0)).z);
    if (viewDepth > uCascadeSplits[3]) return -1;
    for (int i = 0; i < 4; ++i) if (viewDepth < uCascadeSplits[i]) return i;
    return -1;
}

vec3 DirectionalCascadeDebugColor(vec3 worldPosition) {
    int layer = DirectionalCascadeIndex(worldPosition);
    if (layer == 0) return vec3(1.0, 0.18, 0.12);
    if (layer == 1) return vec3(0.18, 1.0, 0.20);
    if (layer == 2) return vec3(0.15, 0.35, 1.0);
    if (layer == 3) return vec3(1.0, 0.82, 0.12);
    return vec3(0.0);
}
)GLSL";

} // namespace

std::string ComposeDirectionalShadowShader(const std::string& fragmentSource) {
    const std::size_t position = fragmentSource.find(kMarker);
    if (position == std::string::npos) {
        throw std::invalid_argument("Directional shadow shader marker is missing");
    }
    std::string result = fragmentSource;
    result.replace(position, kMarker.size(), kDirectionalShadowGlsl);
    return result;
}

} // namespace engine
