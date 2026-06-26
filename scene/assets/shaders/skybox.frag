#version 330 core
in vec3 vDir;
out vec4 FragColor;
uniform samplerCube uSky;
void main() {
    vec3 c = texture(uSky, normalize(vDir)).rgb;
    c = pow(c, vec3(1.0 / 2.2));   // match the scene's gamma
    FragColor = vec4(c, 1.0);
}
