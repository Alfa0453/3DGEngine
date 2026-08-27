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

constexpr char kMagic[8] = {'3','D','G','L','I','T','E','2'};
constexpr float kPi = 3.14159265358979323846f;

std::array<float, 4> EvaluateSH4Basis(const glm::vec3& direction) {
    return {0.2820947918f, 0.4886025119f * direction.y,
            0.4886025119f * direction.z, 0.4886025119f * direction.x};
}

template <class T> void HashBytes(std::uint64_t& h, const T& value) {
    const auto* p = reinterpret_cast<const unsigned char*>(&value);
    for (std::size_t i = 0; i < sizeof(T); ++i) { h ^= p[i]; h *= 1099511628211ull; }
}

bool RayTriangle(const glm::vec3& origin, const glm::vec3& direction,
                 const LightingTriangle& triangle, float maxDistance,
                 float* hitDistance = nullptr, glm::vec3* barycentric = nullptr) {
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
    if (distance <= 0.015f || distance >= maxDistance) return false;
    if (hitDistance) *hitDistance = distance;
    if (barycentric) *barycentric = glm::vec3(1.0f - u - v, u, v);
    return true;
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
    bool Trace(const glm::vec3& origin,const glm::vec3& direction,float maximum,
               LightingRayHit* result) const {
        if(result)*result=LightingRayHit{};
        if(nodes.empty())return false;
        std::array<int,64> stack{};int top=0;stack[top++]=0;
        float nearest=maximum;std::uint32_t nearestIndex=0;glm::vec3 nearestBary(0.0f);bool found=false;
        while(top){const BvhNode& node=nodes[stack[--top]];if(!HitBox(origin,direction,node.lo,node.hi,nearest))continue;
            if(node.left<0){for(std::uint32_t i=0;i<node.count;++i){const std::uint32_t ti=order[node.begin+i];float distance=0.0f;glm::vec3 bary;
                if(RayTriangle(origin,direction,triangles[ti],nearest,&distance,&bary)){nearest=distance;nearestIndex=ti;nearestBary=bary;found=true;}}}
            else if(top+2<=static_cast<int>(stack.size())){stack[top++]=node.left;stack[top++]=node.right;}}
        if(found&&result){const auto&t=triangles[nearestIndex];result->hit=true;result->distance=nearest;
            result->position=origin+direction*nearest;result->normal=glm::normalize(glm::cross(t.b-t.a,t.c-t.a));
            if(glm::dot(result->normal,direction)>0.0f)result->normal=-result->normal;
            result->barycentric=nearestBary;result->albedo=t.albedo;result->emissive=t.emissive;result->triangleIndex=nearestIndex;}
        return found;
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

glm::vec3 SphereDirection(std::uint32_t i, std::uint32_t count) {
    constexpr float golden = 2.39996322972865332f;
    const float y = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / static_cast<float>(count);
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

glm::vec3 DirectionalSkyRadiance::Sample(const glm::vec3& input) const {
    const glm::vec3 direction = glm::normalize(input);
    const float above = glm::clamp(direction.y, 0.0f, 1.0f);
    glm::vec3 radiance = direction.y >= 0.0f
        ? glm::mix(glm::max(horizon, glm::vec3(0.0f)),
                   glm::max(zenith, glm::vec3(0.0f)), std::sqrt(above))
        : glm::max(ground, glm::vec3(0.0f));
    radiance += glm::max(sunRadiance, glm::vec3(0.0f))
        * std::pow(glm::clamp(glm::dot(direction, glm::normalize(sunDirection)), 0.0f, 1.0f),
                   glm::clamp(sunSharpness, 1.0f, 4096.0f));
    return radiance;
}

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
    for (const auto& t : triangles) { HashBytes(hash, t.a); HashBytes(hash, t.b); HashBytes(hash, t.c); HashBytes(hash,t.albedo); HashBytes(hash,t.emissive); }
    HashBytes(hash, settings.quality); HashBytes(hash, settings.probeSpacing);
    HashBytes(hash, settings.maxRayDistance); HashBytes(hash, settings.boundsPadding);
    HashBytes(hash, settings.minimumVisibility); HashBytes(hash, settings.raysPerProbe);
    HashBytes(hash, settings.directionalIrradiance); HashBytes(hash, settings.indirectBounceStrength);
    return hash;
}

bool BuildLightingProbes(const std::vector<LightingTriangle>& triangles,
                         const glm::vec3& skyColor, std::uint64_t sourceHash,
                         const std::string& sourceScene,
                         const LightingBuildSettings& input, LightingBuildData* output,
                         LightingBuildProgress* progress, std::string* error) {
    DirectionalSkyRadiance sky;
    const glm::vec3 safe = glm::max(skyColor, glm::vec3(0.0f));
    sky.zenith = safe;
    sky.horizon = safe * 0.75f;
    sky.ground = safe * 0.08f;
    return BuildLightingProbes(triangles, sky, sourceHash, sourceScene, input,
                               output, progress, error);
}

bool BuildLightingProbes(const std::vector<LightingTriangle>& triangles,
                         const DirectionalSkyRadiance& sky, std::uint64_t sourceHash,
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
        std::uint32_t visible = 0, skySamples = 0;
        std::array<glm::vec3, 4> projected{{glm::vec3(0.0f), glm::vec3(0.0f),
                                            glm::vec3(0.0f), glm::vec3(0.0f)}};
        std::uint32_t nearBlocked = 0;
        static constexpr glm::vec3 axes[6] = {
            {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (const glm::vec3& axis : axes)
            if (acceleration.Occluded(position, axis, settings.probeSpacing * 0.2f))
                ++nearBlocked;
        for (std::uint32_t ray = 0; ray < settings.raysPerProbe; ++ray) {
            const glm::vec3 direction = SphereDirection(ray, settings.raysPerProbe);
            if (direction.y > 0.0f) ++skySamples;
            LightingRayHit hit;
            const bool blocked = acceleration.Trace(position, direction, settings.maxRayDistance, &hit);
            if (!blocked) {
                if (direction.y > 0.0f) ++visible;
                const auto basis = EvaluateSH4Basis(direction);
                const glm::vec3 radiance = sky.Sample(direction);
                for (std::size_t coefficient = 0; coefficient < projected.size(); ++coefficient)
                    projected[coefficient] += radiance * basis[coefficient];
            } else if (settings.indirectBounceStrength > 0.0f) {
                const auto basis = EvaluateSH4Basis(direction);
                const glm::vec3 incident = sky.Sample(hit.normal);
                const glm::vec3 bounce = hit.emissive + hit.albedo * incident
                    * (glm::clamp(settings.indirectBounceStrength, 0.0f, 1.0f) / kPi);
                for (std::size_t coefficient = 0; coefficient < projected.size(); ++coefficient)
                    projected[coefficient] += bounce * basis[coefficient];
            }
        }
        LightingProbe& probe = built.probes[built.Index(x, y, z)];
        probe.skyVisibility = std::clamp(static_cast<float>(visible) / std::max(skySamples, 1u),
                                         settings.minimumVisibility, 1.0f);
        const float projectionWeight = 4.0f * kPi / static_cast<float>(settings.raysPerProbe);
        probe.irradianceSH[0] = projected[0] * projectionWeight * kPi;
        for (std::size_t coefficient = 1; coefficient < probe.irradianceSH.size(); ++coefficient)
            probe.irradianceSH[coefficient] = projected[coefficient] * projectionWeight * (2.0f * kPi / 3.0f);
        const auto up = EvaluateSH4Basis(glm::vec3(0, 1, 0));
        probe.ambient = glm::max(probe.irradianceSH[0] * up[0]
            + probe.irradianceSH[1] * up[1]
            + probe.irradianceSH[2] * up[2]
            + probe.irradianceSH[3] * up[3], glm::vec3(0.0f));
        probe.valid = nearBlocked < 6;
        if (!probe.valid) {
            probe.ambient = glm::vec3(0.0f);
            for (glm::vec3& coefficient : probe.irradianceSH)
                coefficient = glm::vec3(0.0f);
        }
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
    const std::uint8_t directional = data.settings.directionalIrradiance ? 1 : 0;
    Write(out, directional); Write(out, data.settings.indirectBounceStrength);
    Write(out, probeCount);
    for (const auto& p : data.probes) { Write(out, p.ambient); for(const auto& sh:p.irradianceSH)Write(out,sh); Write(out, p.skyVisibility); const std::uint8_t valid=p.valid?1:0; Write(out,valid); }
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
    if (std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) { if (error) *error = "Lighting build data is outdated and requires rebuild."; return false; }
    LightingBuildData loaded; std::uint32_t sceneLength=0; std::uint64_t probeCount=0;
    if (!Read(in, loaded.version) || loaded.version != LightingBuildData::kVersion || !Read(in,loaded.sourceHash) || !Read(in,sceneLength) || sceneLength>1024*1024) { if(error)*error="Lighting build data is outdated and requires rebuild."; return false; }
    loaded.sourceScene.resize(sceneLength); in.read(loaded.sourceScene.data(), sceneLength);
    if (!Read(in,loaded.boundsMin)||!Read(in,loaded.boundsMax)||!Read(in,loaded.dimensions)||!Read(in,loaded.spacing)
        ||!Read(in,loaded.settings.quality)||!Read(in,loaded.settings.probeSpacing)||!Read(in,loaded.settings.maxRayDistance)
        ||!Read(in,loaded.settings.boundsPadding)||!Read(in,loaded.settings.minimumVisibility)||!Read(in,loaded.settings.raysPerProbe)
        ) { if(error)*error="Truncated or unsafe lighting asset."; return false; }
    std::uint8_t directional=0;
    if(!Read(in,directional)||!Read(in,loaded.settings.indirectBounceStrength)
        ||!Read(in,probeCount) || probeCount>262144) { if(error)*error="Truncated or unsafe lighting asset."; return false; }
    loaded.settings.directionalIrradiance=directional!=0;
    loaded.probes.resize(static_cast<std::size_t>(probeCount));
    for(auto& p:loaded.probes){std::uint8_t valid=0;if(!Read(in,p.ambient)){if(error)*error="Truncated lighting probes.";return false;}for(auto& sh:p.irradianceSH)if(!Read(in,sh)){if(error)*error="Truncated lighting probes.";return false;}if(!Read(in,p.skyVisibility)||!Read(in,valid)){if(error)*error="Truncated lighting probes.";return false;}p.valid=valid!=0;}
    if(!loaded.IsValid()){if(error)*error="Lighting asset grid is inconsistent.";return false;}
    *data=std::move(loaded); return true;
}

LightingProbeGrid::~LightingProbeGrid(){ Reset(); }
LightingProbeGrid::LightingProbeGrid(LightingProbeGrid&& o) noexcept { *this=std::move(o); }
LightingProbeGrid& LightingProbeGrid::operator=(LightingProbeGrid&& o) noexcept { if(this!=&o){Reset();m_textures=o.m_textures;o.m_textures={0,0,0,0};m_boundsMin=o.m_boundsMin;m_boundsMax=o.m_boundsMax;}return *this; }
void LightingProbeGrid::Reset(){ glDeleteTextures(static_cast<GLsizei>(m_textures.size()),m_textures.data());m_textures={0,0,0,0}; }
bool LightingProbeGrid::Upload(const LightingBuildData& data,std::string* error){
    if(!data.IsValid()){if(error)*error="Cannot upload invalid lighting grid.";return false;}
    std::array<std::vector<float>,4> packed;for(auto& texture:packed)texture.resize(data.probes.size()*4,0.0f);
    for(std::size_t i=0;i<data.probes.size();++i){const auto&p=data.probes[i];const float v=p.valid?1.0f:0.0f;const std::size_t b=i*4;
        packed[0][b]=p.irradianceSH[0].r*v;packed[0][b+1]=p.irradianceSH[0].g*v;packed[0][b+2]=p.irradianceSH[0].b*v;packed[0][b+3]=p.irradianceSH[1].r*v;
        packed[1][b]=p.irradianceSH[1].g*v;packed[1][b+1]=p.irradianceSH[1].b*v;packed[1][b+2]=p.irradianceSH[2].r*v;packed[1][b+3]=p.irradianceSH[2].g*v;
        packed[2][b]=p.irradianceSH[2].b*v;packed[2][b+1]=p.irradianceSH[3].r*v;packed[2][b+2]=p.irradianceSH[3].g*v;packed[2][b+3]=p.irradianceSH[3].b*v;
        packed[3][b]=p.skyVisibility*v;packed[3][b+1]=v;}
    Reset();glGenTextures(static_cast<GLsizei>(m_textures.size()),m_textures.data());
    for(std::size_t index=0;index<m_textures.size();++index){glBindTexture(GL_TEXTURE_3D,m_textures[index]);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);glTexImage3D(GL_TEXTURE_3D,0,GL_RGBA16F,data.dimensions.x,data.dimensions.y,data.dimensions.z,0,GL_RGBA,GL_FLOAT,packed[index].data());}
    const GLenum status=glGetError();glBindTexture(GL_TEXTURE_3D,0);if(status!=GL_NO_ERROR){Reset();if(error)*error="OpenGL could not create the SH lighting probe textures.";return false;}
    m_boundsMin=data.boundsMin;m_boundsMax=data.boundsMax;return true;
}
void LightingProbeGrid::Bind(unsigned int unit)const{for(std::size_t i=0;i<m_textures.size();++i){glActiveTexture(GL_TEXTURE0+unit+static_cast<unsigned int>(i));glBindTexture(GL_TEXTURE_3D,m_textures[i]);}}

} // namespace engine
