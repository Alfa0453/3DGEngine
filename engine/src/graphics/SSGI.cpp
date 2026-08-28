#include "engine/graphics/SSGI.h"
#include "engine/graphics/VertexLayout.h"

#include <glad/glad.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

namespace engine {
namespace {
const char* kVertex = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPosition;
layout(location=1) in vec2 aUv;
out vec2 vUv;
void main(){vUv=aUv;gl_Position=vec4(aPosition,0.0,1.0);}
)GLSL";

const char* kTrace = R"GLSL(
#version 330 core
in vec2 vUv;out vec4 FragColor;
uniform sampler2D uScene;
uniform sampler2D uPosition;
uniform sampler2D uNormal;
uniform mat4 uProjection;
uniform float uRayLength;
uniform float uThickness;
uniform int uSteps;
const float PI=3.14159265359;
vec3 tangentFor(vec3 n){return normalize(abs(n.z)<0.999?cross(n,vec3(0,0,1)):cross(n,vec3(0,1,0)));}
void main(){
    vec3 origin=texture(uPosition,vUv).xyz;
    vec3 normal=normalize(texture(uNormal,vUv).xyz);
    if(dot(origin,origin)<1e-8||dot(normal,normal)<0.5){FragColor=vec4(0);return;}
    vec3 tangent=tangentFor(normal),bitangent=cross(normal,tangent);
    vec3 indirect=vec3(0);float weight=0.0;
    for(int ray=0;ray<4;++ray){
        float angle=(float(ray)+fract(dot(vUv,vec2(91.7,37.3))))*1.5707963;
        vec3 direction=normalize(normal*0.62+tangent*cos(angle)*0.55+bitangent*sin(angle)*0.55);
        for(int stepIndex=1;stepIndex<=48;++stepIndex){
            if(stepIndex>uSteps)break;
            float t=uRayLength*float(stepIndex)/float(max(uSteps,1));
            vec3 rayPosition=origin+direction*t;
            vec4 clip=uProjection*vec4(rayPosition,1.0);
            if(clip.w<=0.0)break;
            vec2 uv=clip.xy/clip.w*0.5+0.5;
            if(any(lessThan(uv,vec2(0.002)))||any(greaterThan(uv,vec2(0.998))))break;
            vec3 hitPosition=texture(uPosition,uv).xyz;
            if(dot(hitPosition,hitPosition)<1e-8)continue;
            float separation=abs(hitPosition.z-rayPosition.z);
            if(separation<=uThickness*(1.0+t*0.08)){
                vec3 hitNormal=normalize(texture(uNormal,uv).xyz);
                float facing=max(dot(hitNormal,-direction),0.0);
                float falloff=1.0-t/max(uRayLength,0.001);
                falloff*=falloff; // SSGI is deliberately near-field detail, not another full GI baseline.
                indirect+=texture(uScene,uv).rgb*facing*falloff;
                weight+=max(facing,0.05);break;
            }
        }
    }
    indirect=weight>0.0?indirect/weight:vec3(0);
    float edgeDistance=min(min(vUv.x,vUv.y),min(1.0-vUv.x,1.0-vUv.y));
    float edgeConfidence=smoothstep(0.0,0.075,edgeDistance);
    FragColor=vec4(max(indirect,vec3(0))*edgeConfidence,
                   (weight>0.0?1.0:0.0)*edgeConfidence);
}
)GLSL";

const char* kDenoise = R"GLSL(
#version 330 core
in vec2 vUv;out vec4 FragColor;
uniform sampler2D uIndirect;
uniform sampler2D uPosition;
uniform sampler2D uNormal;
void main(){
    vec2 texel=1.0/vec2(textureSize(uIndirect,0));
    vec3 centerPosition=texture(uPosition,vUv).xyz;
    vec3 centerNormal=normalize(texture(uNormal,vUv).xyz);
    vec3 sum=vec3(0);float total=0.0;
    for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x){
        vec2 uv=clamp(vUv+vec2(x,y)*texel,vec2(0.001),vec2(0.999));
        vec4 sampleValue=texture(uIndirect,uv);
        vec3 samplePosition=texture(uPosition,uv).xyz;
        vec3 sampleNormal=normalize(texture(uNormal,uv).xyz);
        float spatial=exp(-0.35*float(x*x+y*y));
        float depth=exp(-abs(samplePosition.z-centerPosition.z)*8.0);
        float normalWeight=pow(max(dot(centerNormal,sampleNormal),0.0),16.0);
        float w=spatial*depth*normalWeight*sampleValue.a;
        sum+=sampleValue.rgb*w;total+=w;
    }
    FragColor=vec4(total>1e-5?sum/total:vec3(0),1.0);
}
)GLSL";

Mesh Quad(){
    const std::vector<float> vertices={-1,-1,0,0, 1,-1,1,0, 1,1,1,1, -1,1,0,1};
    const std::vector<std::uint32_t> indices={0,1,2,0,2,3};
    return Mesh(vertices,indices,VertexLayout{{2},{2}});
}
}

SSGI::SSGI(int width,int height)
    :m_width(std::max(width/2,1)),m_height(std::max(height/2,1)),
     m_raw(m_width,m_height,GL_RGBA16F,false),m_filtered(m_width,m_height,GL_RGBA16F,false),
     m_trace(kVertex,kTrace),m_denoise(kVertex,kDenoise),m_quad(Quad()){}

void SSGI::Resize(int width,int height){
    const int w=std::max(width/2,1),h=std::max(height/2,1);
    if(w==m_width&&h==m_height)return;m_width=w;m_height=h;m_raw.Resize(w,h);m_filtered.Resize(w,h);
}

void SSGI::Generate(unsigned int sceneColor,unsigned int viewPosition,
                    unsigned int viewNormal,const glm::mat4& projection){
    if(!sceneColor||!viewPosition||!viewNormal)return;
    const auto begin=std::chrono::steady_clock::now();
    glDisable(GL_DEPTH_TEST);m_raw.Bind();glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
    m_trace.Bind();
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,sceneColor);m_trace.SetInt("uScene",0);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,viewPosition);m_trace.SetInt("uPosition",1);
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,viewNormal);m_trace.SetInt("uNormal",2);
    m_trace.SetMat4("uProjection",projection);m_trace.SetFloat("uRayLength",std::clamp(rayLength,0.25f,20.0f));
    m_trace.SetFloat("uThickness",std::clamp(thickness,0.01f,2.0f));m_trace.SetInt("uSteps",std::clamp(steps,4,48));m_quad.Draw();
    const auto denoiseBegin=std::chrono::steady_clock::now();
    m_filtered.Bind();glClear(GL_COLOR_BUFFER_BIT);m_denoise.Bind();
    m_raw.BindColorTexture(0);m_denoise.SetInt("uIndirect",0);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,viewPosition);m_denoise.SetInt("uPosition",1);
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,viewNormal);m_denoise.SetInt("uNormal",2);m_quad.Draw();
    m_lastDenoiseMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-denoiseBegin).count();
    m_lastMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    glEnable(GL_DEPTH_TEST);
}
} // namespace engine
