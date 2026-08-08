#include "engine/graphics/Water.h"

#include "engine/graphics/Shader.h"
#include "engine/graphics/Camera.h"
#include "engine/graphics/IBL.h"
#include "engine/graphics/Frustum.h"
#include "engine/graphics/VertexLayout.h"
#include "engine/math/Spline.h"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace engine {
namespace {

// Shared value-noise + "sea octave" helpers (Seascape style). Duplicated into both
// stages because GLSL compiles each stage independently.
const char* kWaterNoise = R"glsl(
const mat2 octave_m = mat2(1.6, 1.2, -1.2, 1.6);

float hash12(vec2 p) {
    uvec2 q = uvec2(ivec2(floor(p))) * uvec2(1597334677u, 3812015801u);
    uint n = (q.x ^ q.y) * 1597334677u;
    return float(n) * (1.0 / 4294967295.0);
}
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return -1.0 + 2.0 * mix(mix(hash12(i + vec2(0.0, 0.0)), hash12(i + vec2(1.0, 0.0)), u.x),
                            mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), u.x), u.y);
}
float sea_octave(vec2 uv, float choppy) {
    uv += noise(uv);
    vec2 wv  = 1.0 - abs(sin(uv));
    vec2 swv = abs(cos(uv));
    wv = mix(wv, swv, wv);
    return pow(1.0 - pow(wv.x * wv.y, 0.65), choppy);
}
// Summed sea octaves -> surface height (world units) at an XZ point.
float sea_height(vec2 xz, float time, float seaHeight, float seaChoppy,
                 float seaSpeed, float seaFreq, int iters) {
    float freq   = seaFreq;
    float amp    = seaHeight;
    float choppy = seaChoppy;
    vec2  uv = xz; uv.x *= 0.75;
    float h = 0.0;
    for (int i = 0; i < iters; ++i) {
        float d  = sea_octave((uv + time * seaSpeed) * freq, choppy);
        d       += sea_octave((uv - time * seaSpeed) * freq, choppy);
        h += d * amp;
        uv = uv * octave_m;
        freq *= 1.9;
        amp  *= 0.22;
        choppy = mix(choppy, 1.0, 0.2);
    }
    return h;
}
)glsl";

constexpr const char* kWaterContactUniforms[Water::kMaxContacts] = {
    "uContacts[0]", "uContacts[1]", "uContacts[2]", "uContacts[3]",
    "uContacts[4]", "uContacts[5]", "uContacts[6]", "uContacts[7]",
    "uContacts[8]", "uContacts[9]", "uContacts[10]", "uContacts[11]",
    "uContacts[12]", "uContacts[13]", "uContacts[14]", "uContacts[15]"
};

const char* kWaterVertBody = R"glsl(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aTangent;

uniform mat4  uViewProj;
uniform vec3  uCenter;       // world centre (xz offset + y = calm level)
uniform float uTime;
uniform float uSeaHeight;
uniform float uSeaChoppy;
uniform float uSeaSpeed;
uniform float uSeaFreq;
uniform vec2  uFlowDir;
uniform float uFlowStrength;
uniform int   uSplineRibbon;
uniform float uRiverWidth;
uniform float uRiverLength;

out vec3 vWorldPos;
out vec2 vFlowDir;
out vec3 vBaseNormal;
out vec2 vSurfaceCoord;
out vec3 vSurfaceTangent;
out vec3 vSurfaceSide;

void main() {
    vec2 xz = vec2(aPos.x + uCenter.x, aPos.z + uCenter.z);
    // Ribbon vertices carry their local spline tangent separately. Square water
    // keeps the old uniform flow direction.
    vec3 tangent3 = length(aTangent) > 0.001 ? normalize(aTangent) : vec3(0.0, 0.0, 1.0);
    vec3 normal3 = length(aNormal) > 0.001 ? normalize(aNormal) : vec3(0.0, 1.0, 0.0);
    vec3 side3 = normalize(cross(normal3, tangent3));
    vec2 meshFlow = length(tangent3.xz) > 0.001 ? normalize(tangent3.xz) : uFlowDir;
    vFlowDir = meshFlow;
    vBaseNormal = normal3;
    vSurfaceTangent = tangent3;
    vSurfaceSide = side3;
    // A spline-local coordinate system keeps the phase continuous around corners.
    // Using world XZ with a different tangent on each row tears the flow at bends.
    vSurfaceCoord = uSplineRibbon != 0
        ? vec2((aUV.x - 0.5) * uRiverWidth, aUV.y * uRiverLength)
        : xz;
    vec2 sampleXZ = vSurfaceCoord
        - (uSplineRibbon != 0 ? vec2(0.0, 1.0) : meshFlow)
            * uTime * uFlowStrength;
    // 3 octaves for geometry (cheaper); the fragment adds detail via more octaves.
    float h = sea_height(sampleXZ, uTime, uSeaHeight, uSeaChoppy, uSeaSpeed, uSeaFreq, 3);
    // aPos.y carries the authored river elevation relative to uCenter. Square water
    // has aPos.y == 0, while spline ribbons use it to climb and descend with the path.
    vec3 P = vec3(xz.x, uCenter.y + aPos.y + h, xz.y);
    vWorldPos = P;
    gl_Position = uViewProj * vec4(P, 1.0);
}
)glsl";

// Fragment declarations: every `in` varying and `uniform` the engine binds, plus the
// FragColor output. This block is ALWAYS prepended (to both the built-in fragment body
// and any custom water shader) so a custom shader has the full water interface available
// without re-declaring anything — it only writes helper functions + main().
const char* kWaterFragDecls = R"glsl(
in vec3 vWorldPos;
in vec2 vFlowDir;
in vec3 vBaseNormal;
in vec2 vSurfaceCoord;
in vec3 vSurfaceTangent;
in vec3 vSurfaceSide;

uniform vec3  uCamPos;
uniform vec3  uSunDir;       // travel direction of the sun light
uniform vec3  uSunColor;
uniform vec3  uAmbient;
uniform vec3  uShallow;
uniform vec3  uDeep;
uniform vec3  uReflection;   // sky tint mixed in by Fresnel
uniform float uFresnelPower;
uniform float uSpecStrength;
uniform float uShininess;
uniform float uTransparency;
uniform sampler2D uSceneColor;
uniform int   uHasSceneColor;
uniform sampler2D uSceneDepth;
uniform int   uHasSceneDepth;
uniform samplerCube uEnvironment;
uniform int   uHasEnvironment;
uniform float uMaxReflectionLod;
uniform float uRefractionStrength;
uniform float uReflectionRoughness;
uniform float uEnvironmentReflectionStrength;
uniform float uAbsorptionStrength;
uniform float uCausticsStrength;
uniform float uCausticsScale;
uniform vec2  uViewportSize;
uniform float uNearPlane;
uniform float uFarPlane;
uniform float uDepthFadeDistance;
uniform float uShoreFoamWidth;
uniform float uShoreFoamStrength;
uniform float uTime;
uniform float uSeaHeight;
uniform float uSeaChoppy;
uniform float uSeaSpeed;
uniform float uSeaFreq;
uniform vec2  uFlowDir;
uniform float uFlowStrength;
uniform int   uSplineRibbon;
uniform vec3  uFoamColor;
uniform float uFoamAmount;

const int kMaxContacts = 16;
uniform int   uContactCount;
uniform vec4  uContacts[kMaxContacts];   // xy = world XZ centre, z = radius, w = strength

out vec4 FragColor;
)glsl";

// Default surface implementation: helper functions + main(). Swapped out wholesale when a
// water object supplies a custom fragment shader (which provides its own helpers + main).
const char* kWaterFragImpl = R"glsl(
// Detailed normal from the height field via central differences of sea_height.
vec3 waterNormal(vec2 xz, float eps) {
    float h  = sea_height(xz,                 uTime, uSeaHeight, uSeaChoppy, uSeaSpeed, uSeaFreq, 5);
    float hx = sea_height(xz + vec2(eps, 0.0), uTime, uSeaHeight, uSeaChoppy, uSeaSpeed, uSeaFreq, 5);
    float hz = sea_height(xz + vec2(0.0, eps), uTime, uSeaHeight, uSeaChoppy, uSeaSpeed, uSeaFreq, 5);
    return normalize(vec3(h - hx, eps, h - hz));
}

float linearEyeDepth(float depth) {
    float z = depth * 2.0 - 1.0;
    return (2.0 * uNearPlane * uFarPlane)
        / max(uFarPlane + uNearPlane - z * (uFarPlane - uNearPlane), 0.0001);
}

// Screen-space reflection of the OPAQUE scene, reusing the colour + depth buffers already
// bound for refraction -- so it reflects real scene objects with no extra scene pass. Marches
// the reflection ray in world space, projecting each step to screen space and comparing eye
// depths; a hit within a thin band returns that pixel's colour. Misses fall back to the sky.
uniform mat4  uViewProj;       // also declared in the vertex stage; same program uniform
uniform int   uSsrEnabled;
uniform float uSsrStrength;
uniform float uSsrDistance;    // world-space march length
uniform float uSsrThickness;   // eye-space depth band that counts as a hit
bool ssrReflect(vec3 P, vec3 R, out vec3 hitColor) {
    hitColor = vec3(0.0);
    const int STEPS = 20;
    float stepLen = max(uSsrDistance, 0.5) / float(STEPS);
    vec3 pos = P + R * (stepLen * 0.5);
    for (int i = 0; i < STEPS; ++i) {
        pos += R * stepLen;
        vec4 clip = uViewProj * vec4(pos, 1.0);
        if (clip.w <= 0.0) return false;
        vec3 ndc = clip.xyz / clip.w;
        vec2 uv = ndc.xy * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return false;  // off screen
        float sceneD = texture(uSceneDepth, uv).r;
        if (sceneD >= 0.999999) continue;                       // sky pixel -> keep marching
        float diff = linearEyeDepth(ndc.z * 0.5 + 0.5) - linearEyeDepth(sceneD);
        if (diff > 0.0 && diff < uSsrThickness) {                // ray just behind the surface
            hitColor = texture(uSceneColor, uv).rgb;
            return true;
        }
    }
    return false;
}

void main() {
    vec2 xz = vWorldPos.xz;
    float dist = length(uCamPos - vWorldPos);
    // Distance-based LOD: widen the sampling step far away to kill shimmer/aliasing.
    float eps = max(0.02, dist * 0.004);
    // Match the vertex flow scroll so the shaded normals travel with the current.
    vec2 flow = length(vFlowDir) > 0.001 ? normalize(vFlowDir) : uFlowDir;
    vec2 sampleCoord = vSurfaceCoord
        - (uSplineRibbon != 0 ? vec2(0.0, 1.0) : flow)
            * uTime * uFlowStrength;
    vec3 waveN = waterNormal(sampleCoord, eps);
    // Preserve the smoothly interpolated ribbon normal, then layer the animated
    // wave slope over it. This removes faceted lighting on vertical/rolled bends.
    vec3 baseN = length(vBaseNormal) > 0.001 ? normalize(vBaseNormal) : vec3(0.0, 1.0, 0.0);
    vec3 N = uSplineRibbon != 0
        ? normalize(normalize(vSurfaceSide) * waveN.x
                    + baseN * max(waveN.y, 0.15)
                    + normalize(vSurfaceTangent) * waveN.z)
        : normalize(baseN * max(waveN.y, 0.15) + vec3(waveN.x, 0.0, waveN.z));

    vec3 V = normalize(uCamPos - vWorldPos);
    if (dot(N, V) < 0.0) N = -N; // render the underside correctly when submerged
    float ndv = max(dot(N, V), 0.0);

    float fresnel = pow(1.0 - ndv, uFresnelPower);
    vec2 screenUv = gl_FragCoord.xy / max(uViewportSize, vec2(1.0));
    // Opaque scene depth below the water. Unlike view-angle tinting, this makes banks,
    // submerged terrain, and props genuinely shallow while open water becomes deep.
    float waterDepth = uDepthFadeDistance * 4.0;
    float hasBottom = 0.0;
    if (uHasSceneDepth != 0) {
        float opaqueDepth = texture(uSceneDepth, screenUv).r;
        if (opaqueDepth < 0.999999) {
            waterDepth = max(linearEyeDepth(opaqueDepth)
                           - linearEyeDepth(gl_FragCoord.z), 0.0);
            hasBottom = 1.0;
        }
    }
    float depthMix = 1.0 - exp(-waterDepth / max(uDepthFadeDistance, 0.01));
    vec3  body  = mix(uShallow, uDeep, clamp(depthMix, 0.0, 1.0));

    // Distort the already-rendered opaque scene through the animated wave normal.
    // Near banks the displacement is reduced so the shoreline remains stable. If
    // the displaced sample crosses in front of the water, fall back to the original
    // pixel to avoid pulling foreground silhouettes into the surface.
    vec3 transmitted = body;
    if (uHasSceneColor != 0) {
        float refractionDepthScale = mix(0.25, 1.0, clamp(depthMix, 0.0, 1.0));
        vec2 refractUv = clamp(screenUv + N.xz * uRefractionStrength * refractionDepthScale,
                               vec2(0.001), vec2(0.999));
        if (uHasSceneDepth != 0) {
            float displacedDepth = texture(uSceneDepth, refractUv).r;
            if (displacedDepth + 0.00005 < gl_FragCoord.z) refractUv = screenUv;
        }
        vec3 sceneBehind = texture(uSceneColor, refractUv).rgb;
        float causticA = sin((xz.x + xz.y) * uCausticsScale + uTime * 1.9);
        float causticB = sin((xz.x - xz.y) * uCausticsScale * 1.27 - uTime * 1.4);
        float caustics = pow(max(0.0, 1.0 - abs(causticA + causticB) * 0.55), 7.0);
        sceneBehind += uFoamColor * caustics * uCausticsStrength
                       * hasBottom * (1.0 - depthMix);
        float absorption = clamp(depthMix * uAbsorptionStrength, 0.0, 1.0);
        transmitted = mix(sceneBehind, body, absorption);
    }

    vec3 reflectionDir = reflect(-V, N);
    vec3 reflected = uReflection;
    if (uHasEnvironment != 0) {
        vec3 environment = textureLod(
            uEnvironment, reflectionDir,
            clamp(uReflectionRoughness, 0.0, 1.0) * uMaxReflectionLod).rgb;
        reflected = mix(uReflection, environment,
                        clamp(uEnvironmentReflectionStrength, 0.0, 1.0));
    }
    // Screen-space scene reflection on top of the sky/environment (cheap: reuses the bound
    // opaque buffers). Faded near the screen edges so reflections don't pop at the borders;
    // off-screen / missed rays keep the environment reflection, so it always looks sensible.
    if (uSsrEnabled != 0 && uHasSceneColor != 0 && uHasSceneDepth != 0) {
        vec3 ssrColor;
        if (ssrReflect(vWorldPos, reflectionDir, ssrColor)) {
            vec2 edge = abs(screenUv - 0.5) * 2.0;
            float fade = clamp(1.0 - pow(max(edge.x, edge.y), 4.0), 0.0, 1.0);
            reflected = mix(reflected, ssrColor, clamp(uSsrStrength, 0.0, 1.0) * fade);
        }
    }
    vec3 color = mix(transmitted, reflected, fresnel * 0.72);

    // Sun glint (Blinn-Phong), tone-limited so it sparkles instead of blowing out.
    vec3 L = normalize(-uSunDir);
    vec3 H = normalize(V + L);
    float spec = pow(max(dot(N, H), 0.0), uShininess) * uSpecStrength;
    color += uSunColor * min(spec, 1.0);
    color += uAmbient * body * 0.2;

    // Stylised crest foam: appears where the surface tilts away from vertical (crests),
    // broken up by animated noise into organic clumps.
    float crest = smoothstep(0.78, 0.55, N.y);            // 0 flat .. 1 steep crest
    float foamNoise = noise(xz * 1.1 + uTime * 0.12) * 0.5 + 0.5;
    float foam = clamp(crest * uFoamAmount * smoothstep(0.30, 0.72, foamNoise) * 1.8, 0.0, 1.0);

    // Automatic shoreline foam follows the real depth intersection instead of a
    // manually placed mask. Noise breaks up the otherwise uniform bank line.
    float shore = hasBottom
        * (1.0 - smoothstep(0.0, max(uShoreFoamWidth, 0.01), waterDepth));
    shore *= uShoreFoamStrength
        * smoothstep(0.18, 0.78, noise(xz * 1.7 - uTime * 0.22) * 0.5 + 0.5);
    foam = clamp(max(foam, shore), 0.0, 1.0);
    color = mix(color, uFoamColor, foam);

    // Contact foam: a bright, animated ring where objects pierce the surface -- the
    // visual cue that the water is touching something.
    float contact = 0.0;
    for (int i = 0; i < uContactCount; ++i) {
        vec2  c = uContacts[i].xy;
        float r = uContacts[i].z;
        float s = uContacts[i].w;
        float d = length(xz - c);
        // Band hugging the object edge (inner fade -> peak at r -> outer fade).
        float ring = smoothstep(r + 1.1, r + 0.15, d) * smoothstep(r - 0.9, r - 0.05, d);
        float n = noise(xz * 2.3 - uTime * 0.5) * 0.5 + 0.5;   // break the ring into clumps
        contact = max(contact, ring * s * smoothstep(0.20, 0.75, n));
    }
    contact = clamp(contact, 0.0, 1.0);
    color = mix(color, uFoamColor, contact);

    // Shallower water exposes more of the scene below it; deep water retains the
    // authored opacity. Fresnel, foam, and contact rings remain opaque highlights.
    float depthAlpha = mix(uTransparency * 0.42, uTransparency, depthMix);
    float alpha = clamp(depthAlpha + fresnel * (1.0 - depthAlpha) + foam + contact, 0.0, 1.0);
    FragColor = vec4(color, alpha);
}
)glsl";

// Assembled at construction (version + shared noise + stage body).
std::string BuildVert() {
    return std::string("#version 330 core\n") + kWaterNoise + kWaterVertBody;
}
std::string BuildFrag() {
    return std::string("#version 330 core\n") + kWaterNoise + kWaterFragDecls + kWaterFragImpl;
}
// A custom water shader supplies only the fragment body (helpers + main). The engine
// prepends the version, shared noise helpers, and the full declaration block so every
// water uniform / varying is already in scope.
std::string BuildCustomFrag(const std::string& body) {
    return std::string("#version 330 core\n") + kWaterNoise + kWaterFragDecls + body;
}

} // namespace

Water::Water(const WaterConfig& config) : m_config(config) {
    const std::string vert = BuildVert();
    const std::string frag = BuildFrag();
    m_shader = std::make_unique<Shader>(vert.c_str(), frag.c_str());
    RebuildCustomShader();
    BuildMesh();
}

Water::~Water() = default;

void Water::RebuildCustomShader() {
    m_customShader.reset();
    m_customShaderError.clear();
    if (m_config.customFragmentSource.empty()) return;
    const std::string vert = BuildVert();
    const std::string frag = BuildCustomFrag(m_config.customFragmentSource);
    ShaderCompileReport report;
    std::unique_ptr<Shader> compiled = Shader::TryCompile(vert, frag, report);
    if (compiled && report.success) {
        m_customShader = std::move(compiled);
    } else {
        // Keep rendering with the built-in shader; surface the first diagnostic.
        m_customShaderError = report.diagnostics.empty()
            ? std::string("Custom water shader failed to compile.")
            : report.diagnostics.front().message;
    }
}

void Water::SetConfig(const WaterConfig& config) {
    bool splineChanged = config.splineClosed != m_config.splineClosed
        || config.riverWidth != m_config.riverWidth
        || config.splinePoints.size() != m_config.splinePoints.size()
        || config.splinePointRotations.size() != m_config.splinePointRotations.size();
    if (!splineChanged) {
        for (std::size_t i = 0; i < config.splinePoints.size(); ++i) {
            const glm::vec3 delta = config.splinePoints[i] - m_config.splinePoints[i];
            if (glm::dot(delta, delta) > 1.0e-8f) { splineChanged = true; break; }
        }
    }
    if (!splineChanged) {
        for (std::size_t i = 0; i < config.splinePointRotations.size(); ++i) {
            const glm::vec3 delta = config.splinePointRotations[i]
                - m_config.splinePointRotations[i];
            if (glm::dot(delta, delta) > 1.0e-8f) { splineChanged = true; break; }
        }
    }
    const bool rebuild = config.size != m_config.size
        || config.resolution != m_config.resolution
        || config.center != m_config.center || splineChanged;
    const bool customShaderChanged =
        config.customFragmentSource != m_config.customFragmentSource;
    m_config = config;
    if (customShaderChanged) RebuildCustomShader();
    if (rebuild || !m_mesh) BuildMesh();
}

void Water::BuildMesh() {
    const int res = (m_config.resolution < 1) ? 1 : m_config.resolution;
    m_splineLength = 0.0f;
    m_spline.SetPoints(m_config.splinePoints);
    m_spline.SetClosed(m_config.splineClosed);
    std::vector<glm::vec3> flatPoints = m_config.splinePoints;
    for (glm::vec3& point : flatPoints) point.y = 0.0f;
    m_flatSpline.SetPoints(std::move(flatPoints));
    m_flatSpline.SetClosed(m_config.splineClosed);
    if (m_config.splinePoints.size() >= 2) {
        const float length = std::max(m_spline.Length(), 0.01f);
        m_splineLength = length;
        const float width = std::max(m_config.riverWidth, 0.1f);
        // A river needs substantially more longitudinal vertices than a square patch:
        // every row rotates to the spline tangent, so this density is what makes the
        // plane visibly flexible through tight bends. Resolution remains the quality
        // control, but rivers may use up to 4x that value along their length.
        const int maximumAlong = std::max(res * 4, 64);
        const int along = std::clamp(
            std::max(static_cast<int>(m_config.splinePoints.size()) * 24,
                     static_cast<int>(std::ceil(length * 3.0f))), 16, maximumAlong);
        const int across = std::clamp(
            static_cast<int>(std::ceil(width * 1.5f)), 3, 24);
        std::vector<glm::vec3> centers(static_cast<std::size_t>(along + 1));
        for (int z = 0; z <= along; ++z) {
            const float d = length * static_cast<float>(z) / static_cast<float>(along);
            centers[static_cast<std::size_t>(z)] = m_spline.PositionAtDistance(d);
        }
        // Prevent the two banks crossing at turns tighter than half the authored
        // width. A short smoothing pass makes the narrowing gradual rather than a
        // visible pinch at a single row.
        std::vector<float> rowHalfWidths(static_cast<std::size_t>(along + 1), width * 0.5f);
        for (int z = 1; z < along; ++z) {
            glm::vec3 before = centers[static_cast<std::size_t>(z)]
                - centers[static_cast<std::size_t>(z - 1)];
            glm::vec3 after = centers[static_cast<std::size_t>(z + 1)]
                - centers[static_cast<std::size_t>(z)];
            const float beforeLength = glm::length(before);
            const float afterLength = glm::length(after);
            if (beforeLength <= 1.0e-5f || afterLength <= 1.0e-5f) continue;
            before /= beforeLength;
            after /= afterLength;
            const float angle = std::acos(std::clamp(glm::dot(before, after), -1.0f, 1.0f));
            if (angle > 1.0e-4f) {
                const float radius = ((beforeLength + afterLength) * 0.5f) / angle;
                rowHalfWidths[static_cast<std::size_t>(z)] = std::min(
                    rowHalfWidths[static_cast<std::size_t>(z)], std::max(radius * 0.82f, width * 0.12f));
            }
        }
        for (int pass = 0; pass < 4; ++pass) {
            std::vector<float> smoothed = rowHalfWidths;
            for (int z = 1; z < along; ++z) {
                smoothed[static_cast<std::size_t>(z)] =
                    rowHalfWidths[static_cast<std::size_t>(z - 1)] * 0.25f
                    + rowHalfWidths[static_cast<std::size_t>(z)] * 0.5f
                    + rowHalfWidths[static_cast<std::size_t>(z + 1)] * 0.25f;
            }
            rowHalfWidths.swap(smoothed);
        }
        std::vector<float> verts;
        verts.reserve(static_cast<std::size_t>(along + 1) * (across + 1) * 11);
        glm::vec3 transportedSide(1.0f, 0.0f, 0.0f);
        glm::vec3 previousTangent(0.0f, 0.0f, 1.0f);
        for (int z = 0; z <= along; ++z) {
            const glm::vec3 center = centers[static_cast<std::size_t>(z)];
            const int tangentLo = std::max(z - 2, 0);
            const int tangentHi = std::min(z + 2, along);
            glm::vec3 tangent = centers[static_cast<std::size_t>(tangentHi)]
                - centers[static_cast<std::size_t>(tangentLo)];
            if (glm::dot(tangent, tangent) < 1.0e-8f) tangent = glm::vec3(0.0f, 0.0f, 1.0f);
            else tangent = glm::normalize(tangent);

            if (z == 0) {
                transportedSide = glm::cross(tangent, glm::vec3(0.0f, 1.0f, 0.0f));
                if (glm::dot(transportedSide, transportedSide) < 1.0e-8f)
                    transportedSide = glm::cross(tangent, glm::vec3(0.0f, 0.0f, 1.0f));
                transportedSide = glm::normalize(transportedSide);
            } else {
                // Rotation-minimising parallel transport avoids the visible twisting
                // introduced by repeatedly projecting the frame at steep bends.
                glm::vec3 axis = glm::cross(previousTangent, tangent);
                const float sine = glm::length(axis);
                const float cosine = std::clamp(glm::dot(previousTangent, tangent), -1.0f, 1.0f);
                if (sine > 1.0e-6f) {
                    axis /= sine;
                    transportedSide = transportedSide * cosine
                        + glm::cross(axis, transportedSide) * sine
                        + axis * glm::dot(axis, transportedSide) * (1.0f - cosine);
                }
                transportedSide -= tangent * glm::dot(transportedSide, tangent);
                if (glm::dot(transportedSide, transportedSide) < 1.0e-8f)
                    transportedSide = glm::cross(tangent, glm::vec3(0.0f, 1.0f, 0.0f));
                transportedSide = glm::normalize(transportedSide);
            }
            previousTangent = tangent;

            float rollDegrees = 0.0f;
            if (!m_config.splinePointRotations.empty()) {
                const std::size_t count = m_config.splinePointRotations.size();
                const float progress = static_cast<float>(z) / static_cast<float>(along);
                const float keyed = progress * static_cast<float>(m_config.splineClosed ? count : count - 1);
                const std::size_t lo = std::min(static_cast<std::size_t>(std::floor(keyed)), count - 1);
                const std::size_t hi = m_config.splineClosed ? (lo + 1) % count : std::min(lo + 1, count - 1);
                float f = keyed - std::floor(keyed);
                f = f * f * (3.0f - 2.0f * f);
                const float a = m_config.splinePointRotations[lo].z;
                const float delta = std::remainder(m_config.splinePointRotations[hi].z - a, 360.0f);
                rollDegrees = a + delta * f;
            }
            const float roll = glm::radians(rollDegrees);
            glm::vec3 side = transportedSide * std::cos(roll)
                + glm::cross(tangent, transportedSide) * std::sin(roll);
            glm::vec3 surfaceNormal = glm::normalize(glm::cross(tangent, side));
            if (surfaceNormal.y < 0.0f) surfaceNormal = -surfaceNormal;
            for (int x = 0; x <= across; ++x) {
                const float u = static_cast<float>(x) / static_cast<float>(across);
                const glm::vec3 world = center + side * ((u * 2.0f - 1.0f)
                    * rowHalfWidths[static_cast<std::size_t>(z)]);
                const glm::vec3 local = world - m_config.center;
                verts.insert(verts.end(), {local.x, local.y, local.z,
                    surfaceNormal.x, surfaceNormal.y, surfaceNormal.z,
                    u, static_cast<float>(z) / static_cast<float>(along),
                    tangent.x, tangent.y, tangent.z});
            }
        }
        std::vector<std::uint32_t> indices;
        indices.reserve(static_cast<std::size_t>(along) * across * 6);
        const int stride = across + 1;
        for (int z = 0; z < along; ++z) {
            for (int x = 0; x < across; ++x) {
                const std::uint32_t i0 = static_cast<std::uint32_t>(z * stride + x);
                const std::uint32_t i1 = i0 + 1;
                const std::uint32_t i2 = static_cast<std::uint32_t>((z + 1) * stride + x);
                const std::uint32_t i3 = i2 + 1;
                indices.insert(indices.end(), {i0, i2, i1, i1, i2, i3});
            }
        }
        glm::vec3 boundsMin(std::numeric_limits<float>::max());
        glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
        const float verticalPadding = std::abs(m_config.seaHeight)
            + std::abs(m_config.seaChoppy);
        const float halfWidth = width * 0.5f;
        for (const glm::vec3& point : m_config.splinePoints) {
            boundsMin = glm::min(boundsMin, point - glm::vec3(halfWidth, verticalPadding, halfWidth));
            boundsMax = glm::max(boundsMax, point + glm::vec3(halfWidth, verticalPadding, halfWidth));
        }
        m_boundsMin = boundsMin;
        m_boundsMax = boundsMax;
        m_mesh.emplace(verts, indices, VertexLayout{{3}, {3}, {2}, {3}});
        return;
    }
    const float half = m_config.size * 0.5f;
    const float step = m_config.size / static_cast<float>(res);

    std::vector<float> verts;
    verts.reserve(static_cast<std::size_t>(res + 1) * (res + 1) * 11);
    for (int z = 0; z <= res; ++z) {
        for (int x = 0; x <= res; ++x) {
            const float px = -half + static_cast<float>(x) * step;
            const float pz = -half + static_cast<float>(z) * step;
            verts.push_back(px);  verts.push_back(0.0f); verts.push_back(pz);   // position (local; centre added in shader)
            verts.push_back(0.0f); verts.push_back(1.0f); verts.push_back(0.0f); // normal (waves recompute it)
            verts.push_back(static_cast<float>(x) / res);
            verts.push_back(static_cast<float>(z) / res);
            verts.push_back(0.0f); verts.push_back(0.0f); verts.push_back(0.0f); // no spline tangent
        }
    }
    const float halfExtent = std::abs(half);
    const float verticalPadding = std::abs(m_config.seaHeight)
        + std::abs(m_config.seaChoppy);
    m_boundsMin = m_config.center + glm::vec3(-halfExtent, -verticalPadding, -halfExtent);
    m_boundsMax = m_config.center + glm::vec3(halfExtent, verticalPadding, halfExtent);

    std::vector<std::uint32_t> indices;
    indices.reserve(static_cast<std::size_t>(res) * res * 6);
    const int rowStride = res + 1;
    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            const std::uint32_t i0 = static_cast<std::uint32_t>(z * rowStride + x);
            const std::uint32_t i1 = i0 + 1;
            const std::uint32_t i2 = static_cast<std::uint32_t>((z + 1) * rowStride + x);
            const std::uint32_t i3 = i2 + 1;
            indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
            indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
        }
    }

    m_mesh.emplace(verts, indices, VertexLayout{{3}, {3}, {2}, {3}});
}

void Water::Draw(const Camera& camera, float aspect,
                 const glm::vec3& sunDir, const glm::vec3& sunColor, const glm::vec3& ambient,
                 const glm::vec4* contacts, int contactCount,
                 unsigned int sceneColorTexture,
                 unsigned int sceneDepthTexture,
                 int viewportWidth, int viewportHeight,
                 const IBL* ibl) {
    if (!m_mesh || !m_shader) return;
    if (m_config.maxRenderDistance > 0.0f) {
        const glm::vec2 delta = glm::max(
            glm::abs(glm::vec2(camera.Position().x - m_config.center.x,
                               camera.Position().z - m_config.center.z))
                - glm::vec2(m_config.size * 0.5f),
            glm::vec2(0.0f));
        if (glm::length(delta) > m_config.maxRenderDistance) return;
    }
    // Reject the whole surface before touching blend state or binding any of the
    // opaque scene textures. This is especially important for large water patches
    // and spline rivers that can sit outside the current camera view.
    const glm::mat4 viewProj = camera.ProjectionMatrix(aspect) * camera.ViewMatrix();
    const Frustum viewFrustum = ExtractFrustum(viewProj);
    if (!AABBInFrustum(viewFrustum, m_boundsMin, m_boundsMax)) return;

    // Transparent surface: blend over the opaque scene, keep depth testing but don't
    // write depth (so particles / other transparents still composite correctly).
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLboolean prevDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    // Use the custom water shader when one compiled; otherwise the built-in one. Both
    // share the identical uniform interface, so the uniform block below is unchanged.
    Shader& sh = m_customShader ? *m_customShader : *m_shader;
    sh.Bind();
    sh.SetMat4("uViewProj", viewProj);
    sh.SetVec3("uCenter", m_config.center);
    sh.SetFloat("uTime", m_time);
    sh.SetVec3("uCamPos", camera.Position());
    sh.SetVec3("uSunDir", sunDir);
    sh.SetVec3("uSunColor", sunColor);
    sh.SetVec3("uAmbient", ambient);
    sh.SetVec3("uShallow", m_config.shallowColor);
    sh.SetVec3("uDeep", m_config.deepColor);
    sh.SetVec3("uReflection", m_config.reflectionColor);
    sh.SetFloat("uFresnelPower", m_config.fresnelPower);
    sh.SetFloat("uSpecStrength", m_config.specularStrength);
    sh.SetFloat("uShininess", m_config.shininess);
    sh.SetFloat("uTransparency", m_config.transparency);
    sh.SetInt("uHasSceneColor", sceneColorTexture != 0 ? 1 : 0);
    sh.SetInt("uHasSceneDepth", sceneDepthTexture != 0 ? 1 : 0);
    sh.SetFloat("uRefractionStrength", m_config.refractionStrength);
    sh.SetFloat("uReflectionRoughness", m_config.reflectionRoughness);
    sh.SetFloat("uEnvironmentReflectionStrength", m_config.environmentReflectionStrength);
    // Screen-space scene reflection only makes sense when the opaque scene textures are bound.
    sh.SetInt("uSsrEnabled",
        (m_config.reflectScene && sceneColorTexture != 0 && sceneDepthTexture != 0) ? 1 : 0);
    sh.SetFloat("uSsrStrength", m_config.ssrStrength);
    sh.SetFloat("uSsrDistance", m_config.ssrDistance);
    sh.SetFloat("uSsrThickness", m_config.ssrThickness);
    sh.SetFloat("uAbsorptionStrength", m_config.absorptionStrength);
    sh.SetFloat("uCausticsStrength", m_config.causticsStrength);
    sh.SetFloat("uCausticsScale", m_config.causticsScale);
    sh.SetVec2("uViewportSize", glm::vec2(
        static_cast<float>(std::max(viewportWidth, 1)),
        static_cast<float>(std::max(viewportHeight, 1))));
    sh.SetFloat("uNearPlane", camera.nearPlane);
    sh.SetFloat("uFarPlane", camera.farPlane);
    sh.SetFloat("uDepthFadeDistance", m_config.depthFadeDistance);
    sh.SetFloat("uShoreFoamWidth", m_config.shorelineFoamWidth);
    sh.SetFloat("uShoreFoamStrength", m_config.shorelineFoamStrength);
    if (sceneColorTexture != 0) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, sceneColorTexture);
        sh.SetInt("uSceneColor", 1);
    }
    if (sceneDepthTexture != 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sceneDepthTexture);
        sh.SetInt("uSceneDepth", 0);
    }
    sh.SetInt("uHasEnvironment", ibl ? 1 : 0);
    sh.SetFloat("uMaxReflectionLod", ibl ? ibl->MaxReflectionLod() : 0.0f);
    if (ibl) {
        ibl->BindPrefilter(2);
        sh.SetInt("uEnvironment", 2);
    }
    sh.SetFloat("uSeaHeight", m_config.seaHeight);
    sh.SetFloat("uSeaChoppy", m_config.seaChoppy);
    sh.SetFloat("uSeaSpeed", m_config.seaSpeed);
    sh.SetFloat("uSeaFreq", m_config.seaFreq);
    sh.SetVec3("uFoamColor", m_config.foamColor);
    sh.SetFloat("uFoamAmount", m_config.foamAmount);
    sh.SetVec2("uFlowDir", m_config.flowDir);
    sh.SetFloat("uFlowStrength", m_config.flowStrength);
    const bool splineRibbon = m_config.splinePoints.size() >= 2;
    sh.SetInt("uSplineRibbon", splineRibbon ? 1 : 0);
    sh.SetFloat("uRiverWidth", std::max(m_config.riverWidth, 0.1f));
    sh.SetFloat("uRiverLength", splineRibbon
        ? std::max(m_splineLength, 0.01f)
        : std::max(m_config.size, 0.01f));

    const int n = (contacts && contactCount > 0)
        ? std::min(contactCount, kMaxContacts) : 0;
    sh.SetInt("uContactCount", n);
    for (int i = 0; i < n; ++i) {
        sh.SetVec4(kWaterContactUniforms[i], contacts[i]);
    }

    m_mesh->Draw();

    glDepthMask(prevDepthMask);
    if (!prevBlend) glDisable(GL_BLEND);
}

namespace {
// CPU mirror of the shader's value noise + sea octaves, so buoyancy matches the
// rendered surface. Uses the geometry octave count (3) like the vertex shader.
float CpuHash12(const glm::vec2& p) {
    const glm::vec2 fp(std::floor(p.x), std::floor(p.y));
    const std::uint32_t qx = static_cast<std::uint32_t>(static_cast<std::int32_t>(fp.x)) * 1597334677u;
    const std::uint32_t qy = static_cast<std::uint32_t>(static_cast<std::int32_t>(fp.y)) * 3812015801u;
    const std::uint32_t n = (qx ^ qy) * 1597334677u;
    return static_cast<float>(n) * (1.0f / 4294967295.0f);
}
float CpuNoise(const glm::vec2& p) {
    const glm::vec2 i(std::floor(p.x), std::floor(p.y));
    const glm::vec2 f = p - i;
    const glm::vec2 u = f * f * (glm::vec2(3.0f) - 2.0f * f);
    const float a = CpuHash12(i + glm::vec2(0.0f, 0.0f));
    const float b = CpuHash12(i + glm::vec2(1.0f, 0.0f));
    const float c = CpuHash12(i + glm::vec2(0.0f, 1.0f));
    const float d = CpuHash12(i + glm::vec2(1.0f, 1.0f));
    return -1.0f + 2.0f * glm::mix(glm::mix(a, b, u.x), glm::mix(c, d, u.x), u.y);
}
float CpuSeaOctave(glm::vec2 uv, float choppy) {
    uv += glm::vec2(CpuNoise(uv));
    glm::vec2 wv  = glm::vec2(1.0f) - glm::abs(glm::sin(uv));
    glm::vec2 swv = glm::abs(glm::cos(uv));
    wv = glm::mix(wv, swv, wv);
    return std::pow(1.0f - std::pow(wv.x * wv.y, 0.65f), choppy);
}
} // namespace

bool Water::ContainsXZ(float worldX, float worldZ, float padding) const {
    if (m_config.splinePoints.size() >= 2) {
        const glm::vec3 closest = m_flatSpline.ClosestPoint(glm::vec3(worldX, 0.0f, worldZ));
        const glm::vec2 delta(worldX - closest.x, worldZ - closest.z);
        return glm::dot(delta, delta)
            <= std::pow(m_config.riverWidth * 0.5f + std::max(padding, 0.0f), 2.0f);
    }
    const float half = m_config.size * 0.5f + std::max(padding, 0.0f);
    return std::abs(worldX - m_config.center.x) <= half
        && std::abs(worldZ - m_config.center.z) <= half;
}

float Water::HeightAt(float worldX, float worldZ) const {
    // Matches the vertex shader's sea_height() (3 geometry octaves) so floating objects
    // track the rendered crests.
    float freq   = m_config.seaFreq;
    float amp    = m_config.seaHeight;
    float choppy = m_config.seaChoppy;
    glm::vec2 flow = m_config.flowDir;
    glm::vec2 uv(worldX, worldZ);
    float baseY = m_config.center.y;
    if (m_config.splinePoints.size() >= 2) {
        float distance = 0.0f;
        glm::vec3 tangent(0.0f, 0.0f, 1.0f);
        const glm::vec3 closest = m_flatSpline.ClosestPoint(
            glm::vec3(worldX, 0.0f, worldZ), &distance, &tangent);
        baseY = m_spline.PositionAtDistance(distance).y;
        flow = glm::vec2(tangent.x, tangent.z);
        if (glm::dot(flow, flow) > 1.0e-8f) flow = glm::normalize(flow);
        const glm::vec2 side(flow.y, -flow.x);
        uv = glm::vec2(glm::dot(glm::vec2(worldX - closest.x, worldZ - closest.z), side),
                       distance);
        uv.y -= m_time * m_config.flowStrength;
    } else {
        uv -= flow * m_time * m_config.flowStrength;
    }
    uv.x *= 0.75f;
    const glm::mat2 octave(1.6f, 1.2f, -1.2f, 1.6f);
    float h = 0.0f;
    for (int i = 0; i < 3; ++i) {
        float d  = CpuSeaOctave((uv + m_time * m_config.seaSpeed) * freq, choppy);
        d       += CpuSeaOctave((uv - m_time * m_config.seaSpeed) * freq, choppy);
        h += d * amp;
        uv = uv * octave;
        freq *= 1.9f;
        amp  *= 0.22f;
        choppy = glm::mix(choppy, 1.0f, 0.2f);
    }
    return baseY + h;
}

} // namespace engine
