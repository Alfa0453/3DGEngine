#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
out vec4 FragColor;

uniform vec3 uLightPos;
uniform vec3 uLightColor;
uniform vec3 uViewPos;
uniform vec3 uColor;

void main() {
    float ambientStrength = 0.25;
    vec3 ambient = ambientStrength * uLightColor;

    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightPos - vWorldPos);
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * uLightColor;

    float specularStrength = 0.4;
    vec3 V = normalize(uViewPos - vWorldPos);
    vec3 Rd = reflect(-L, N);
    float spec = pow(max(dot(V, Rd), 0.0), 24.0);
    vec3 specular = specularStrength * spec * uLightColor;

    vec3 result = (ambient + diffuse + specular) * uColor;
    FragColor = vec4(result, 1.0);
}
