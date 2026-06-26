#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 uViewProj;        // projection * view-with-translation-removed
out vec3 vDir;
void main() {
    vDir = aPos;
    vec4 p = uViewProj * vec4(aPos, 1.0);
    gl_Position = p.xyww;       // force depth = 1 (behind everything)
}
