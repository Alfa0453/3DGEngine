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
namespace {
std::int64_t CellKey(const glm::ivec3& cell) {
    constexpr std::int64_t mask=0x1fffff;
    return ((static_cast<std::int64_t>(cell.x)&mask)<<42)
         | ((static_cast<std::int64_t>(cell.y)&mask)<<21)
         | (static_cast<std::int64_t>(cell.z)&mask);
}
glm::ivec3 CellFor(const glm::vec3& position,float cellSize){return glm::ivec3(glm::floor(position/cellSize));}
}
ReflectionProbeSystem::~ReflectionProbeSystem(){Clear();}
void ReflectionProbeSystem::Clear(){for(auto& pair:m_entries)if(pair.second.texture)glDeleteTextures(1,&pair.second.texture);m_entries.clear();m_spatialGrid.clear();m_memoryBytes=0;m_residentCount=0;}

void ReflectionProbeSystem::SetStreaming(float distance,std::uint64_t budgetBytes,int maxSampled){
    m_streamingDistance=std::max(distance,16.0f);m_memoryBudgetBytes=std::max<std::uint64_t>(budgetBytes,8ull*1024ull*1024ull);
    m_maxSampled=std::clamp(maxSampled,0,2);
}

void ReflectionProbeSystem::Sync(ecs::Registry& registry,const glm::vec3& referencePosition){
    if(m_textureUnits==0)glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&m_textureUnits);
    std::unordered_set<std::uint32_t> live;
    registry.view<ecs::Transform,ecs::ReflectionProbe>().each([&](ecs::Entity entity,ecs::Transform& transform,ecs::ReflectionProbe& probe){
        const std::uint32_t key=static_cast<std::uint32_t>(entity);live.insert(key);
        const float influence=probe.shape==ecs::ReflectionProbe::Shape::Sphere?probe.radius:glm::length(probe.boxExtents);
        const bool relevant=glm::distance(referencePosition,transform.position)<=m_streamingDistance+influence;
        auto found=m_entries.find(key);const bool reload=found==m_entries.end()||found->second.path!=probe.bakedCubemapPath||
            (probe.enabled&&relevant&&!probe.bakedCubemapPath.empty()&&found->second.texture==0);
        if(reload){if(found!=m_entries.end()&&found->second.texture)glDeleteTextures(1,&found->second.texture);
            Entry entry;entry.path=probe.bakedCubemapPath;entry.id=probe.stableId;
            if(probe.enabled&&relevant&&!entry.path.empty())entry.texture=IBL::LoadPrefilteredCubemap(entry.path,&entry.resolution,&entry.mipCount,nullptr);
            m_entries[key]=std::move(entry);found=m_entries.find(key);}
        Entry& entry=found->second;entry.position=transform.position;entry.extents=glm::max(probe.boxExtents*glm::abs(transform.scale),glm::vec3(0.01f));
        entry.radius=std::max(probe.radius*std::max({std::abs(transform.scale.x),std::abs(transform.scale.y),std::abs(transform.scale.z)}),0.01f);
        entry.blend=std::max(probe.blendDistance,0.001f);entry.intensity=std::max(probe.intensity,0.0f);
        entry.priority=probe.priority;entry.shape=probe.shape==ecs::ReflectionProbe::Shape::Sphere?1:0;
        if((!probe.enabled||!relevant)&&entry.texture){glDeleteTextures(1,&entry.texture);entry.texture=0;entry.resolution=0;entry.mipCount=0;}
    });
    for(auto it=m_entries.begin();it!=m_entries.end();)if(!live.count(it->first)){if(it->second.texture)glDeleteTextures(1,&it->second.texture);it=m_entries.erase(it);}else++it;
    m_memoryBytes=0;m_residentCount=0;m_spatialGrid.clear();for(const auto& pair:m_entries){const Entry&e=pair.second;
        const glm::vec3 influence=e.shape?glm::vec3(e.radius):e.extents;
        const glm::ivec3 first=CellFor(e.position-influence,m_cellSize),last=CellFor(e.position+influence,m_cellSize);
        for(int z=first.z;z<=last.z;++z)for(int y=first.y;y<=last.y;++y)for(int x=first.x;x<=last.x;++x)
            m_spatialGrid[CellKey(glm::ivec3(x,y,z))].push_back(pair.first);
        if(e.texture&&e.resolution>0){m_memoryBytes+=static_cast<std::uint64_t>(e.resolution)*e.resolution*6u*6u*4u/3u;++m_residentCount;}}
}

void ReflectionProbeSystem::BindBest(Shader& shader,const glm::vec3& p,unsigned int firstUnit)const{
    // Assign both cube samplers even when no probe is selected. Leaving an
    // active sampler at its default unit 0 can alias the 2D albedo sampler;
    // OpenGL rejects the whole draw when different sampler types share a unit.
    shader.SetInt("uLocalReflection0",static_cast<int>(firstUnit));
    shader.SetInt("uLocalReflection1",static_cast<int>(firstUnit+1));
    if(m_textureUnits<=static_cast<int>(firstUnit+1)||m_maxSampled<=0){shader.SetInt("uReflectionProbeCount",0);return;}
    std::array<const Entry*,2> best{{nullptr,nullptr}};
    auto score=[&](const Entry&e){glm::vec3 d=glm::max(glm::abs(p-e.position)-e.extents,glm::vec3(0.0f));float distance=e.shape?std::max(glm::length(p-e.position)-e.radius,0.0f):glm::length(d);return float(e.priority)*100000.0f-distance;};
    std::vector<const Entry*> candidates;std::unordered_set<std::uint32_t> seen;const glm::ivec3 center=CellFor(p,m_cellSize);
    for(int z=-1;z<=1;++z)for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){auto found=m_spatialGrid.find(CellKey(center+glm::ivec3(x,y,z)));if(found==m_spatialGrid.end())continue;
        for(std::uint32_t key:found->second){if(!seen.insert(key).second)continue;auto entry=m_entries.find(key);if(entry!=m_entries.end())candidates.push_back(&entry->second);}}
    m_candidateCountLastQuery=candidates.size();
    for(const Entry* candidate:candidates){const Entry&e=*candidate;if(!e.texture)continue;
        const bool inside=e.shape?glm::length(p-e.position)<=e.radius:
            glm::all(glm::lessThanEqual(glm::abs(p-e.position),e.extents));
        if(!inside)continue;
        if(!best[0]||score(e)>score(*best[0])){best[1]=best[0];best[0]=&e;}else if(!best[1]||score(e)>score(*best[1]))best[1]=&e;}
    const int highestPriority=best[0]?best[0]->priority:0;
    int count=0;for(int i=0;i<m_maxSampled;++i){const Entry*e=best[i];if(!e)continue;const unsigned unit=firstUnit+static_cast<unsigned>(i);
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
