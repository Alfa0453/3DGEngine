// ============================================================================
//  Custom water shader — example / starter template
// ============================================================================
//
//  Assign this file to a water object via:  Inspector > Water > "Set Shader..."
//
//  You write ONLY the fragment body (helper functions + main()). The engine
//  automatically prepends, in this order:
//     1. #version 330 core
//     2. the shared sea-noise helpers  (sea_height(...), etc.)
//     3. the full water DECLARATION block — every `in` varying and `uniform`
//        the engine binds, plus `out vec4 FragColor;`
//  ...so everything below is already in scope. Just write to FragColor.
//
//  If this file fails to compile the water silently falls back to the built-in
//  look and the reason is printed to the editor log. Saving the file hot-reloads
//  it live in the viewport.
//
//  ---- Inputs available to you (declared by the engine) --------------------
//   Varyings (per-fragment):
//     vec3 vWorldPos;        // world-space position of this surface point
//     vec3 vBaseNormal;      // smooth surface normal (before wave detail)
//     vec2 vSurfaceCoord;    // planar surface coordinate
//     vec2 vFlowDir;         // local flow direction (rivers)
//   Camera / lighting:
//     vec3  uCamPos, uSunDir, uSunColor, uAmbient;
//   Look / tint:
//     vec3  uShallow, uDeep, uReflection, uFoamColor;
//     float uFresnelPower, uSpecStrength, uShininess, uTransparency, uFoamAmount;
//   Scene buffers (opaque scene captured before the water pass):
//     sampler2D uSceneColor;  int uHasSceneColor;
//     sampler2D uSceneDepth;  int uHasSceneDepth;
//     samplerCube uEnvironment; int uHasEnvironment;
//     vec2  uViewportSize;   float uNearPlane, uFarPlane;
//   Waves / time:
//     float uTime, uSeaHeight, uSeaChoppy, uSeaSpeed, uSeaFreq;
//   Helper already provided by the engine's noise block:
//     float sea_height(vec2 xz, float t, float h, float choppy, float speed, float freq, int octaves);
// ============================================================================

// A detailed surface normal from the wave height field (central differences).
vec3 customWaterNormal(vec2 xz, float eps) {
    float h  = sea_height(xz,                 uTime, uSeaHeight, uSeaChoppy, uSeaSpeed, uSeaFreq, 5);
    float hx = sea_height(xz + vec2(eps, 0.0), uTime, uSeaHeight, uSeaChoppy, uSeaSpeed, uSeaFreq, 5);
    float hz = sea_height(xz + vec2(0.0, eps), uTime, uSeaHeight, uSeaChoppy, uSeaSpeed, uSeaFreq, 5);
    return normalize(vec3(h - hx, eps, h - hz));
}

void main() {
    // Wave-perturbed normal.
    vec3 N = customWaterNormal(vWorldPos.xz, 0.15);
    vec3 V = normalize(uCamPos - vWorldPos);

    // View-angle Fresnel: more sky reflection at grazing angles.
    float fresnel = pow(clamp(1.0 - max(dot(N, V), 0.0), 0.0, 1.0), uFresnelPower);

    // Base body colour: blend shallow -> deep by view angle as a simple stand-in
    // for depth (swap in a uSceneDepth read for true depth-based absorption).
    vec3 body = mix(uDeep, uShallow, clamp(dot(N, V), 0.0, 1.0));

    // Sky tint mixed in by Fresnel.
    vec3 color = mix(body, uReflection, fresnel);

    // Sun glint (Blinn-Phong specular).
    vec3 L = normalize(-uSunDir);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), uShininess) * uSpecStrength;
    color += uSunColor * spec;

    // Crest foam where the wave height peaks.
    float crest = smoothstep(uSeaHeight * 0.55, uSeaHeight * 0.95,
                             sea_height(vWorldPos.xz, uTime, uSeaHeight, uSeaChoppy, uSeaSpeed, uSeaFreq, 5));
    color = mix(color, uFoamColor, crest * uFoamAmount);

    // Ambient term + transparency (more opaque at grazing angles).
    color += uAmbient * body * 0.25;
    float alpha = clamp(uTransparency + fresnel * (1.0 - uTransparency), 0.0, 1.0);

    FragColor = vec4(color, alpha);
}
