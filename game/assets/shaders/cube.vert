#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;

// The combined Model-View-Projection matrix, set once per frame from C++.
// Multiplying a vertex by it transforms the cube from its own local space all
// the way into clip space in a single step.
uniform mat4 uMVP;

out vec3 vColor;

void main()
{
    vColor = aColor;
    gl_Position = uMVP * vec4(aPos, 1.0);
}