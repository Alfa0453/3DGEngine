#include "engine/graphics/LightingBuildData.h"

#include <glad/glad.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numeric>

namespace engine {
namespace {

constexpr char kMagic[8] = {'3','D','G','L','I','T','E','1'};

template <class T> void HashBytes(std::uint64_t& h, const T& value) {
    const auto* p = reinterpret_cast<const unsigned char*>(&value);
    for (std::size_t i = 0; i < sizeof(T); ++i) { h ^= p[i]; h *= 1099511628211ull; }
}

bool RayTriangle(const glm::vec3& origin, const glm::vec3& direction,
                 const LightingTriangle& triangle, float maxDistance) {
    const glm::vec3 e1 = triangle.b - triangle.a;
    const glm::vec3 e2 = triangle.c - triangle.a;
    const glm::vec3 p = glm::cross(direction, e2);
    const float det = glm::dot(e1, p);
    if (std::abs(det) < 1e-7f) return false;
    const float inv = 1.0f / det;
    const glm::vec3 t = origin - triangle.a;
    const float u = glm::dot(t, p) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    const glm::vec3 q = glm::cross(t, e1);
    const float v = glm::dot(direction, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    const float distance = glm::dot(e2, q) * inv;
    return distance > 0.015f && distance < maxDistance;
}

struct BvhNode {
    glm::vec3 lo{0.0f}, hi{0.0f};
    std::uint32_t begin=0, count=0;
    int left=-1, right=-1;
};

class TriangleBvh {
public:
    explicit TriangleBvh(const std::vector<LightingTriangle>& source):triangles(source),order(source.size()) {
        std::iota(order.begin(),order.end(),0u); if(!order.empty())Build(0,static_cast<std::uint32_t>(order.size()));
    }
    bool Occluded(const glm::vec3& origin,const glm::vec3& direction,float maximum)const {
        if(nodes.empty())return false;std::array<int,64> stack{};int top=0;stack[top++]=0;
        while(top){const BvhNode& node=nodes[stack[--top]];if(!HitBox(origin,direction,node.lo,node.hi,maximum))continue;
            if(node.left<0){for(std::uint32_t i=0;i<node.count;++i)if(RayTriangle(origin,direction,triangles[order[node.begin+i]],maximum))return true;}
            else{if(top+2>static_cast<int>(stack.size()))continue;stack[top++]=node.left;stack[top++]=node.right;}}
        return false;
    }
private:
    const std::vector<LightingTriangle>& triangles;std::vector<std::uint32_t> order;std::vector<BvhNode> nodes;
    static glm::vec3 Centroid(const LightingTriangle& t){return(t.a+t.b+t.c)/3.0f;}
    static bool HitBox(const glm::vec3&o,const glm::vec3&d,const glm::vec3&lo,const glm::vec3&hi,float maximum){
        const glm::vec3 inv=1.0f/glm::vec3(std::abs(d.x)<1e-8f?std::copysign(1e-8f,d.x):d.x,std::abs(d.y)<1e-8f?std::copysign(1e-8f,d.y):d.y,std::abs(d.z)<1e-8f?std::copysign(1e-8f,d.z):d.z);
        const glm::vec3 a=(lo-o)*inv,b=(hi-o)*inv;const glm::vec3 near=glm::min(a,b),far=glm::max(a,b);
        return std::max({near.x,near.y,near.z,0.0f})<=std::min({far.x,far.y,far.z,maximum});
    }
    int Build(std::uint32_t begin,std::uint32_t count){
        const int index=static_cast<int>(nodes.size());nodes.emplace_back();BvhNode& node=nodes.back();node.begin=begin;node.count=count;
        node.lo=glm::vec3(std::numeric_limits<float>::max());node.hi=-node.lo;glm::vec3 clo=node.lo,chi=node.hi;
        for(std::uint32_t i=0;i<count;++i){const auto&t=triangles[order[begin+i]];node.lo=glm::min(node.lo,glm::min(t.a,glm::min(t.b,t.c)));node.hi=glm::max(node.hi,glm::max(t.a,glm::max(t.b,t.c)));const auto c=Centroid(t);clo=glm::min(clo,c);chi=glm::max(chi,c);}
        if(count<=8)return index;const glm::vec3 extent=chi-clo;int axis=extent.y>extent.x?1:0;if(extent.z>extent[axis])axis=2;
        const std::uint32_t middle=begin+count/2;std::nth_element(order.begin()+begin,order.begin()+middle,order.begin()+begin+count,[&](auto a,auto b){return Centroid(triangles[a])[axis]<Centroid(triangles[b])[axis];});
        const int left=Build(begin,middle-begin),right=Build(middle,begin+count-middle);nodes[index].left=left;nodes[index].right=right;nodes[index].count=0;return index;
    }
};

glm::vec3 HemisphereDirection(std::uint32_t i, std::uint32_t count) {
    constexpr float golden = 2.39996322972865332f;
    const float y = (static_cast<float>(i) + 0.5f) / static_cast<float>(count);
    const float radius = std::sqrt(std::max(0.0f, 1.0f - y * y));
    const float angle = golden * static_cast<float>(i);
    return glm::normalize(glm::vec3(std::cos(angle) * radius, y,
                                    std::sin(angle) * radius));
}

template <class T> bool Write(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value)); return !!out;
}
template <class T> bool Read(std::ifstream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(value)); return !!in;
}

} // namespace

bool LightingBuildData::IsValid() const {
    if (version != kVersion || dimensions.x <= 0 || dimensions.y <= 0 || dimensions.z <= 0)
        return false;
    const std::size_t expected = static_cast<std::size_t>(dimensions.x) * dimensions.y * dimensions.z;
    return expected == probes.size() && glm::all(glm::greaterThan(boundsMax, boundsMin));
}

std::size_t LightingBuildData::Index(int x, int y, int z) const {
    return static_cast<std::size_t>((z * dimensions.y + y) * dimensions.x + x);
}

std::uint64_t HashLightingGeometry(const std::vector<LightingTriangle>& triangles,
                                   const LightingBuildSettings& settings) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const auto& t : triangles) { HashBytes(hash, t.a); HashBytes(hash, t.b); HashBytes(hash, t.c); }
    HashBytes(hash, settings.quality); HashBytes(hash, settings.probeSpacing);
    HashBytes(hash, settings.maxRayDistance); HashBytes(hash, settings.boundsPadding);
    HashBytes(hash, settings.minimumVisibility); HashBytes(hash, settings.raysPerProbe);
    return hash;
}

bool BuildLightingProbes(const std::vector<LightingTriangle>& triangles,
                         const glm::vec3& skyColor, std::uint64_t sourceHash,
                         const std::string& sourceScene,
                         const LightingBuildSettings& input, LightingBuildData* output,
                         LightingBuildProgress* progress, std::string* error) {
    if (!output) { if (error) *error = "Lighting build output is null."; return false; }
    if (triangles.empty()) { if (error) *error = "Lighting build found no static triangles."; return false; }
    LightingBuildSettings settings = input;
    settings.probeSpacing = std::max(settings.probeSpacing, 0.25f);
    settings.maxRayDistance = std::max(settings.maxRayDistance, settings.probeSpacing);
    settings.raysPerProbe = std::clamp(settings.raysPerProbe, 8u, 1024u);
    if (settings.quality == LightingBuildQuality::Preview) settings.raysPerProbe = std::min(settings.raysPerProbe, 32u);
    if (settings.quality == LightingBuildQuality::High) settings.raysPerProbe = std::max(settings.raysPerProbe, 192u);

    glm::vec3 lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
    for (const auto& t : triangles) {
        lo = glm::min(lo, glm::min(t.a, glm::min(t.b, t.c)));
        hi = glm::max(hi, glm::max(t.a, glm::max(t.b, t.c)));
    }
    lo -= glm::vec3(settings.boundsPadding);
    hi += glm::vec3(settings.boundsPadding);
    hi.y = std::max(hi.y, lo.y + settings.probeSpacing * 2.0f);
    glm::ivec3 dimensions = glm::max(glm::ivec3(2), glm::ivec3(glm::ceil((hi - lo) / settings.probeSpacing)) + 1);
    constexpr std::size_t kMaxProbes = 262144;
    while (static_cast<std::size_t>(dimensions.x) * dimensions.y * dimensions.z > kMaxProbes) {
        settings.probeSpacing *= 1.25f;
        dimensions = glm::max(glm::ivec3(2), glm::ivec3(glm::ceil((hi - lo) / settings.probeSpacing)) + 1);
    }
    hi = lo + glm::vec3(dimensions - 1) * settings.probeSpacing;

    LightingBuildData built;
    built.sourceHash = sourceHash; built.sourceScene = sourceScene;
    built.boundsMin = lo; built.boundsMax = hi; built.dimensions = dimensions;
    built.spacing = settings.probeSpacing; built.settings = settings;
    const std::size_t count = static_cast<std::size_t>(dimensions.x) * dimensions.y * dimensions.z;
    built.probes.resize(count);
    const TriangleBvh acceleration(triangles);
    if (progress) { progress->completed = 0; progress->total = static_cast<std::uint32_t>(count); }

    for (int z = 0; z < dimensions.z; ++z) for (int y = 0; y < dimensions.y; ++y)
    for (int x = 0; x < dimensions.x; ++x) {
        if (progress && progress->cancel.load()) { if (error) *error = "Lighting build cancelled."; return false; }
        const glm::vec3 position = lo + glm::vec3(x, y, z) * settings.probeSpacing;
        std::uint32_t visible = 0;
        for (std::uint32_t ray = 0; ray < settings.raysPerProbe; ++ray) {
            const glm::vec3 direction = HemisphereDirection(ray, settings.raysPerProbe);
            const bool blocked = acceleration.Occluded(position, direction, settings.maxRayDistance);
            if (!blocked) ++visible;
        }
        LightingProbe& probe = built.probes[built.Index(x, y, z)];
        probe.skyVisibility = std::clamp(static_cast<float>(visible) / settings.raysPerProbe,
                                         settings.minimumVisibility, 1.0f);
        probe.ambient = glm::max(skyColor, glm::vec3(0.0f)) * probe.skyVisibility;
        probe.valid = true;
        if (progress) ++progress->completed;
    }
    *output = std::move(built);
    return true;
}

bool SaveLightingBuildData(const std::string& path, const LightingBuildData& data, std::string* error) {
    if (!data.IsValid()) { if (error) *error = "Cannot save invalid lighting build data."; return false; }
    std::error_code ec; std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    const std::string temporary = path + ".tmp";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) { if (error) *error = "Could not open lighting asset for writing: " + temporary; return false; }
    out.write(kMagic, sizeof(kMagic));
    const std::uint32_t sceneLength = static_cast<std::uint32_t>(data.sourceScene.size());
    const std::uint64_t probeCount = data.probes.size();
    Write(out, data.version); Write(out, data.sourceHash); Write(out, sceneLength);
    out.write(data.sourceScene.data(), sceneLength);
    Write(out, data.boundsMin); Write(out, data.boundsMax); Write(out, data.dimensions); Write(out, data.spacing);
    Write(out, data.settings.quality); Write(out, data.settings.probeSpacing); Write(out, data.settings.maxRayDistance);
    Write(out, data.settings.boundsPadding); Write(out, data.settings.minimumVisibility); Write(out, data.settings.raysPerProbe);
    Write(out, probeCount);
    for (const auto& p : data.probes) { Write(out, p.ambient); Write(out, p.skyVisibility); const std::uint8_t valid=p.valid?1:0; Write(out,valid); }
    out.flush(); out.close();
    if (!out) { std::filesystem::remove(temporary, ec); if (error) *error = "Failed while writing lighting asset."; return false; }
    const std::string backup=path+".bak";std::filesystem::remove(backup,ec);ec.clear();
    const bool hadPrevious=std::filesystem::exists(path,ec);ec.clear();
    if(hadPrevious){std::filesystem::rename(path,backup,ec);if(ec){std::filesystem::remove(temporary,ec);if(error)*error="Could not preserve the previous lighting asset.";return false;}}
    std::filesystem::rename(temporary,path,ec);
    if(ec){const std::string message=ec.message();ec.clear();if(hadPrevious)std::filesystem::rename(backup,path,ec);if(error)*error="Could not commit lighting asset: "+message;return false;}
    if(hadPrevious)std::filesystem::remove(backup,ec);
    return true;
}

bool LoadLightingBuildData(const std::string& path, LightingBuildData* data, std::string* error) {
    if (!data) { if (error) *error = "Lighting load output is null."; return false; }
    std::ifstream in(path, std::ios::binary); if (!in) { if (error) *error = "Could not open lighting asset: " + path; return false; }
    char magic[8]{}; in.read(magic, sizeof(magic));
    if (std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) { if (error) *error = "Unsupported lighting asset header."; return false; }
    LightingBuildData loaded; std::uint32_t sceneLength=0; std::uint64_t probeCount=0;
    if (!Read(in, loaded.version) || loaded.version != LightingBuildData::kVersion || !Read(in,loaded.sourceHash) || !Read(in,sceneLength) || sceneLength>1024*1024) { if(error)*error="Invalid lighting asset metadata."; return false; }
    loaded.sourceScene.resize(sceneLength); in.read(loaded.sourceScene.data(), sceneLength);
    if (!Read(in,loaded.boundsMin)||!Read(in,loaded.boundsMax)||!Read(in,loaded.dimensions)||!Read(in,loaded.spacing)
        ||!Read(in,loaded.settings.quality)||!Read(in,loaded.settings.probeSpacing)||!Read(in,loaded.settings.maxRayDistance)
        ||!Read(in,loaded.settings.boundsPadding)||!Read(in,loaded.settings.minimumVisibility)||!Read(in,loaded.settings.raysPerProbe)
        ||!Read(in,probeCount) || probeCount>262144) { if(error)*error="Truncated or unsafe lighting asset."; return false; }
    loaded.probes.resize(static_cast<std::size_t>(probeCount));
    for(auto& p:loaded.probes){std::uint8_t valid=0;if(!Read(in,p.ambient)||!Read(in,p.skyVisibility)||!Read(in,valid)){if(error)*error="Truncated lighting probes.";return false;}p.valid=valid!=0;}
    if(!loaded.IsValid()){if(error)*error="Lighting asset grid is inconsistent.";return false;}
    *data=std::move(loaded); return true;
}

LightingProbeGrid::~LightingProbeGrid(){ Reset(); }
LightingProbeGrid::LightingProbeGrid(LightingProbeGrid&& o) noexcept { *this=std::move(o); }
LightingProbeGrid& LightingProbeGrid::operator=(LightingProbeGrid&& o) noexcept { if(this!=&o){Reset();m_texture=o.m_texture;o.m_texture=0;m_boundsMin=o.m_boundsMin;m_boundsMax=o.m_boundsMax;}return *this; }
void LightingProbeGrid::Reset(){ if(m_texture){glDeleteTextures(1,&m_texture);m_texture=0;} }
bool LightingProbeGrid::Upload(const LightingBuildData& data,std::string* error){
    if(!data.IsValid()){if(error)*error="Cannot upload invalid lighting grid.";return false;}
    std::vector<float> rgba(data.probes.size()*4);
    for(std::size_t i=0;i<data.probes.size();++i){rgba[i*4]=data.probes[i].ambient.r;rgba[i*4+1]=data.probes[i].ambient.g;rgba[i*4+2]=data.probes[i].ambient.b;rgba[i*4+3]=data.probes[i].skyVisibility;}
    Reset(); glGenTextures(1,&m_texture); glBindTexture(GL_TEXTURE_3D,m_texture);
    glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);
    glTexImage3D(GL_TEXTURE_3D,0,GL_RGBA16F,data.dimensions.x,data.dimensions.y,data.dimensions.z,0,GL_RGBA,GL_FLOAT,rgba.data());
    const GLenum status=glGetError();glBindTexture(GL_TEXTURE_3D,0);if(status!=GL_NO_ERROR){Reset();if(error)*error="OpenGL could not create the lighting probe texture.";return false;}
    m_boundsMin=data.boundsMin;m_boundsMax=data.boundsMax;return true;
}
void LightingProbeGrid::Bind(unsigned int unit)const{glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_3D,m_texture);}

} // namespace engine
