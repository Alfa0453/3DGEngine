#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;

out vec4 FragColor;

uniform vec3 uLightPos;
uniform vec3 uLightColor;
uniform vec3 uViewPos;
uniform vec3 uColor;

void main() {
    // Procedural checker from the sphere's UVs, so the ball's spin is visible
    // even without a texture. 8 cells around, 6 from pole to pole.
    float cu = floor(fract(vTexCoord.x) * 8.0);
    float cv = floor(vTexCoord.y * 6.0);
    float checker = mod(cu + cv, 2.0);
    vec3 base = mix(uColor, uColor * 0.30, checker);

    // Phong lighting (same model as the rest of the scene).
    float ambientStrength = 0.22;
    vec3 ambient = ambientStrength * uLightColor;

    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightPos - vWorldPos);
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * uLightColor;

    float specularStrength = 0.5;
    vec3 V = normalize(uViewPos - vWorldPos);
    vec3 Rd = reflect(-L, N);
    float spec = pow(max(dot(V, Rd), 0.0), 28.0);
    vec3 specular = specularStrength * spec * uLightColor;

    vec3 result = (ambient + diffuse + specular) * base;
    FragColor = vec4(result, 1.0);
}