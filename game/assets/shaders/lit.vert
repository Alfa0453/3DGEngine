#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

uniform mat4 uModel;        // local -> world
uniform mat4 uViewProj;     // world -> clip (same for every object this frame)
uniform mat3 uNormalMat;    // correct normals under non-uniform scale

out vec3 vWorldPos;         // fragment position in world space (for lighting)
out vec3 vNormal;           // world-space surface normal
out vec2 vTexCoord;

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vNormal = uNormalMat * aNormal;
    vTexCoord = aTexCoord;
    gl_Position = uViewProj * world;
}