#version 330 core
const float PI = 3.14159265359;
const int   MAX_POINTS = 16;

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec4 vLightSpacePos;
out vec4 FragColor;

uniform vec3 uViewPos;

// Material (metallic / roughness workflow).
uniform vec3  uAlbedo;
uniform float uMetallic;
uniform float uRoughness;
uniform float uAO;
uniform vec3  uEmissive;

// Directional "sun" (the shadow caster) + ambient.
uniform vec3 uSunDir;       // direction the light travels
uniform vec3 uSunColor;     // colour * intensity (radiance)
uniform vec3 uAmbient;

// Point lights.
uniform int  uNumPoints;
uniform vec3 uPointPos[MAX_POINTS];
uniform vec3 uPointColor[MAX_POINTS];   // colour * intensity

uniform sampler2D uShadowMap;

float DistributionGGX(vec3 N, vec3 H, float rough) {
    float a = rough * rough;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float d = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}
float GeometrySchlickGGX(float NdotV, float rough) {
    float r = rough + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float rough) {
    return GeometrySchlickGGX(max(dot(N, V), 0.0), rough)
         * GeometrySchlickGGX(max(dot(N, L), 0.0), rough);
}
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float ShadowFactor(vec4 lightSpacePos, float NdotL) {
    vec3 p = lightSpacePos.xyz / lightSpacePos.w * 0.5 + 0.5;
    if (p.z > 1.0) return 0.0;
    float bias = max(0.0025 * (1.0 - NdotL), 0.0008);
    float shadow = 0.0;
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            float closest = texture(uShadowMap, p.xy + vec2(x, y) * texel).r;
            shadow += (p.z - bias > closest) ? 1.0 : 0.0;
        }
    return shadow / 9.0;
}

vec3 Radiance(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, vec3 F0,
              float metallic, float rough) {
    vec3 H = normalize(V + L);
    float NDF = DistributionGGX(N, H, rough);
    float G   = GeometrySmith(N, V, L, rough);
    vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3 spec = (NDF * G * F) / (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    float NdotL = max(dot(N, L), 0.0);
    return (kD * albedo / PI + spec) * radiance * NdotL;
}

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uViewPos - vWorldPos);
    vec3 F0 = mix(vec3(0.04), uAlbedo, uMetallic);

    vec3 Lo = vec3(0.0);

    // Directional sun (shadowed).
    vec3 Ls = normalize(-uSunDir);
    float sunNdotL = max(dot(N, Ls), 0.0);
    float shadow = ShadowFactor(vLightSpacePos, sunNdotL);
    Lo += (1.0 - shadow) * Radiance(N, V, Ls, uSunColor, uAlbedo, F0, uMetallic, uRoughness);

    // Point lights.
    for (int i = 0; i < uNumPoints && i < MAX_POINTS; ++i) {
        vec3 d = uPointPos[i] - vWorldPos;
        float dist2 = dot(d, d);
        vec3 L = d / sqrt(dist2);
        vec3 radiance = uPointColor[i] / max(dist2, 0.0001);   // inverse-square falloff
        Lo += Radiance(N, V, L, radiance, uAlbedo, F0, uMetallic, uRoughness);
    }

    vec3 ambient = uAmbient * uAlbedo * uAO;
    vec3 color = ambient + Lo + uEmissive;

    color = color / (color + vec3(1.0));            // Reinhard tonemap
    color = pow(color, vec3(1.0 / 2.2));            // gamma correct
    FragColor = vec4(color, 1.0);
}
