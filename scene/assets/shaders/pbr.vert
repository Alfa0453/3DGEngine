#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

uniform mat4 uModel;
uniform mat4 uViewProj;
uniform mat3 uNormalMat;
uniform mat4 uLightSpace;     // directional light's view-projection

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec4 vLightSpacePos;

void main() {
    vec4 world     = uModel * vec4(aPos, 1.0);
    vWorldPos      = world.xyz;
    vNormal        = normalize(uNormalMat * aNormal);
    vUV            = aTexCoord;
    vLightSpacePos = uLightSpace * world;
    gl_Position    = uViewProj * world;
}
