#include "engine/graphics/ReflectionProbeSystem.h"
#include "engine/graphics/IBL.h"
#include "engine/graphics/Shader.h"
#include "engine/ecs/Components.h"
#include "engine/ecs/Registry.h"
#include <glad/glad.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_set>

namespace engine {
ReflectionProbeSystem::~ReflectionProbeSystem(){Clear();}
void ReflectionProbeSystem::Clear(){for(auto& pair:m_entries)if(pair.second.texture)glDeleteTextures(1,&pair.second.texture);m_entries.clear();m_memoryBytes=0;}

void ReflectionProbeSystem::Sync(ecs::Registry& registry){
    std::unordered_set<std::uint32_t> live;
    registry.view<ecs::Transform,ecs::ReflectionProbe>().each([&](ecs::Entity entity,ecs::Transform& transform,ecs::ReflectionProbe& probe){
        const std::uint32_t key=static_cast<std::uint32_t>(entity);live.insert(key);
        auto found=m_entries.find(key);const bool reload=found==m_entries.end()||found->second.path!=probe.bakedCubemapPath||
            (probe.enabled&&!probe.bakedCubemapPath.empty()&&found->second.texture==0);
        if(reload){if(found!=m_entries.end()&&found->second.texture)glDeleteTextures(1,&found->second.texture);
            Entry entry;entry.path=probe.bakedCubemapPath;entry.id=probe.stableId;
            if(probe.enabled&&!entry.path.empty())entry.texture=IBL::LoadPrefilteredCubemap(entry.path,&entry.resolution,&entry.mipCount,nullptr);
            m_entries[key]=std::move(entry);found=m_entries.find(key);}
        Entry& entry=found->second;entry.position=transform.position;entry.extents=glm::max(probe.boxExtents*glm::abs(transform.scale),glm::vec3(0.01f));
        entry.radius=std::max(probe.radius*std::max({std::abs(transform.scale.x),std::abs(transform.scale.y),std::abs(transform.scale.z)}),0.01f);
        entry.blend=std::max(probe.blendDistance,0.001f);entry.intensity=std::max(probe.intensity,0.0f);
        entry.priority=probe.priority;entry.shape=probe.shape==ecs::ReflectionProbe::Shape::Sphere?1:0;
        if(!probe.enabled&&entry.texture){glDeleteTextures(1,&entry.texture);entry.texture=0;}
    });
    for(auto it=m_entries.begin();it!=m_entries.end();)if(!live.count(it->first)){if(it->second.texture)glDeleteTextures(1,&it->second.texture);it=m_entries.erase(it);}else++it;
    m_memoryBytes=0;for(const auto& pair:m_entries){const Entry&e=pair.second;if(e.texture&&e.resolution>0)m_memoryBytes+=static_cast<std::uint64_t>(e.resolution)*e.resolution*6u*6u*4u/3u;}
}

void ReflectionProbeSystem::BindBest(Shader& shader,const glm::vec3& p,unsigned int firstUnit)const{
    // Assign both cube samplers even when no probe is selected. Leaving an
    // active sampler at its default unit 0 can alias the 2D albedo sampler;
    // OpenGL rejects the whole draw when different sampler types share a unit.
    shader.SetInt("uLocalReflection0",static_cast<int>(firstUnit));
    shader.SetInt("uLocalReflection1",static_cast<int>(firstUnit+1));
    GLint textureUnits=0;glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&textureUnits);
    if(textureUnits<=static_cast<GLint>(firstUnit+1)){shader.SetInt("uReflectionProbeCount",0);return;}
    std::array<const Entry*,2> best{{nullptr,nullptr}};
    auto score=[&](const Entry&e){glm::vec3 d=glm::max(glm::abs(p-e.position)-e.extents,glm::vec3(0.0f));float distance=e.shape?std::max(glm::length(p-e.position)-e.radius,0.0f):glm::length(d);return float(e.priority)*100000.0f-distance;};
    for(const auto& pair:m_entries){const Entry&e=pair.second;if(!e.texture)continue;
        const bool inside=e.shape?glm::length(p-e.position)<=e.radius:
            glm::all(glm::lessThanEqual(glm::abs(p-e.position),e.extents));
        if(!inside)continue;
        if(!best[0]||score(e)>score(*best[0])){best[1]=best[0];best[0]=&e;}else if(!best[1]||score(e)>score(*best[1]))best[1]=&e;}
    const int highestPriority=best[0]?best[0]->priority:0;
    int count=0;for(int i=0;i<2;++i){const Entry*e=best[i];if(!e)continue;const unsigned unit=firstUnit+static_cast<unsigned>(i);
        glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_CUBE_MAP,e->texture);
        shader.SetInt(i==0?"uLocalReflection0":"uLocalReflection1",static_cast<int>(unit));
        const std::string index="["+std::to_string(i)+"]";shader.SetVec3("uReflectionProbePosition"+index,e->position);
        shader.SetVec3("uReflectionProbeExtents"+index,e->extents);shader.SetFloat("uReflectionProbeRadius"+index,e->radius);
        shader.SetFloat("uReflectionProbeBlend"+index,e->blend);shader.SetFloat("uReflectionProbeIntensity"+index,e->intensity);
        shader.SetFloat("uReflectionProbePriorityFactor"+index,
            std::exp2(static_cast<float>(std::clamp(e->priority-highestPriority,-8,0))));
        shader.SetInt("uReflectionProbeShape"+index,e->shape);shader.SetFloat("uReflectionProbeMaxLod"+index,float(std::max(e->mipCount-1,0)));++count;}
    shader.SetInt("uReflectionProbeCount",count);
}
} // namespace engine
