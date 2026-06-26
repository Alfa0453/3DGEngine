#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

uniform mat4 uModel;
uniform mat4 uViewProj;
uniform mat3 uNormalMat;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;

void main() {
    vec4 world  = uModel * vec4(aPos, 1.0);
    vWorldPos   = world.xyz;
    vNormal     = uNormalMat * aNormal;
    vUV         = aTexCoord;
    gl_Position = uViewProj * world;
}
