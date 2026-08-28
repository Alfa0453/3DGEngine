#include "engine/graphics/PostProcess.h"

#include "engine/graphics/VertexLayout.h"
#include "engine/graphics/Texture.h"
#include "engine/graphics/ShaderParameterBinding.h"
#include "engine/graphics/CascadedShadow.h"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <cmath>
#include <utility>
#include <vector>
#include <algorithm>

namespace engine {
namespace {

const char* kFullscreenVert = R"GLSL(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
out vec2 vUV;
void main () { vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }
)GLSL";

// Bright-pass: keep only pixels brighter than the threshold (the bloom source).
const char* kBrightFrag = R"GLSL(
#version 330 core
in vec2 vUV; out vec4 FragColor;
uniform sampler2D uScene;
uniform float uThreshold;
uniform float uKnee;
void main() {
    vec2 texel = 1.0 / vec2(textureSize(uScene, 0));
    // A compact prefilter prevents sub-pixel emissive details from entering and
    // leaving the bloom pyramid as single-frame flashes.
    vec3 c = texture(uScene, vUV).rgb * 0.50;
    c += texture(uScene, vUV + vec2(texel.x, 0.0)).rgb * 0.125;
    c += texture(uScene, vUV - vec2(texel.x, 0.0)).rgb * 0.125;
    c += texture(uScene, vUV + vec2(0.0, texel.y)).rgb * 0.125;
    c += texture(uScene, vUV - vec2(0.0, texel.y)).rgb * 0.125;
    float b = max(max(c.r, c.g), c.b);
    float knee = max(uKnee, 1e-4);
    float soft = clamp((b - uThreshold + knee) / (2.0 * knee), 0.0, 1.0);
    soft = soft * soft * knee;
    float contribution = max(b - uThreshold, soft) / max(b, 1e-4);
    FragColor = vec4(c * contribution, 1.0);
}
)GLSL";

// Separable Gaussian blur (one axis per invocation).
const char* kBlurFrag = R"GLSL(
#version 330 core
in vec2 vUV; out vec4 FragColor;
uniform sampler2D uImage;
uniform float uHorizontal;
const float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
void main() {
    vec2 texel = 1.0 / vec2(textureSize(uImage, 0));
    vec2 dir = uHorizontal > 0.5 ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec3 r = texture(uImage, vUV).rgb * w[0];
    for (int i = 1; i < 5; ++i) {
        vec2 off = dir * texel * float(i);
        r += texture(uImage, vUV + off).rgb * w[i];
        r += texture(uImage, vUV - off).rgb * w[i];
    }
    FragColor = vec4(r, 1.0);
}
)GLSL";

// Composite: scene + bloom, exposure, ACES tone map, gamma.
const char* kCompositeFrag = R"GLSL(
#version 330 core
in vec2 vUV; out vec4 FragColor;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform sampler2D uSceneDepth;
uniform sampler2D uIndirect;
uniform float uIndirectStrength;
uniform int uIndirectDebug;
uniform float uExposure;
uniform float uBloomStrength;
uniform float uTime;
uniform float uUnderwaterBlend;
uniform vec3  uUnderwaterTint;
uniform float uUnderwaterFogDensity;
uniform float uUnderwaterDistortion;
uniform float uUnderwaterCausticsStrength;
uniform float uUnderwaterCausticsScale;
uniform float uSaturation;
uniform float uContrast;
uniform float uTemperature;
uniform float uTint;
uniform vec3 uLift;
uniform vec3 uGamma;
uniform vec3 uGain;
uniform sampler2D uColorLut;
uniform int uHasColorLut;
uniform float uLutIntensity;
uniform float uLutSize;
uniform sampler2D uVolumetric;
uniform int uVolumetricEnabled;
vec3 ACES(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
vec3 sampleFlattenedLut(vec3 color) {
    float size = max(uLutSize, 2.0);
    vec3 scaled = clamp(color, 0.0, 1.0) * (size - 1.0);
    float blue0 = floor(scaled.b);
    float blue1 = min(blue0 + 1.0, size - 1.0);
    vec2 texSize = vec2(size * size, size);
    vec2 uv0 = (vec2(blue0 * size + scaled.r, scaled.g) + 0.5) / texSize;
    vec2 uv1 = (vec2(blue1 * size + scaled.r, scaled.g) + 0.5) / texSize;
    return mix(texture(uColorLut, uv0).rgb, texture(uColorLut, uv1).rgb,
               fract(scaled.b));
}
vec4 sampleDepthAwareVolume(vec2 uv) {
    vec2 texel = 1.0 / vec2(textureSize(uVolumetric, 0));
    float centerDepth = texture(uSceneDepth, uv).r;
    vec4 accumulated = texture(uVolumetric, uv);
    float totalWeight = 1.0;
    const vec2 offsets[4] = vec2[](vec2(1.0, 0.0), vec2(-1.0, 0.0),
                                    vec2(0.0, 1.0), vec2(0.0, -1.0));
    for (int i = 0; i < 4; ++i) {
        vec2 samplePosition = clamp(uv + offsets[i] * texel, vec2(0.001), vec2(0.999));
        float sampleDepth = texture(uSceneDepth, samplePosition).r;
        // Keep low-resolution fog from leaking across foreground silhouettes.
        float weight = exp(-abs(sampleDepth - centerDepth) * 600.0);
        accumulated += texture(uVolumetric, samplePosition) * weight;
        totalWeight += weight;
    }
    return accumulated / max(totalWeight, 1e-4);
}
void main() {
    float underwater = clamp(uUnderwaterBlend, 0.0, 1.0);
    vec2 wave = vec2(
        sin(vUV.y * 31.0 + uTime * 1.7) + sin(vUV.y * 13.0 - uTime * 0.9),
        cos(vUV.x * 27.0 - uTime * 1.4) + cos(vUV.x * 11.0 + uTime * 0.8));
    vec2 sampleUv = clamp(vUV + wave * (0.5 * uUnderwaterDistortion * underwater),
                          vec2(0.001), vec2(0.999));
    vec3 hdr = texture(uScene, sampleUv).rgb;
    vec4 volume = vec4(0.0, 0.0, 0.0, 1.0);
    if (uVolumetricEnabled == 1) {
        volume = sampleDepthAwareVolume(sampleUv);
        hdr = hdr * clamp(volume.a, 0.0, 1.0) + max(volume.rgb, vec3(0.0));
    }
    vec3 indirect = texture(uIndirect, sampleUv).rgb * uIndirectStrength;
    hdr = uIndirectDebug == 1 ? indirect : hdr + indirect;
    if (uBloomStrength > 0.0001) {
        vec3 bloom = textureLod(uBloom, sampleUv, 0.0).rgb;
        bloom += textureLod(uBloom, sampleUv, 1.0).rgb * 0.75;
        bloom += textureLod(uBloom, sampleUv, 2.0).rgb * 0.50;
        bloom += textureLod(uBloom, sampleUv, 3.0).rgb * 0.30;
        hdr += bloom * uBloomStrength;
    }
    if (underwater > 0.0001) {
        float rawDepth = texture(uSceneDepth, sampleUv).r;
        float distanceHint = smoothstep(0.15, 1.0, rawDepth);
        float fog = underwater * (1.0 - exp(-uUnderwaterFogDensity
                    * mix(1.0, 12.0, distanceHint)));
        vec3 filtered = hdr * mix(vec3(1.0), uUnderwaterTint * 2.0, fog * 0.72);
        filtered = mix(filtered, uUnderwaterTint * max(dot(hdr, vec3(0.333)), 0.16),
                       fog * 0.38);
        float causticA = sin((sampleUv.x + sampleUv.y) * uUnderwaterCausticsScale * 18.0
                             + uTime * 1.8);
        float causticB = sin((sampleUv.x - sampleUv.y) * uUnderwaterCausticsScale * 15.0
                             - uTime * 1.3);
        float caustics = pow(max(0.0, 1.0 - abs(causticA + causticB) * 0.55), 7.0);
        filtered += uUnderwaterTint * caustics * uUnderwaterCausticsStrength
                    * underwater * (1.0 - fog * 0.6);
        float vignette = 1.0 - smoothstep(0.20, 0.85, length(vUV - vec2(0.5)));
        hdr = filtered * mix(1.0, mix(0.82, 1.0, vignette), underwater);
    }
    // Presentation-only white balance and lift/gamma/gain. Lighting buffers stay linear.
    float temp = clamp((uTemperature - 6500.0) / 6500.0, -1.0, 1.0);
    vec3 whiteBalance = vec3(1.0 + temp * 0.10 + uTint * 0.04,
                             1.0 - abs(temp) * 0.025,
                             1.0 - temp * 0.10 - uTint * 0.04);
    hdr *= whiteBalance;
    float luma = dot(hdr, vec3(0.2126, 0.7152, 0.0722));
    hdr = mix(vec3(luma), hdr, max(uSaturation, 0.0));
    hdr = (hdr - vec3(0.18)) * max(uContrast, 0.0) + vec3(0.18);
    hdr = pow(max(hdr + uLift, vec3(0.0)), vec3(1.0) / max(uGamma, vec3(0.01))) * uGain;
    vec3 col = ACES(hdr * uExposure);
    if (uHasColorLut == 1)
        col = mix(col, sampleFlattenedLut(col), clamp(uLutIntensity, 0.0, 1.0));
    col = pow(col, vec3(1.0 / 2.2));
    FragColor = vec4(col, 1.0);
}
)GLSL";

const char* kVolumetricFrag = R"GLSL(
#version 330 core
in vec2 vUV; out vec4 FragColor;
uniform sampler2D uDepth;
uniform sampler2D uHistory;
uniform mat4 uInvViewProjection;
uniform mat4 uPreviousViewProjection;
uniform vec3 uCameraPosition;
uniform vec3 uSunDirection;
uniform vec3 uSunRadiance;
uniform vec3 uSkyRadiance;
uniform vec3 uAlbedo;
uniform float uDensity;
uniform float uScattering;
uniform float uExtinction;
uniform float uAnisotropy;
uniform float uBaseHeight;
uniform float uHeightFalloff;
uniform float uStartDistance;
uniform float uMaxDistance;
uniform float uHistoryWeight;
uniform int uSlices;
uniform int uHistoryValid;
uniform sampler2DArray uShadowMap;
uniform mat4 uShadowVP[4];
uniform mat4 uView;
uniform vec4 uCascadeSplits;
uniform int uShadowEnabled;
uniform int uLightCount;
uniform vec4 uLightPositionRange[16];
uniform vec4 uLightDirectionOuter[16];
uniform vec3 uLightRadiance[16];
uniform int uLocalFogCount;
uniform vec4 uLocalFogPositionShape[8];
uniform vec4 uLocalFogExtentsRadius[8];
uniform vec4 uLocalFogAlbedoDensity[8];
uniform vec4 uLocalFogParams[8];

float hg(float mu, float g) {
    float gg=g*g;
    return (1.0-gg)/max(12.56637*pow(1.0+gg-2.0*g*mu,1.5),1e-4);
}
float sunVisibility(vec3 worldPosition) {
    if(uShadowEnabled==0) return 1.0;
    float viewDepth=-(uView*vec4(worldPosition,1.0)).z;
    int cascade=viewDepth<uCascadeSplits.x?0:(viewDepth<uCascadeSplits.y?1:(viewDepth<uCascadeSplits.z?2:3));
    vec4 lp=uShadowVP[cascade]*vec4(worldPosition,1.0);
    vec3 uvw=lp.xyz/max(abs(lp.w),1e-5); uvw=uvw*0.5+0.5;
    if(any(lessThan(uvw.xy,vec2(0.0)))||any(greaterThan(uvw.xy,vec2(1.0)))) return 1.0;
    float bias=0.0015; float visibility=0.0;
    vec2 texel=1.0/vec2(textureSize(uShadowMap,0).xy);
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)
        visibility+=uvw.z-bias<=texture(uShadowMap,vec3(uvw.xy+vec2(x,y)*texel,float(cascade))).r?1.0:0.0;
    return visibility/9.0;
}
void main() {
    float depth=texture(uDepth,vUV).r;
    vec4 farPoint=uInvViewProjection*vec4(vUV*2.0-1.0,1.0,1.0);
    farPoint.xyz/=max(abs(farPoint.w),1e-5);
    vec3 ray=normalize(farPoint.xyz-uCameraPosition);
    float sceneDistance=mix(uStartDistance,uMaxDistance,clamp(depth,0.0,1.0));
    float endDistance=min(sceneDistance,uMaxDistance);
    vec3 scatteringSum=vec3(0.0); float transmittance=1.0;
    int slices=clamp(uSlices,8,96);
    for(int i=0;i<96;++i) {
        if(i>=slices) break;
        float a=float(i)/float(slices), b=float(i+1)/float(slices);
        float da=mix(uStartDistance,endDistance,a*a);
        float db=mix(uStartDistance,endDistance,b*b);
        float ds=max(db-da,0.0);
        vec3 p=uCameraPosition+ray*(0.5*(da+db));
        float heightDensity=exp(clamp(-uHeightFalloff*(p.y-uBaseHeight),-12.0,12.0));
        float density=max(uDensity*heightDensity,0.0);
        vec3 mediumAlbedo=uAlbedo;
        float mediumExtinction=uExtinction;
        float mediumAnisotropy=uAnisotropy;
        for(int fogIndex=0;fogIndex<8;++fogIndex) {
            if(fogIndex>=uLocalFogCount) break;
            vec3 center=uLocalFogPositionShape[fogIndex].xyz;
            bool sphere=uLocalFogPositionShape[fogIndex].w>0.5;
            vec3 extents=max(uLocalFogExtentsRadius[fogIndex].xyz,vec3(0.001));
            float radius=max(uLocalFogExtentsRadius[fogIndex].w,0.001);
            float blend=max(uLocalFogParams[fogIndex].x,0.001);
            float signedDistance;
            if(sphere) signedDistance=length(p-center)-radius;
            else {
                vec3 q=abs(p-center)-extents;
                signedDistance=length(max(q,vec3(0.0)))+min(max(q.x,max(q.y,q.z)),0.0);
            }
            float weight=1.0-smoothstep(-blend,0.0,signedDistance);
            float localDensity=max(uLocalFogAlbedoDensity[fogIndex].w,0.0)*weight;
            float combined=max(density+localDensity,1e-6);
            mediumAlbedo=mix(mediumAlbedo,uLocalFogAlbedoDensity[fogIndex].rgb,
                             localDensity/combined);
            mediumExtinction=mix(mediumExtinction,uLocalFogParams[fogIndex].y,
                                  localDensity/combined);
            mediumAnisotropy=mix(mediumAnisotropy,uLocalFogParams[fogIndex].z,
                                  localDensity/combined);
            density=combined;
        }
        float extinction=max(density*mediumExtinction,0.0);
        float stepTrans=exp(-extinction*ds);
        float phase=hg(dot(ray,uSunDirection),clamp(mediumAnisotropy,-0.94,0.94));
        vec3 lighting=uSkyRadiance+uSunRadiance*phase*sunVisibility(p);
        for(int lightIndex=0;lightIndex<16;++lightIndex) {
            if(lightIndex>=uLightCount) break;
            vec3 toLight=uLightPositionRange[lightIndex].xyz-p;
            float range=max(uLightPositionRange[lightIndex].w,0.01);
            float distanceToLight=length(toLight);
            vec3 lightDirection=toLight/max(distanceToLight,1e-4);
            float rangeFade=clamp(1.0-distanceToLight/range,0.0,1.0);
            rangeFade*=rangeFade;
            float cone=1.0;
            float outer=uLightDirectionOuter[lightIndex].w;
            if(outer>=0.0) cone=smoothstep(outer,min(outer+0.12,1.0),
                dot(-lightDirection,normalize(uLightDirectionOuter[lightIndex].xyz)));
            lighting+=uLightRadiance[lightIndex]*rangeFade*cone*hg(dot(ray,lightDirection),mediumAnisotropy);
        }
        vec3 inscatter=lighting*mediumAlbedo*density*uScattering;
        scatteringSum += transmittance*inscatter*(1.0-stepTrans)/max(extinction,1e-4);
        transmittance*=stepTrans;
    }
    vec4 current=vec4(scatteringSum,transmittance);
    vec4 history=current;
    vec3 historyWorldPosition=uCameraPosition+ray*endDistance;
    vec4 previousClip=uPreviousViewProjection*vec4(historyWorldPosition,1.0);
    vec2 previousUv=previousClip.xy/max(abs(previousClip.w),1e-5)*0.5+0.5;
    bool historyOnScreen=all(greaterThanEqual(previousUv,vec2(0.0)))
                      && all(lessThanEqual(previousUv,vec2(1.0)))
                      && previousClip.w>0.0;
    if(uHistoryValid==1 && historyOnScreen) history=texture(uHistory,previousUv);
    float reject=step(0.02,abs(history.a-current.a));
    float weight=(uHistoryValid==1 && historyOnScreen)?uHistoryWeight*(1.0-reject):0.0;
    FragColor=mix(current,history,clamp(weight,0.0,0.98));
}
)GLSL";

const char* kLuminanceFrag = R"GLSL(
#version 330 core
in vec2 vUV; out float FragColor;
uniform sampler2D uScene;
void main() {
    vec3 c = texture(uScene, vUV).rgb;
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    FragColor = log(max(lum, 1e-4));     // log space averages perceptually
}
)GLSL";

// FXAA (Timothy Lottes, console fit). Edge anti-aliasing on the final LDR image,
// so the HDR/post path gets AA that MSAA on the default framebuffer can't provide.
const char* kFxaaFrag = R"GLSL(
#version 330 core
in vec2 vUV; out vec4 FragColor;
uniform sampler2D uImage;
uniform float uInvW;
uniform float uInvH;
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
void main() {
    vec2 inv = vec2(uInvW, uInvH);
    vec3 rgbM  = texture(uImage, vUV).rgb;
    vec3 rgbNW = texture(uImage, vUV + vec2(-1.0,-1.0) * inv).rgb;
    vec3 rgbNE = texture(uImage, vUV + vec2( 1.0,-1.0) * inv).rgb;
    vec3 rgbSW = texture(uImage, vUV + vec2(-1.0, 1.0) * inv).rgb;
    vec3 rgbSE = texture(uImage, vUV + vec2( 1.0, 1.0) * inv).rgb;
    float lM = luma(rgbM), lNW = luma(rgbNW), lNE = luma(rgbNE), lSW = luma(rgbSW), lSE = luma(rgbSE);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    if (lMax - lMin < 0.05 * lMax) { FragColor = vec4(rgbM, 1.0); return; }  // no edge here
    vec2 dir;
    dir.x = -((lNW + lNE) - (lSW + lSE));
    dir.y =  ((lNW + lSW) - (lNE + lSE));
    float reduce = max((lNW + lNE + lSW + lSE) * 0.25 * 0.125, 1.0 / 128.0);
    float rcp = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcp, vec2(-8.0), vec2(8.0)) * inv;
    vec3 rgbA = 0.5 * (texture(uImage, vUV + dir * (1.0/3.0 - 0.5)).rgb +
                       texture(uImage, vUV + dir * (2.0/3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(uImage, vUV + dir * -0.5).rgb +
                                     texture(uImage, vUV + dir *  0.5).rgb);
    float lB = luma(rgbB);
    FragColor = vec4((lB < lMin || lB > lMax) ? rgbA : rgbB, 1.0);
}
)GLSL";

Mesh MakeFullscreenQuad() {
    const std::vector<float> verts = {
    // x,    y,    u,   v
    -1.0f, -1.0f, 0.0f, 0.0f,
     1.0f, -1.0f, 1.0f, 0.0f,
     1.0f,  1.0f, 1.0f, 1.0f,
    -1.0f,  1.0f, 0.0f, 1.0f,
    };
    const std::vector<std::uint32_t> idx = {0, 1, 2, 0, 2, 3};
    return Mesh(verts, idx, VertexLayout{ {2}, {2} });
}

void UploadEffectParameters(Shader& shader, const PostProcess::Effect& effect) {
    int textureUnit = 4;
    for (const auto& entry : effect.parameters) {
        const auto type = effect.parameterTypes.find(entry.first);
        const int valueType =
            type == effect.parameterTypes.end() ? 0 : type->second;
        const auto texture = effect.textures.find(entry.first);
        textureUnit = UploadShaderParameter(
            shader, entry.first, valueType, entry.second,
            texture == effect.textures.end() ? nullptr : texture->second,
            textureUnit);
    }
}

} // namespace

PostProcess::PostProcess(int width, int height)
    : m_width(width), m_height(height),
      m_hdr(width, height, GL_RGBA16F, true),
      m_bloomA(width / 2, height / 2, GL_RGBA16F, false),
      m_bloomB(width / 2, height / 2, GL_RGBA16F, false),
      m_effectA(width, height, GL_RGBA16F, false),
      m_effectB(width, height, GL_RGBA16F, false),
      m_ldr(width, height, GL_RGBA8, false),
      m_bright(kFullscreenVert, kBrightFrag),
      m_blur(kFullscreenVert, kBlurFrag),
      m_composite(kFullscreenVert, kCompositeFrag),
      m_luminance(kFullscreenVert, kLuminanceFrag),
      m_fxaa(kFullscreenVert, kFxaaFrag),
      m_volumetricShader(kFullscreenVert, kVolumetricFrag),
      m_volumetricA(std::max(width / 4, 1), std::max(height / 4, 1), GL_RGBA16F, false),
      m_volumetricB(std::max(width / 4, 1), std::max(height / 4, 1), GL_RGBA16F, false),
      m_quad(MakeFullscreenQuad()) {
    glGenFramebuffers(1, &m_lumFbo);
    glGenTextures(1, &m_lumTex);
    glBindTexture(GL_TEXTURE_2D, m_lumTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, m_lumSize, m_lumSize, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindFramebuffer(GL_FRAMEBUFFER, m_lumFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_lumTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    const float normal[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    glGenTextures(1, &m_fallbackNormal);
    glBindTexture(GL_TEXTURE_2D, m_fallbackNormal);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 1, 1, 0,
                 GL_RGBA, GL_FLOAT, normal);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    glGenTextures(1, &m_fallbackIndirect);
    glBindTexture(GL_TEXTURE_2D, m_fallbackIndirect);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 1, 1, 0, GL_RGBA, GL_FLOAT, black);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    const float velocity[2] = {0.0f, 0.0f};
    glGenTextures(1, &m_fallbackVelocity);
    glBindTexture(GL_TEXTURE_2D, m_fallbackVelocity);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, 1, 1, 0,
                 GL_RG, GL_FLOAT, velocity);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

}

PostProcess::~PostProcess() {
    if (m_lumTex) glDeleteTextures(1, &m_lumTex);
    if (m_lumFbo) glDeleteFramebuffers(1, &m_lumFbo);
    if (m_fallbackNormal) glDeleteTextures(1, &m_fallbackNormal);
    if (m_fallbackVelocity) glDeleteTextures(1, &m_fallbackVelocity);
    if (m_fallbackIndirect) glDeleteTextures(1, &m_fallbackIndirect);
}

std::uint64_t PostProcess::MemoryBytes() const {
    const std::uint64_t full = static_cast<std::uint64_t>(m_width) * m_height;
    const std::uint64_t half = static_cast<std::uint64_t>(std::max(m_width / 2, 1))
        * std::max(m_height / 2, 1);
    const std::uint64_t volume = static_cast<std::uint64_t>(std::max(m_width / m_volumeDownsample, 1))
        * std::max(m_height / m_volumeDownsample, 1);
    // HDR color+depth, two bloom buffers, two graph intermediates, LDR,
    // two volumetric history buffers and the luminance histogram target.
    return full * (8u + 4u + 8u + 8u + 4u)
        + half * 8u * 2u + volume * 8u * 2u
        + static_cast<std::uint64_t>(m_lumSize) * m_lumSize * sizeof(float);
}

void PostProcess::BeginScene() {
    m_hdr.Bind();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void PostProcess::Resize(int width, int height) {
    const int volumeDownsample = std::clamp(volumetrics.xyDownsample, 2, 8);
    const bool sceneSizeChanged = width != m_width || height != m_height;
    const bool volumeSizeChanged = volumeDownsample != m_volumeDownsample;
    if (!sceneSizeChanged && !volumeSizeChanged) return;
    m_width = width; m_height = height;
    if (sceneSizeChanged) {
        m_hdr.Resize(width, height);
        m_bloomA.Resize(width / 2, height / 2);
        m_bloomB.Resize(width / 2, height / 2);
        m_effectA.Resize(width, height);
        m_effectB.Resize(width, height);
        m_ldr.Resize(width, height);
    }
    m_volumeDownsample = volumeDownsample;
    m_volumetricA.Resize(std::max(width / volumeDownsample, 1),
                         std::max(height / volumeDownsample, 1));
    m_volumetricB.Resize(std::max(width / volumeDownsample, 1),
                         std::max(height / volumeDownsample, 1));
    m_volumeHistoryValid = false;
}

void PostProcess::RenderToScreen(int screenWidth, int screenHeight, float dt) {
    RenderComposite(screenWidth, screenHeight, dt, nullptr);
}

void PostProcess::RenderToFramebuffer(const Framebuffer& target, float dt) {
    RenderComposite(target.Width(), target.Height(), dt, &target);
}

void PostProcess::RenderComposite(int screenWidth, int screenHeight, float dt,
                                  const Framebuffer* target) {
    glDisable(GL_DEPTH_TEST);
    m_time += std::max(dt, 0.0f);

    // Camera-aligned, quarter-resolution froxel-column integration. Depth slices
    // use a quadratic/log-like distribution, concentrating samples near the camera.
    // The RGBA16F result stores in-scattering.rgb + transmittance.a and is temporally
    // accumulated before depth-aware composition in the HDR pass.
    Framebuffer* volumeCurrent = &m_volumetricA;
    Framebuffer* volumeHistory = &m_volumetricB;
    const bool volumeActive = volumetrics.enabled
        && (volumetrics.density > 0.000001f || !m_localFogVolumes.empty());
    if (volumeActive) {
        volumeCurrent->Bind();
        m_volumetricShader.Bind();
        m_hdr.BindDepthTexture(0); m_volumetricShader.SetInt("uDepth",0);
        volumeHistory->BindColorTexture(1); m_volumetricShader.SetInt("uHistory",1);
        m_volumetricShader.SetMat4("uInvViewProjection",m_inverseViewProjection);
        m_volumetricShader.SetMat4("uPreviousViewProjection",m_previousViewProjection);
        m_volumetricShader.SetVec3("uCameraPosition",m_cameraPosition);
        m_volumetricShader.SetVec3("uSunDirection",m_environment.sunDirection);
        m_volumetricShader.SetVec3("uSunRadiance",m_environment.sunRadiance);
        m_volumetricShader.SetVec3("uSkyRadiance",m_environment.ambientRadiance);
        m_volumetricShader.SetVec3("uAlbedo",volumetrics.scatteringAlbedo);
        m_volumetricShader.SetFloat("uDensity",volumetrics.density);
        m_volumetricShader.SetFloat("uScattering",volumetrics.scattering);
        m_volumetricShader.SetFloat("uExtinction",volumetrics.extinction);
        m_volumetricShader.SetFloat("uAnisotropy",std::clamp(volumetrics.anisotropy,-0.94f,0.94f));
        m_volumetricShader.SetFloat("uBaseHeight",volumetrics.baseHeight);
        m_volumetricShader.SetFloat("uHeightFalloff",volumetrics.heightFalloff);
        m_volumetricShader.SetFloat("uStartDistance",volumetrics.startDistance);
        m_volumetricShader.SetFloat("uMaxDistance",volumetrics.maxDistance);
        m_volumetricShader.SetFloat("uHistoryWeight",volumetrics.historyWeight);
        m_volumetricShader.SetInt("uSlices",volumetrics.depthSlices);
        m_volumetricShader.SetInt("uHistoryValid",m_volumeHistoryValid?1:0);
        m_volumetricShader.SetMat4("uView",m_volumeView);
        if(m_directionalShadow) {
            m_directionalShadow->BindArray(2);
            m_volumetricShader.SetInt("uShadowMap",2);
            for(int i=0;i<CascadedShadow::kCascades;++i)
                m_volumetricShader.SetMat4("uShadowVP["+std::to_string(i)+"]",m_directionalShadow->CascadeVP(i));
            m_volumetricShader.SetVec4("uCascadeSplits",glm::vec4(
                m_directionalShadow->SplitDepth(0),m_directionalShadow->SplitDepth(1),
                m_directionalShadow->SplitDepth(2),m_directionalShadow->SplitDepth(3)));
            m_volumetricShader.SetInt("uShadowEnabled",1);
        } else m_volumetricShader.SetInt("uShadowEnabled",0);
        m_volumetricShader.SetInt("uLightCount",static_cast<int>(m_volumetricLights.size()));
        for(std::size_t i=0;i<m_volumetricLights.size();++i) {
            const auto& light=m_volumetricLights[i]; const std::string suffix="["+std::to_string(i)+"]";
            m_volumetricShader.SetVec4("uLightPositionRange"+suffix,glm::vec4(light.position,light.range));
            m_volumetricShader.SetVec4("uLightDirectionOuter"+suffix,glm::vec4(light.direction,light.outerCos));
            m_volumetricShader.SetVec3("uLightRadiance"+suffix,light.radiance);
        }
        m_volumetricShader.SetInt("uLocalFogCount", static_cast<int>(m_localFogVolumes.size()));
        for (std::size_t i = 0; i < m_localFogVolumes.size(); ++i) {
            const auto& fog = m_localFogVolumes[i];
            const std::string suffix = "[" + std::to_string(i) + "]";
            m_volumetricShader.SetVec4("uLocalFogPositionShape" + suffix,
                glm::vec4(fog.position, fog.sphere ? 1.0f : 0.0f));
            m_volumetricShader.SetVec4("uLocalFogExtentsRadius" + suffix,
                glm::vec4(glm::max(fog.boxExtents, glm::vec3(0.001f)), std::max(fog.radius, 0.001f)));
            m_volumetricShader.SetVec4("uLocalFogAlbedoDensity" + suffix,
                glm::vec4(glm::clamp(fog.albedo, glm::vec3(0.0f), glm::vec3(1.0f)),
                          std::max(fog.density, 0.0f)));
            m_volumetricShader.SetVec4("uLocalFogParams" + suffix,
                glm::vec4(std::max(fog.blendDistance, 0.001f),
                          std::max(fog.extinction, 0.0f),
                          std::clamp(fog.anisotropy, -0.94f, 0.94f), 0.0f));
        }
        m_quad.Draw();
        std::swap(m_volumetricA,m_volumetricB);
        volumeHistory=&m_volumetricB;
        m_volumeHistoryValid=true;
    }

    // Graph-authored effects run in author-defined order on the linear HDR
    // scene. All effects sample the original scene depth while colour
    // ping-pongs between full-resolution buffers.
    const Framebuffer* scene = &m_hdr;
    bool writeA = true;
    for (const Effect& effect : m_effects) {
        if (!effect.enabled || !effect.shader) continue;
        Framebuffer& destination = writeA ? m_effectA : m_effectB;
        destination.Bind();
        Shader& shader = *const_cast<Shader*>(effect.shader);
        shader.Bind();
        scene->BindColorTexture(0);
        shader.SetInt("uSceneColor", 0);
        m_hdr.BindDepthTexture(1);
        shader.SetInt("uSceneDepth", 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D,
            m_sceneNormal ? m_sceneNormal : m_fallbackNormal);
        shader.SetInt("uSceneNormal", 2);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D,
            m_sceneVelocity ? m_sceneVelocity : m_fallbackVelocity);
        shader.SetInt("uSceneVelocity", 3);
        shader.SetVec2("uTexelSize", glm::vec2(
            1.0f / static_cast<float>(std::max(m_width, 1)),
            1.0f / static_cast<float>(std::max(m_height, 1))));
        shader.SetFloat("uExposure", m_exposure);
        shader.SetFloat("uTime", m_time);
        shader.SetFloat("uDeltaTime", std::max(dt, 0.0f));
        UploadEffectParameters(shader, effect);
        m_quad.Draw();
        scene = &destination;
        writeA = !writeA;
    }

    // --- Histogram auto-exposure. The luminance reduction remains on GPU; a
    // throttled 64x64 readback builds a robust percentile histogram so a tiny sun
    // or emissive pixel cannot dominate adaptation. ---
    float exposure = settings.exposure;
    if (settings.autoExposure) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_lumFbo);
        glViewport(0, 0, m_lumSize, m_lumSize);
        m_luminance.Bind();
        scene->BindColorTexture(0);
        m_luminance.SetInt("uScene", 0);
        m_quad.Draw();
        glBindTexture(GL_TEXTURE_2D, m_lumTex);
        glGenerateMipmap(GL_TEXTURE_2D);
        if ((m_exposureFrame++ & 3u) == 0u || m_resetExposure) {
            constexpr int kReadMip = 2;
            const int side = m_lumSize >> kReadMip;
            std::vector<float> logLuminance(static_cast<std::size_t>(side * side));
            glGetTexImage(GL_TEXTURE_2D, kReadMip, GL_RED, GL_FLOAT, logLuminance.data());
            std::sort(logLuminance.begin(), logLuminance.end());
            const std::size_t lo = static_cast<std::size_t>(std::clamp(
                settings.histogramLowPercent, 0.0f, 0.99f) * float(logLuminance.size() - 1));
            const std::size_t hi = static_cast<std::size_t>(std::clamp(
                settings.histogramHighPercent, settings.histogramLowPercent + 0.001f, 1.0f)
                * float(logLuminance.size() - 1));
            double sum = 0.0;
            for (std::size_t i = lo; i <= hi; ++i) sum += logLuminance[i];
            const float avgLog = static_cast<float>(sum / double(std::max<std::size_t>(hi - lo + 1, 1)));
            const float avgLum = std::exp(avgLog);
            const float targetExposure = settings.exposureKey / std::max(avgLum, 1e-4f);
            m_targetEV = std::clamp(std::log2(std::max(targetExposure, 1e-6f))
                + settings.exposureCompensationEV, settings.minEV, settings.maxEV);
        }
        if (std::abs(m_targetEV - m_currentEV) < std::max(settings.exposureDeadZoneEV, 0.0f))
            m_targetEV = m_currentEV;
        const float targetExposure = std::exp2(m_targetEV);
        const float speed = targetExposure > m_exposure ? settings.adaptationSpeedUp
                                                        : settings.adaptationSpeedDown;
        if (m_resetExposure || dt <= 0.0f) m_exposure = targetExposure;
        else m_exposure += (targetExposure - m_exposure) * (1.0f - std::exp(-dt * std::max(speed, 0.0f)));
        m_currentEV = std::log2(std::max(m_exposure, 1e-6f));
        m_resetExposure = false;
        exposure = m_exposure;
    }

    Framebuffer* src = &m_bloomA;
    if (settings.bloom && settings.bloomStrength > 0.0001f) {
        // Bright-pass into the half-res bloom buffer. Disabled bloom performs no
        // extraction or blur work; the composite strength is already zero.
        m_bloomA.Bind();
        m_bright.Bind();
        scene->BindColorTexture(0);
        m_bright.SetInt("uScene", 0);
        m_bright.SetFloat("uThreshold", settings.bloomThreshold);
        m_bright.SetFloat("uKnee", settings.bloomKnee);
        m_quad.Draw();

        m_blur.Bind();
        m_blur.SetInt("uImage", 0);
        Framebuffer* dst = &m_bloomB;
        bool horizontal = true;
        const int bloomPasses = std::clamp(settings.bloomLevels, 1, 6) * 2;
        for (int i = 0; i < bloomPasses; i++) {
            dst->Bind();
            src->BindColorTexture(0);
            m_blur.SetFloat("uHorizontal", horizontal ? 1.0f : 0.0f);
            m_quad.Draw();
            std::swap(src, dst);
            horizontal = !horizontal;
        }
        glBindTexture(GL_TEXTURE_2D, src->ColorTexture());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    // Composite (scene + bloom, exposure, ACES, gamma). With FXAA on, render into the
    // LDR buffer first; otherwise composite straight to the final target.
    if (settings.fxaa) m_ldr.Bind();
    else if (target)   target->Bind();
    else               Framebuffer::BindDefault(screenWidth, screenHeight);
    m_composite.Bind();
    scene->BindColorTexture(0); m_composite.SetInt("uScene", 0);
    src->BindColorTexture(1);  m_composite.SetInt("uBloom", 1);
    m_hdr.BindDepthTexture(2); m_composite.SetInt("uSceneDepth", 2);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_indirectTexture ? m_indirectTexture : m_fallbackIndirect);
    m_composite.SetInt("uIndirect", 3);
    m_composite.SetFloat("uIndirectStrength", m_indirectTexture ? std::clamp(m_indirectStrength,0.0f,1.0f) : 0.0f);
    m_composite.SetInt("uIndirectDebug", m_indirectDebug ? 1 : 0);
    volumeHistory->BindColorTexture(4); m_composite.SetInt("uVolumetric",4);
    m_composite.SetInt("uVolumetricEnabled",volumeActive?1:0);
    m_composite.SetFloat("uExposure", exposure);
    m_composite.SetFloat("uBloomStrength", settings.bloom ? settings.bloomStrength : 0.0f);
    m_composite.SetFloat("uTime", m_time);
    m_composite.SetFloat("uUnderwaterBlend", underwater.blend);
    m_composite.SetVec3("uUnderwaterTint", underwater.tint);
    m_composite.SetFloat("uUnderwaterFogDensity", underwater.fogDensity);
    m_composite.SetFloat("uUnderwaterDistortion", underwater.distortion);
    m_composite.SetFloat("uUnderwaterCausticsStrength", underwater.causticsStrength);
    m_composite.SetFloat("uUnderwaterCausticsScale", underwater.causticsScale);
    m_composite.SetFloat("uSaturation", settings.saturation);
    m_composite.SetFloat("uContrast", settings.contrast);
    m_composite.SetFloat("uTemperature", settings.temperature);
    m_composite.SetFloat("uTint", settings.tint);
    m_composite.SetVec3("uLift", settings.lift);
    m_composite.SetVec3("uGamma", settings.gamma);
    m_composite.SetVec3("uGain", settings.gain);
    const bool validLut = m_colorLut && m_colorLut->Height() >= 2
        && m_colorLut->Width() == m_colorLut->Height() * m_colorLut->Height();
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, validLut ? m_colorLut->ID() : m_fallbackIndirect);
    m_composite.SetInt("uColorLut", 5);
    m_composite.SetInt("uHasColorLut", validLut ? 1 : 0);
    m_composite.SetFloat("uLutIntensity", std::clamp(settings.lutIntensity, 0.0f, 1.0f));
    m_composite.SetFloat("uLutSize", validLut ? static_cast<float>(m_colorLut->Height()) : 2.0f);
    m_quad.Draw();

    // FXAA the LDR result to the final target (edge AA for the offscreen path).
    if (settings.fxaa) {
        if (target) target->Bind();
        else Framebuffer::BindDefault(screenWidth, screenHeight);
        m_fxaa.Bind();
        m_ldr.BindColorTexture(0); m_fxaa.SetInt("uImage", 0);
        m_fxaa.SetFloat("uInvW", 1.0f / static_cast<float>(m_ldr.Width()));
        m_fxaa.SetFloat("uInvH", 1.0f / static_cast<float>(m_ldr.Height()));
        m_quad.Draw();
    }

    glEnable(GL_DEPTH_TEST);
}

} // namespace engine
