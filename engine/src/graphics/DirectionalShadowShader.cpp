#include "engine/graphics/DirectionalShadowShader.h"

#include <stdexcept>
#include <string_view>

namespace engine {
namespace {

constexpr std::string_view kMarker = "//__DIRECTIONAL_SHADOW_IMPLEMENTATION__";

constexpr std::string_view kDirectionalShadowGlsl = R"GLSL(
const int DIRECTIONAL_BLOCKER_SAMPLES = 16;
const int DIRECTIONAL_FILTER_SAMPLES = 32;

// Interleaved gradient noise (Jimenez 2014): a per-SCREEN-PIXEL value in [0,1). The old
// kernel rotated its sample disk per shadow-map texel, so the rotation was constant across
// every screen pixel a magnified shadow texel covered and its jumps read as coarse blocks.
// Rotating per screen pixel instead turns that into a fine, even grain -- far less
// objectionable, and exactly the residual a temporal (TAA) pass would later resolve to
// perfectly smooth.
float DirectionalIGN(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// Vogel disk: `count` points spiralling out on the golden angle for a near-uniform,
// blue-noise-like distribution at ANY sample count -- much smoother penumbrae than a fixed
// Poisson set of the same size. `phi` rotates the whole spiral (per-pixel, from IGN above).
vec2 DirectionalDiskSample(int i, int count, float phi) {
    float r = sqrt((float(i) + 0.5) / float(count));
    float theta = float(i) * 2.39996323 + phi;   // 2.39996323 = golden angle (radians)
    return r * vec2(cos(theta), sin(theta));
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
    // Slope-scaled depth bias. Faces nearly parallel to the sun (grazing, slope->1 --
    // the cube's vertical sides) have a huge depth gradient across one shadow texel, so
    // they self-shadow into vertical acne streaks unless the bias grows steeply. The
    // grazing multiplier and ceiling are raised so those faces stop striping; flat faces
    // (slope->0) keep a tight bias so contact shadows stay attached.
    float worldBias = max(uCascadeWorldTexelSize[layer], 0.000001)
                    * mix(0.4, 2.75, slope);
    worldBias = clamp(worldBias, 0.00025, 0.05);
    return worldBias / max(uCascadeDepthRange[layer], 0.000001);
}

float DirectionalNormalOffset(int layer, float NdotL) {
    float slope = clamp(1.0 - NdotL, 0.0, 1.0);
    // Push the receiver sample off the surface along its normal, more at grazing angles.
    // Kept more modest than the depth bias above so contact edges do not light-leak.
    return clamp(max(uCascadeWorldTexelSize[layer], 0.000001)
                 * mix(0.5, 1.8, slope), 0.00025, 0.035);
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
    // Per-pixel spiral rotation; +layer decorrelates the cascades at their overlap.
    // Advancing the spiral by the golden ratio each frame (uShadowFrame) makes the residual
    // grain differ every frame so the temporal-AA pass averages it to smooth. uShadowFrame is
    // held at 0 when temporal accumulation is off, so the shadow stays flicker-free there.
    float sampleAngle = (DirectionalIGN(gl_FragCoord.xy)
                        + float(uShadowFrame) * 0.61803398875) * 6.28318530718 + float(layer);

    // Keep the blocker footprint approximately constant in world space. A
    // minimum sub-texel footprint is retained for the coarser far cascades.
    float softness = clamp(uShadowSoftness, 0.1, 12.0);
    float worldTexel = max(uCascadeWorldTexelSize[layer], 0.000001);
    float referenceWorldTexel = max(uCascadeWorldTexelSize[0], 0.000001);
    // No forced-large minimum: near-contact receivers search a tight footprint so the
    // penumbra estimate can collapse toward zero. A small floor keeps the blocker estimate
    // stable on the coarse far cascades without inflating contact shadows.
    float searchRadius = clamp(softness * referenceWorldTexel / worldTexel, 0.35, 4.0);

    float blockerSum = 0.0;
    int blockerCount = 0;
    int blockerSamples = clamp(uShadowBlockerSamples - layer * 2, 4,
                               DIRECTIONAL_BLOCKER_SAMPLES);
    for (int i = 0; i < DIRECTIONAL_BLOCKER_SAMPLES; ++i) {
        if (i >= blockerSamples) break;
        vec2 offset = DirectionalDiskSample(i, blockerSamples, sampleAngle) * texel * searchRadius;
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
    // 0.014 is the directional light angular-size mapping for the existing 0.1..12
    // softness control. Raised from 0.010 so the penumbra widens a little faster with
    // blocker/receiver separation: the shadow's outer edge (large separation) gets soft
    // enough to hide shadow-texel silhouette stair-stepping, while the contact line
    // (separation ~0) is unaffected and stays crisp.
    float receiverDistanceWorld = max(receiverDepth - averageBlockerDepth, 0.0)
                                * max(uCascadeDepthRange[layer], 0.000001);
    float penumbraWorld = receiverDistanceWorld * softness * 0.014;
    // A small minimum radius (~1 texel) keeps the Vogel taps spread over at least a 2x2
    // texel area so the per-tap BilinearShadowCompare has something to anti-alias, giving a
    // clean contact line instead of a single hard-compared texel. Penumbra grows physically
    // above the floor with blocker/receiver separation up to the cap.
    float filterRadius = clamp(penumbraWorld / worldTexel, 1.0, 10.0);

    float shadow = 0.0;
    // Full per-cascade sample budget (up to 32) so the Vogel disk resolves smoothly; the
    // farther cascades step down a little since their penumbrae cover fewer screen pixels.
    int filterSamples = clamp(uShadowFilterSamples - layer * 3, 8,
                              DIRECTIONAL_FILTER_SAMPLES);
    for (int i = 0; i < DIRECTIONAL_FILTER_SAMPLES; ++i) {
        if (i >= filterSamples) break;
        vec2 offset = DirectionalDiskSample(i, filterSamples, sampleAngle) * texel * filterRadius;
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

// Debug: the PCSS filter radius at this fragment, normalized over the 10-texel cap so it
// can be drawn as a heat map. Contact regions read ~0 (small), distant receivers ~1 (wide).
float DirectionalFilterRadiusDebug(float NdotL, vec3 N) {
    float viewDepth = abs((uView * vec4(vWorldPos, 1.0)).z);
    if (viewDepth > uCascadeSplits[3]) return 0.0;
    int layer = 3;
    for (int i = 0; i < 4; ++i) if (viewDepth < uCascadeSplits[i]) { layer = i; break; }
    vec3 shadowWorldPos = vWorldPos + N * DirectionalNormalOffset(layer, NdotL);
    vec4 lp = uCascadeVP[layer] * vec4(shadowWorldPos, 1.0);
    if (abs(lp.w) < 1e-6) return 0.0;
    vec3 projected = lp.xyz / lp.w * 0.5 + 0.5;
    if (projected.z <= 0.0 || projected.z > 1.0
        || any(lessThan(projected.xy, vec2(0.0)))
        || any(greaterThan(projected.xy, vec2(1.0)))) return 0.0;
    float receiverDepth = projected.z;
    float bias = DirectionalReceiverBias(layer, NdotL);
    vec2 texel = 1.0 / vec2(textureSize(uCascadeMaps, 0).xy);
    // Advancing the spiral by the golden ratio each frame (uShadowFrame) makes the residual
    // grain differ every frame so the temporal-AA pass averages it to smooth. uShadowFrame is
    // held at 0 when temporal accumulation is off, so the shadow stays flicker-free there.
    float sampleAngle = (DirectionalIGN(gl_FragCoord.xy)
                        + float(uShadowFrame) * 0.61803398875) * 6.28318530718 + float(layer);
    float softness = clamp(uShadowSoftness, 0.1, 12.0);
    float worldTexel = max(uCascadeWorldTexelSize[layer], 0.000001);
    float referenceWorldTexel = max(uCascadeWorldTexelSize[0], 0.000001);
    float searchRadius = clamp(softness * referenceWorldTexel / worldTexel, 0.35, 4.0);
    float blockerSum = 0.0; int blockerCount = 0;
    int blockerSamples = clamp(uShadowBlockerSamples - layer * 2, 4, DIRECTIONAL_BLOCKER_SAMPLES);
    for (int i = 0; i < DIRECTIONAL_BLOCKER_SAMPLES; ++i) {
        if (i >= blockerSamples) break;
        vec2 offset = DirectionalDiskSample(i, blockerSamples, sampleAngle) * texel * searchRadius;
        float d = texture(uCascadeMaps,
            vec3(ClampDirectionalShadowUv(projected.xy + offset), float(layer))).r;
        if (d < receiverDepth - bias) { blockerSum += d; ++blockerCount; }
    }
    if (blockerCount == 0) return 0.0;
    float avgBlocker = blockerSum / float(blockerCount);
    float receiverDistanceWorld = max(receiverDepth - avgBlocker, 0.0)
                                * max(uCascadeDepthRange[layer], 0.000001);
    float penumbraWorld = receiverDistanceWorld * softness * 0.010;
    return clamp(penumbraWorld / worldTexel, 0.0, 10.0) / 10.0;
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
