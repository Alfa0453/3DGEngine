#version 330 core
layout (location = 0) in vec3 aPos;   // only position is used for the lamp marker
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
}