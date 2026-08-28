#pragma once

#include <string>
#include <algorithm>
#include <cmath>

namespace engine {

inline float SmoothFiniteLightAttenuation(float distanceSquared, float radius) {
    const float safeRadius = std::max(radius, 0.001f);
    const float normalized = std::sqrt(std::max(distanceSquared, 0.0f)) / safeRadius;
    const float base = std::clamp(1.0f - std::pow(normalized, 4.0f), 0.0f, 1.0f);
    return (base * base) / std::max(distanceSquared, 0.0025f);
}

// Shared fragment-lighting helpers injected into both static and skinned PBR
// shaders. Keeping attenuation, probe decoding, and specular occlusion here
// prevents the two material paths from drifting again.
inline const char* PbrLightingCommonGlsl() {
    return R"GLSL(
uniform sampler3D uLightingSH0;
uniform sampler3D uLightingSH1;
uniform sampler3D uLightingSH2;
uniform sampler3D uLightingMeta;
uniform float uLocalProbeInfluence;
uniform float uSpecularOcclusionStrength;
uniform int uLightingDebugMode;
uniform samplerCube uLocalReflection0;
uniform samplerCube uLocalReflection1;
uniform int uReflectionProbeCount;
uniform vec3 uReflectionProbePosition[2];
uniform vec3 uReflectionProbeExtents[2];
uniform float uReflectionProbeRadius[2];
uniform float uReflectionProbeBlend[2];
uniform float uReflectionProbeIntensity[2];
uniform float uReflectionProbePriorityFactor[2];
uniform int uReflectionProbeShape[2];
uniform float uReflectionProbeMaxLod[2];
uniform sampler2D uLtcMatrixLut;
uniform sampler2D uLtcAmplitudeLut;

float SmoothFiniteAttenuation(float distanceSquared, float radius) {
    float safeRadius = max(radius, 0.001);
    float normalizedDistance = sqrt(max(distanceSquared, 0.0)) / safeRadius;
    float window = pow(clamp(1.0 - pow(normalizedDistance, 4.0), 0.0, 1.0), 2.0);
    return window / max(distanceSquared, 0.0025);
}

struct LocalProbeSample { vec3 irradiance; float skyVisibility; float validity; };
LocalProbeSample SampleLocalProbe(vec3 worldPosition, vec3 normal) {
    LocalProbeSample result;
    result.irradiance = vec3(0.0); result.skyVisibility = 1.0; result.validity = 0.0;
    if (uUseLightingGrid != 1) return result;
    vec3 extent = max(uLightingGridMax - uLightingGridMin, vec3(0.0001));
    vec3 uvw = (worldPosition - uLightingGridMin) / extent;
    if (any(lessThan(uvw, vec3(0.0))) || any(greaterThan(uvw, vec3(1.0)))) return result;
    vec4 a = texture(uLightingSH0, uvw);
    vec4 b = texture(uLightingSH1, uvw);
    vec4 c = texture(uLightingSH2, uvw);
    vec4 meta = texture(uLightingMeta, uvw);
    float validity = meta.g;
    if (validity <= 0.001) return result;
    float invValidity = 1.0 / validity;
    vec3 sh0 = a.rgb * invValidity;
    vec3 sh1 = vec3(a.a, b.r, b.g) * invValidity;
    vec3 sh2 = vec3(b.b, b.a, c.r) * invValidity;
    vec3 sh3 = c.gba * invValidity;
    vec4 basis = vec4(0.2820947918,
                      0.4886025119 * normal.y,
                      0.4886025119 * normal.z,
                      0.4886025119 * normal.x);
    result.irradiance = max(sh0 * basis.x + sh1 * basis.y + sh2 * basis.z + sh3 * basis.w,
                            vec3(0.0));
    result.skyVisibility = clamp(meta.r * invValidity, 0.0, 1.0);
    result.validity = clamp(validity, 0.0, 1.0);
    return result;
}

float PbrSpecularOcclusion(float ao, float nDotV, float roughness, float skyVisibility) {
    float exponent = exp2(-16.0 * roughness - 1.0);
    float gtao = clamp(pow(clamp(nDotV + ao, 0.0, 1.0), exponent) - 1.0 + ao, 0.0, 1.0);
    float local = mix(1.0, max(skyVisibility, 0.08), 0.70);
    return mix(1.0, gtao * local, clamp(uSpecularOcclusionStrength, 0.0, 1.0));
}

float ReflectionProbeWeight(int index, vec3 p) {
    if (uReflectionProbeShape[index] == 1) {
        float distanceToEdge = uReflectionProbeRadius[index]
            - length(p-uReflectionProbePosition[index]);
        return smoothstep(0.0, max(uReflectionProbeBlend[index],0.001), distanceToEdge);
    }
    vec3 local = abs(p-uReflectionProbePosition[index]);
    vec3 inside = uReflectionProbeExtents[index]-local;
    if (min(inside.x,min(inside.y,inside.z)) < 0.0) return 0.0;
    return smoothstep(0.0,max(uReflectionProbeBlend[index],0.001),
                      min(inside.x,min(inside.y,inside.z)));
}
vec3 BoxProjectedDirection(int index, vec3 p, vec3 direction) {
    vec3 safeDirection=vec3(abs(direction.x)<1e-5?(direction.x<0.0?-1e-5:1e-5):direction.x,
                            abs(direction.y)<1e-5?(direction.y<0.0?-1e-5:1e-5):direction.y,
                            abs(direction.z)<1e-5?(direction.z<0.0?-1e-5:1e-5):direction.z);
    vec3 boxMin=uReflectionProbePosition[index]-uReflectionProbeExtents[index];
    vec3 boxMax=uReflectionProbePosition[index]+uReflectionProbeExtents[index];
    vec3 t0=(boxMin-p)/safeDirection, t1=(boxMax-p)/safeDirection;
    vec3 farT=max(t0,t1); float distance=min(farT.x,min(farT.y,farT.z));
    return (p+direction*max(distance,0.0))-uReflectionProbePosition[index];
}
vec3 SampleLocalReflectionProbe(int index, vec3 p, vec3 direction, float roughness) {
    vec3 lookup=uReflectionProbeShape[index]==0?BoxProjectedDirection(index,p,direction):direction;
    if(index==0)return textureLod(uLocalReflection0,lookup,roughness*uReflectionProbeMaxLod[0]).rgb;
    return textureLod(uLocalReflection1,lookup,roughness*uReflectionProbeMaxLod[1]).rgb;
}
vec3 SampleReflectionEnvironment(vec3 p, vec3 direction, float roughness,
                                 vec3 globalReflection, out float localWeight) {
    float w0=uReflectionProbeCount>0?ReflectionProbeWeight(0,p)*uReflectionProbePriorityFactor[0]:0.0;
    float w1=uReflectionProbeCount>1?ReflectionProbeWeight(1,p)*uReflectionProbePriorityFactor[1]:0.0;
    float sum=max(w0+w1,1.0); w0/=sum; w1/=sum;
    localWeight=clamp(w0+w1,0.0,1.0);
    vec3 local=vec3(0.0);
    if(w0>0.0)local+=SampleLocalReflectionProbe(0,p,direction,roughness)*w0*uReflectionProbeIntensity[0];
    if(w1>0.0)local+=SampleLocalReflectionProbe(1,p,direction,roughness)*w1*uReflectionProbeIntensity[1];
    return local+globalReflection*(1.0-localWeight);
}

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float nDotH = max(dot(N, H), 0.0);
    float denominator = nDotH * nDotH * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(PI * denominator * denominator, 0.0001);
}
float GeometrySchlickGGX(float nDotV, float roughness) {
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return nDotV / max(nDotV * (1.0 - k) + k, 0.0001);
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness)
         * GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}
vec3 FresnelSchlick(float cosine, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}
vec3 FresnelSchlickRough(float cosine, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0)
        * pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}
vec3 Lighting(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo,
              vec3 F0, float metallic, float roughness) {
    vec3 H = normalize(V + L);
    float distribution = DistributionGGX(N, H, roughness);
    float geometry = GeometrySmith(N, V, L, roughness);
    vec3 fresnel = FresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3 specular = distribution * geometry * fresnel
        / (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001);
    vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic);
    return (diffuseWeight * albedo / PI + specular) * radiance * max(dot(N, L), 0.0);
}
vec3 LtcIntegrateEdge(vec3 a,vec3 b){
    float cosine=clamp(dot(a,b),-0.9999,0.9999);float theta=acos(cosine);
    return cross(a,b)*(theta/max(sin(theta),0.0001));
}
float LtcIntegrateQuad(vec3 points[4],mat3 transform){
    vec3 p0=normalize(transform*points[0]),p1=normalize(transform*points[1]);
    vec3 p2=normalize(transform*points[2]),p3=normalize(transform*points[3]);
    vec3 sum=LtcIntegrateEdge(p0,p1)+LtcIntegrateEdge(p1,p2)+
             LtcIntegrateEdge(p2,p3)+LtcIntegrateEdge(p3,p0);
    return max(sum.z,0.0)/(2.0*PI);
}
vec3 LtcRectangleLight(vec3 N,vec3 V,vec3 worldPosition,vec3 center,vec3 right,vec3 up,
                       vec2 halfSize,int twoSided,vec3 radiance,vec3 albedo,vec3 F0,
                       float metallic,float roughness){
    vec3 planeNormal=normalize(cross(right,up));float facing=dot(planeNormal,worldPosition-center);
    if(twoSided==0&&facing<=0.0)return vec3(0.0);
    float nDotV=clamp(dot(N,V),0.0,1.0);vec2 lutUv=vec2(clamp(roughness,0.0,1.0),nDotV);
    vec4 ltc=texture(uLtcMatrixLut,lutUv);vec2 amplitude=texture(uLtcAmplitudeLut,lutUv).rg;
    vec3 tangent=V-N*dot(V,N);if(dot(tangent,tangent)<1e-6)tangent=abs(N.y)<0.999?cross(vec3(0,1,0),N):cross(vec3(1,0,0),N);
    tangent=normalize(tangent);vec3 bitangent=normalize(cross(N,tangent));
    mat3 worldToShading=transpose(mat3(tangent,bitangent,N));
    vec3 corners[4]=vec3[4](center-right*halfSize.x-up*halfSize.y-worldPosition,
        center+right*halfSize.x-up*halfSize.y-worldPosition,
        center+right*halfSize.x+up*halfSize.y-worldPosition,
        center-right*halfSize.x+up*halfSize.y-worldPosition);
    for(int i=0;i<4;++i)corners[i]=worldToShading*corners[i];
    float diffuseIntegral=LtcIntegrateQuad(corners,mat3(1.0));
    mat3 inverseLtc=mat3(vec3(ltc.x,0.0,ltc.y),vec3(0.0,1.0,0.0),vec3(ltc.z,0.0,max(ltc.w,0.001)));
    float specularIntegral=LtcIntegrateQuad(corners,inverseLtc);
    vec3 fresnelAmplitude=F0*amplitude.x+(vec3(1.0)-F0)*amplitude.y;
    vec3 diffuse=(1.0-metallic)*albedo/PI*diffuseIntegral;
    return radiance*(diffuse+fresnelAmplitude*specularIntegral);
}
)GLSL";
}

inline std::string ComposePbrLightingShader(const char* source) {
    std::string result(source ? source : "");
    constexpr const char* marker = "//__PBR_LIGHTING_COMMON__";
    const std::size_t position = result.find(marker);
    if (position != std::string::npos)
        result.replace(position, std::char_traits<char>::length(marker), PbrLightingCommonGlsl());
    return result;
}

} // namespace engine
