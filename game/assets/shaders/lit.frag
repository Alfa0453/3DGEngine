#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;

out vec4 FragColor;

uniform vec3 uLightPos;     // light position in world space
uniform vec3 uLightColor;
uniform vec3 uViewPos;      // camera position in world space
uniform sampler2D uTexture;

void main() {
    vec3 texColor = texture(uTexture, vTexCoord).rgb;

    // Ambient: a small floor so faces turned away from the light are not black.
    float ambientStrength = 0.15;
    vec3 ambient = ambientStrength * uLightColor;

    // Diffuse: how directly the surface faces the light (Lambert's cosine law).
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightPos - vWorldPos);
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * uLightColor;

    // Specular: a tight highlight along the reflected-light direction (Phong).
    float specularStrength = 0.5;
    vec3 V = normalize(uViewPos - vWorldPos);
    vec3 R = reflect(-L, N);
    float spec = pow(max(dot(V, R), 0.0), 32.0);   // 32 = shininess
    vec3 specular = specularStrength * spec * uLightColor;

    vec3 result = (ambient + diffuse + specular) * texColor;
    FragColor = vec4(result, 1.0);
}