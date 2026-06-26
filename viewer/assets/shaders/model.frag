#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
out vec4 FragColor;

uniform vec3  uLightPos;
uniform vec3  uLightColor;
uniform vec3  uViewPos;

// Per-material (set by engine::DrawModel):
uniform vec3      uColor;        // Kd
uniform vec3      uSpecular;     // Ks
uniform float     uShininess;    // Ns
uniform int       uHasTexture;   // 1 if uTexture is bound
uniform sampler2D uTexture;      // diffuse map (unit 0)

void main() {
    vec3 base = uColor;
    if (uHasTexture == 1) base *= texture(uTexture, vUV).rgb;

    vec3 ambient = 0.20 * uLightColor;

    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightPos - vWorldPos);
    vec3 diffuse = max(dot(N, L), 0.0) * uLightColor;

    vec3 V = normalize(uViewPos - vWorldPos);
    vec3 R = reflect(-L, N);
    float spec = pow(max(dot(V, R), 0.0), max(uShininess, 1.0));
    vec3 specular = spec * uSpecular * uLightColor;

    vec3 result = (ambient + diffuse) * base + specular;
    FragColor = vec4(result, 1.0);
}
