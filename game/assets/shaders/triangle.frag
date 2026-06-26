#version 330 core

// Interpolated colour coming from the vertex shader (one value per pixel).
in vec3 vColor;

// The shader's output: the final colour of this pixel (RGBA).
out vec4 FragColor;

void main()
{
    FragColor = vec4(vColor, 1.0);
}