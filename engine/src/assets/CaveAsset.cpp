#include "engine/assets/CaveAsset.h"
#include "engine/math/Spline.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <filesystem>
#include <fstream>
#include <iomanip>

namespace engine { namespace {
void Error(std::string* e,const std::string&t){if(e)*e=t;}
void Dep(std::vector<AssetHandle>&d,AssetHandle id){if(id.Valid()&&std::find(d.begin(),d.end(),id)==d.end())d.push_back(id);}
void Vertex(StaticMeshSubMeshData& out,const glm::vec3&p,const glm::vec3&n,float u,float v){
    glm::vec3 tangent=glm::cross(glm::vec3(0,1,0),n);if(glm::dot(tangent,tangent)<.001f)tangent={1,0,0};else tangent=glm::normalize(tangent);
    const float values[]={p.x,p.y,p.z,n.x,n.y,n.z,u,v,tangent.x,tangent.y,tangent.z};out.vertices.insert(out.vertices.end(),std::begin(values),std::end(values));
}
}
void NormalizeCaveAsset(CaveAssetData&c){c.width=std::clamp(c.width,.5f,100.f);c.height=std::clamp(c.height,.5f,100.f);c.wallThickness=std::clamp(c.wallThickness,.01f,10.f);c.sampleSpacing=std::clamp(c.sampleSpacing,.1f,20.f);c.radialSegments=std::clamp(c.radialSegments,6,64);for(auto&x:c.chambers){x.pointIndex=std::clamp(x.pointIndex,0,std::max(0,(int)c.points.size()-1));x.radiusScale=std::clamp(x.radiusScale,1.f,8.f);x.lengthScale=std::clamp(x.lengthScale,.25f,8.f);}}
bool ValidateCaveAsset(const CaveAssetData&c,std::string*e){if(c.name.empty()){Error(e,"Cave name is empty.");return false;}if(c.points.size()<2){Error(e,"Cave requires at least two spline points.");return false;}for(auto&p:c.points)if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)){Error(e,"Cave contains a non-finite point.");return false;}Error(e,{});return true;}
bool BuildCaveStaticMesh(const CaveAssetData&source,StaticMeshAssetData*out,CaveGenerationStats*stats,std::string*error){
    if(!out){Error(error,"Cave mesh output is null.");return false;}CaveAssetData c=source;NormalizeCaveAsset(c);if(!ValidateCaveAsset(c,error))return false;
    Spline spline(c.points,c.closed);const float length=spline.Length();if(length<.01f){Error(error,"Cave spline has no length.");return false;}
    const int spans=std::clamp((int)std::ceil(length/c.sampleSpacing),1,8192);const int ringCount=c.closed?spans:spans+1;StaticMeshAssetData mesh;mesh.header.type=AssetType::StaticMesh;mesh.header.id=AssetHandle::Generate();StaticMeshMaterialData mat;mat.name="CaveInterior";mat.diffuse={.32f,.28f,.24f};mesh.materials.push_back(mat);mesh.subMeshes.resize(1);auto&sub=mesh.subMeshes[0];sub.material=0;
    glm::vec3 minimum(1e30f),maximum(-1e30f);for(int r=0;r<ringCount;++r){const float d=length*(float)r/spans;const glm::vec3 center=spline.PositionAtDistance(d);glm::vec3 forward=glm::normalize(spline.TangentAtDistance(d));glm::vec3 right=glm::cross(glm::vec3(0,1,0),forward);if(glm::dot(right,right)<.001f)right={1,0,0};else right=glm::normalize(right);glm::vec3 up=glm::normalize(glm::cross(forward,right));float scale=1;const float normalized=d/length;for(auto&ch:c.chambers){const float location=(float)ch.pointIndex/std::max(1,(int)c.points.size()-1);const float radius=.5f*ch.lengthScale/std::max(1,(int)c.points.size()-1);const float q=std::abs(normalized-location)/std::max(radius,.001f);if(q<1)scale=std::max(scale,1+(ch.radiusScale-1)*(1-q*q*(3-2*q)));}
        for(int s=0;s<c.radialSegments;++s){const float a=6.283185307f*s/c.radialSegments;const glm::vec3 radial=right*(std::cos(a)*c.width*.5f*scale)+up*(std::sin(a)*c.height*.5f*scale);const glm::vec3 p=center+radial;Vertex(sub,p,-glm::normalize(radial),(float)s/c.radialSegments,d/std::max(c.width,1.f));minimum=glm::min(minimum,p);maximum=glm::max(maximum,p);}}
    for(int r=0;r<spans;++r){const int next=(r+1)%ringCount;for(int s=0;s<c.radialSegments;++s){const int sn=(s+1)%c.radialSegments;const std::uint32_t a=r*c.radialSegments+s,b=r*c.radialSegments+sn,cc=next*c.radialSegments+s,d=next*c.radialSegments+sn;sub.indices.insert(sub.indices.end(),{a,cc,b,b,cc,d});}}
    if(c.endCaps&&!c.closed){
        const glm::vec3 first=spline.PositionAtDistance(0),last=spline.PositionAtDistance(length);
        const std::uint32_t firstCenter=static_cast<std::uint32_t>(sub.vertices.size()/kStaticMeshVertexStride);
        Vertex(sub,first,spline.TangentAtDistance(0),.5f,.5f);
        const std::uint32_t lastCenter=static_cast<std::uint32_t>(sub.vertices.size()/kStaticMeshVertexStride);
        Vertex(sub,last,-spline.TangentAtDistance(length),.5f,.5f);
        const std::uint32_t lastRing=static_cast<std::uint32_t>((ringCount-1)*c.radialSegments);
        for(int s=0;s<c.radialSegments;++s){const std::uint32_t n=(s+1)%c.radialSegments;sub.indices.insert(sub.indices.end(),{firstCenter,(std::uint32_t)s,n,lastCenter,lastRing+n,lastRing+(std::uint32_t)s});}
    }
    mesh.minimum={minimum.x,minimum.y,minimum.z};mesh.maximum={maximum.x,maximum.y,maximum.z};*out=std::move(mesh);if(stats){stats->vertices=(std::size_t)ringCount*c.radialSegments+(c.endCaps&&!c.closed?2:0);stats->triangles=(std::size_t)spans*c.radialSegments*2+(c.endCaps&&!c.closed?(std::size_t)c.radialSegments*2:0);stats->rings=ringCount;stats->length=length;}Error(error,{});return true;
}
bool SaveCaveAsset(const std::string&path,CaveAssetData c,std::string*e){NormalizeCaveAsset(c);if(!ValidateCaveAsset(c,e))return false;c.header.type=AssetType::Cave;c.header.assetVersion=kCaveAssetVersion;if(!c.header.id.Valid())c.header.id=AssetHandle::Generate();std::filesystem::create_directories(std::filesystem::path(path).parent_path());std::ofstream o(path,std::ios::trunc);if(!o){Error(e,"Could not save cave asset.");return false;}o<<"3DGCave 1\n"<<c.header.id.ToString()<<' '<<std::quoted(c.name)<<' '<<c.points.size()<<' '<<c.closed<<'\n'<<c.width<<' '<<c.height<<' '<<c.wallThickness<<' '<<c.sampleSpacing<<' '<<c.radialSegments<<' '<<c.endCaps<<' '<<c.createCollision<<' '<<c.createNavigation<<' '<<c.terrainEntrances<<'\n';for(auto&p:c.points)o<<p.x<<' '<<p.y<<' '<<p.z<<'\n';o<<c.chambers.size()<<'\n';for(auto&x:c.chambers)o<<x.pointIndex<<' '<<x.radiusScale<<' '<<x.lengthScale<<'\n';auto ref=[&](const std::string&p,AssetHandle id){o<<std::quoted(p)<<' '<<(id.Valid()?id.ToString():"-")<<' ';};ref(c.wallMaterialPath,c.wallMaterialId);ref(c.floorMaterialPath,c.floorMaterialId);ref(c.ceilingMaterialPath,c.ceilingMaterialId);ref(c.trimMaterialPath,c.trimMaterialId);ref(c.bakedMeshPath,c.bakedMeshId);o<<'\n';std::vector<AssetHandle>d;Dep(d,c.wallMaterialId);Dep(d,c.floorMaterialId);Dep(d,c.ceilingMaterialId);Dep(d,c.trimMaterialId);Dep(d,c.bakedMeshId);o<<"ASSET_DEPS "<<d.size();for(auto id:d)o<<' '<<id.ToString();o<<'\n';Error(e,{});return(bool)o;}
bool LoadCaveAsset(const std::string&path,CaveAssetData*out,std::string*e){if(!out){Error(e,"Cave output is null.");return false;}std::ifstream in(path);std::string magic,id;int version=0;std::size_t count=0;CaveAssetData c;in>>magic>>version>>id>>std::quoted(c.name)>>count>>c.closed>>c.width>>c.height>>c.wallThickness>>c.sampleSpacing>>c.radialSegments>>c.endCaps>>c.createCollision>>c.createNavigation>>c.terrainEntrances;if(!in||magic!="3DGCave"||version!=1||!AssetHandle::Parse(id,&c.header.id)){Error(e,"Invalid cave asset: "+path);return false;}c.points.resize(count);for(auto&p:c.points)in>>p.x>>p.y>>p.z;std::size_t chambers=0;in>>chambers;c.chambers.resize(chambers);for(auto&x:c.chambers)in>>x.pointIndex>>x.radiusScale>>x.lengthScale;auto ref=[&](std::string&p,AssetHandle&h){std::string value;in>>std::quoted(p)>>value;if(value!="-"&&!AssetHandle::Parse(value,&h))in.setstate(std::ios::failbit);Dep(c.header.dependencies,h);};ref(c.wallMaterialPath,c.wallMaterialId);ref(c.floorMaterialPath,c.floorMaterialId);ref(c.ceilingMaterialPath,c.ceilingMaterialId);ref(c.trimMaterialPath,c.trimMaterialId);ref(c.bakedMeshPath,c.bakedMeshId);if(!in){Error(e,"Incomplete cave asset: "+path);return false;}c.header.type=AssetType::Cave;c.header.assetVersion=version;NormalizeCaveAsset(c);if(!ValidateCaveAsset(c,e))return false;*out=std::move(c);Error(e,{});return true;}
}
