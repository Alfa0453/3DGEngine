#include "engine/assets/PoseLibraryAsset.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <unordered_map>
#include <unordered_set>

namespace engine {
namespace {
void SetError(std::string* error, const std::string& text) { if (error) *error = text; }
bool Finite(float v) { return std::isfinite(v); }
bool Valid(const BoneLocal& v) {
    return Finite(v.pos.x)&&Finite(v.pos.y)&&Finite(v.pos.z)&&Finite(v.rot.w)&&Finite(v.rot.x)
        &&Finite(v.rot.y)&&Finite(v.rot.z)&&Finite(v.scale.x)&&Finite(v.scale.y)&&Finite(v.scale.z);
}
BoneLocal BindLocal(const glm::mat4& m) {
    BoneLocal out; out.pos = glm::vec3(m[3]);
    out.scale = {glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2]))};
    glm::mat3 r(m);
    for (int i=0;i<3;++i) if (out.scale[i] > 0.000001f) r[i] /= out.scale[i];
    out.rot = glm::normalize(glm::quat_cast(r)); return out;
}
std::string SwapSide(std::string value) {
    const std::pair<const char*,const char*> pairs[]={{"Left","Right"},{"left","right"},{"LEFT","RIGHT"},
        {"_L","_R"},{"_l","_r"},{".L",".R"},{".l",".r"},{"-L","-R"},{"-l","-r"}};
    for (const auto& [a,b] : pairs) {
        if (const auto p=value.find(a); p!=std::string::npos) { value.replace(p,std::char_traits<char>::length(a),b); return value; }
        if (const auto p=value.find(b); p!=std::string::npos) { value.replace(p,std::char_traits<char>::length(b),a); return value; }
    }
    return value;
}
}

void NormalizePoseLibraryAsset(PoseLibraryAssetData& asset) {
    if (!asset.header.id.Valid()) asset.header.id = AssetHandle::Generate();
    if (asset.name.empty()) asset.name = "NewPoseLibrary";
    asset.header.dependencies.clear();
    if (asset.skeletonId.Valid()) asset.header.dependencies.push_back(asset.skeletonId);
    for (auto& pose : asset.poses) {
        if (pose.name.empty()) pose.name = "NewPose";
        std::sort(pose.tags.begin(), pose.tags.end());
        pose.tags.erase(std::unique(pose.tags.begin(), pose.tags.end()), pose.tags.end());
        std::unordered_set<std::string> seen;
        pose.bones.erase(std::remove_if(pose.bones.begin(), pose.bones.end(), [&](const PoseLibraryBone& bone) {
            return bone.name.empty() || !seen.insert(bone.name).second;
        }), pose.bones.end());
        for (auto& bone : pose.bones) {
            if (glm::length2(bone.local.rot) < 0.000001f) bone.local.rot = glm::quat(1,0,0,0);
            else bone.local.rot = glm::normalize(bone.local.rot);
            bone.local.scale = glm::max(bone.local.scale, glm::vec3(0.0001f));
        }
    }
}

bool ValidatePoseLibraryAsset(const PoseLibraryAssetData& asset, std::string* error) {
    if (!asset.header.id.Valid()) { SetError(error,"Pose library has no stable asset ID."); return false; }
    std::unordered_set<std::string> names;
    for (const auto& pose : asset.poses) {
        if (pose.name.empty() || !names.insert(pose.name).second) { SetError(error,"Pose names must be non-empty and unique."); return false; }
        for (const auto& bone : pose.bones) if (bone.name.empty() || !Valid(bone.local)) {
            SetError(error,"Pose contains an invalid bone transform."); return false;
        }
    }
    return true;
}

bool SavePoseLibraryAsset(const std::string& path, PoseLibraryAssetData asset, std::string* error) {
    NormalizePoseLibraryAsset(asset); if (!ValidatePoseLibraryAsset(asset,error)) return false;
    std::error_code ec; const auto parent=std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent,ec);
    std::ofstream out(path,std::ios::trunc); if(!out){SetError(error,"Could not create pose library: "+path);return false;}
    out<<"3DG_POSE_LIBRARY "<<kPoseLibraryAssetVersion<<' '<<asset.header.id.ToString()<<'\n';
    out<<"NAME "<<std::quoted(asset.name)<<'\n'<<"SKELETON "<<asset.skeletonId.ToString()<<' '<<std::quoted(asset.skeletonPath)<<'\n';
    out<<"PREVIEW_MODEL "<<std::quoted(asset.previewModelPath)<<'\n'<<"POSES "<<asset.poses.size()<<'\n';
    out<<std::setprecision(9);
    for(const auto& pose:asset.poses){out<<"POSE "<<std::quoted(pose.name)<<' '<<pose.tags.size()<<' '<<pose.bones.size()<<'\n';
        for(const auto& tag:pose.tags)out<<"TAG "<<std::quoted(tag)<<'\n';
        for(const auto& bone:pose.bones){const auto& t=bone.local;out<<"BONE "<<std::quoted(bone.name)<<' '
            <<t.pos.x<<' '<<t.pos.y<<' '<<t.pos.z<<' '<<t.rot.w<<' '<<t.rot.x<<' '<<t.rot.y<<' '<<t.rot.z<<' '
            <<t.scale.x<<' '<<t.scale.y<<' '<<t.scale.z<<'\n';}}
    if(!out){SetError(error,"Failed while writing pose library: "+path);return false;} return true;
}

bool LoadPoseLibraryAsset(const std::string& path, PoseLibraryAssetData* output, std::string* error) {
    if(!output){SetError(error,"Pose library output is null.");return false;} std::ifstream in(path);
    PoseLibraryAssetData asset; std::string magic,id,token;std::uint32_t version=0;
    if(!(in>>magic>>version>>id)||magic!="3DG_POSE_LIBRARY"||version!=kPoseLibraryAssetVersion||!AssetHandle::Parse(id,&asset.header.id)){
        SetError(error,"Invalid or unsupported pose library: "+path);return false;}
    std::size_t count=0;if(!(in>>token)||token!="NAME"||!(in>>std::quoted(asset.name))||!(in>>token)||token!="SKELETON"||!(in>>id>>std::quoted(asset.skeletonPath))){SetError(error,"Pose library header is malformed.");return false;}
    AssetHandle::Parse(id,&asset.skeletonId);
    if(!(in>>token)||token!="PREVIEW_MODEL"||!(in>>std::quoted(asset.previewModelPath))||!(in>>token>>count)||token!="POSES"||count>100000){SetError(error,"Pose library payload is malformed.");return false;}
    asset.poses.resize(count);
    for(auto& pose:asset.poses){std::size_t tags=0,bones=0;if(!(in>>token>>std::quoted(pose.name)>>tags>>bones)||token!="POSE"||tags>4096||bones>65536){SetError(error,"Pose record is malformed.");return false;}
        pose.tags.resize(tags);for(auto& tag:pose.tags)if(!(in>>token>>std::quoted(tag))||token!="TAG"){SetError(error,"Pose tag is malformed.");return false;}
        pose.bones.resize(bones);for(auto& bone:pose.bones){auto& t=bone.local;if(!(in>>token>>std::quoted(bone.name)>>t.pos.x>>t.pos.y>>t.pos.z>>t.rot.w>>t.rot.x>>t.rot.y>>t.rot.z>>t.scale.x>>t.scale.y>>t.scale.z)||token!="BONE"){SetError(error,"Pose bone is malformed.");return false;}}}
    asset.header.type=AssetType::PoseLibrary;asset.header.assetVersion=version;NormalizePoseLibraryAsset(asset);
    if(!ValidatePoseLibraryAsset(asset,error))return false;*output=std::move(asset);return true;
}

const PoseLibraryPose* FindPose(const PoseLibraryAssetData& asset,const std::string& name){
    const auto it=std::find_if(asset.poses.begin(),asset.poses.end(),[&](const auto& p){return p.name==name;});return it==asset.poses.end()?nullptr:&*it;
}
PoseLibraryPose MirrorPose(const PoseLibraryPose& source,const std::string& name){PoseLibraryPose out=source;out.name=name.empty()?source.name+"_Mirrored":name;
    for(auto& bone:out.bones){bone.name=SwapSide(bone.name);bone.local.pos.x=-bone.local.pos.x;bone.local.rot=glm::normalize(glm::quat(bone.local.rot.w,bone.local.rot.x,-bone.local.rot.y,-bone.local.rot.z));}return out;}
PoseLibraryPose BlendPoses(const PoseLibraryPose& a,const PoseLibraryPose& b,float weight,const std::string& name){PoseLibraryPose out=a;out.name=name.empty()?a.name+"_Blend_"+b.name:name;weight=glm::clamp(weight,0.f,1.f);
    std::unordered_map<std::string,BoneLocal> rhs;for(const auto& bone:b.bones)rhs[bone.name]=bone.local;
    for(auto& bone:out.bones)if(const auto it=rhs.find(bone.name);it!=rhs.end()){bone.local.pos=glm::mix(bone.local.pos,it->second.pos,weight);bone.local.rot=glm::normalize(glm::slerp(bone.local.rot,it->second.rot,weight));bone.local.scale=glm::mix(bone.local.scale,it->second.scale,weight);rhs.erase(it);}
    if(weight>=0.5f)for(const auto& [bone,local]:rhs)out.bones.push_back({bone,local});return out;}
void ResolvePoseForSkeleton(const PoseLibraryPose& pose,const Skeleton& skeleton,std::vector<BoneLocal>& output){output.resize(skeleton.bones.size());std::unordered_map<std::string,const BoneLocal*> authored;for(const auto& b:pose.bones)authored[b.name]=&b.local;
    for(std::size_t i=0;i<skeleton.bones.size();++i){const auto it=authored.find(skeleton.bones[i].name);output[i]=it==authored.end()?BindLocal(skeleton.bones[i].localBind):*it->second;}}
} // namespace engine
