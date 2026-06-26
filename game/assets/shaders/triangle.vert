#version 330 core

// Vertex attributes, fed from the VAO. The "location" numbers match the
// glVertexAttribPointer calls in the game code.
layout(location = 0) in vec3 aPos;       // position in clip-ish space (-1..1)
layout(location = 1) in vec3 aColor;     // per-vertex colour

// Passed on to the fragment shader. The GPU interpolates it across the
// triangle's surface, so each pixel gets a blended colour.
out vec3 vColor;

void main()
{
    vColor = aColor;
    // gl_Position is the required output: the vertex's clip-space position.
    gl_Position = vec4(aPos, 1.0);
}