#include "RoadGeneratorPanel.h"
#include "EditorPanels.h"
#include "EditorScene.h"

#include <engine/math/Spline.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>

namespace {
std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return value;
}
const EditorScene::Object* FindSpline(const EditorScene& scene, const std::string& name) {
    for (const auto& object : scene.Objects())
        if (object.isSpline && object.name == name && object.splinePoints.size() >= 2) return &object;
    return nullptr;
}
glm::quat Orientation(glm::vec3 tangent) {
    if (glm::dot(tangent, tangent) < 1.e-8f) tangent = {0,0,1};
    tangent = glm::normalize(tangent);
    return glm::angleAxis(std::atan2(tangent.x, tangent.z), glm::vec3(0,1,0))
        * glm::angleAxis(-std::asin(std::clamp(tangent.y, -1.f, 1.f)), glm::vec3(1,0,0));
}
}

void RoadGeneratorPanel::NewAsset() {
    m_assetId = engine::AssetHandle::Generate(); m_path.clear(); m_dirty = true;
    m_width=6; m_thickness=.18f; m_spacing=1.5f; m_lanes=2;
    m_shoulders=true; m_shoulderWidth=1; m_markings=true; m_curbs=false;
    m_sidewalks=false; m_barriers=false; m_endCaps=true;
}
void RoadGeneratorPanel::Preset(int preset) {
    NewAsset();
    if (preset==1) { std::snprintf(m_name.data(),m_name.size(),"%s","CountryRoad"); m_width=5; m_lanes=2; m_shoulderWidth=1.5f; }
    else if (preset==2) { std::snprintf(m_name.data(),m_name.size(),"%s","CityStreet"); m_width=7; m_lanes=2; m_curbs=true; m_sidewalks=true; }
    else if (preset==3) { std::snprintf(m_name.data(),m_name.size(),"%s","Highway"); m_width=14; m_lanes=4; m_barriers=true; m_shoulderWidth=2; }
}
void RoadGeneratorPanel::RefreshMaterials(const std::string& root) {
    m_assetRoot=root; m_materials.clear(); std::error_code ec;
    for(std::filesystem::recursive_directory_iterator it(root,std::filesystem::directory_options::skip_permission_denied,ec),end;it!=end;it.increment(ec)){
        if(ec||!it->is_regular_file(ec)||Lower(it->path().extension().string())!=".3dgmat")continue;
        m_materials.push_back({it->path().string(),it->path().stem().string()});
    }
    std::sort(m_materials.begin(),m_materials.end(),[](const auto&a,const auto&b){return Lower(a.name)<Lower(b.name);});
}
void RoadGeneratorPanel::MaterialCombo(const char* label,std::string& path){
    const std::string selected=path.empty()?"Default":std::filesystem::path(path).stem().string();
    if(!ImGui::BeginCombo(label,selected.c_str()))return;
    if(ImGui::Selectable("Default",path.empty())){path.clear();m_dirty=true;}
    for(const auto& asset:m_materials)if(ImGui::Selectable(asset.name.c_str(),asset.path==path)){path=asset.path;m_dirty=true;}
    ImGui::EndCombo();
}
const std::string& RoadGeneratorPanel::MaterialFor(Surface surface)const{
    switch(surface){case Surface::Road:return m_roadMaterial;case Surface::Shoulder:return m_shoulderMaterial;
    case Surface::Marking:return m_markingMaterial;case Surface::Curb:return m_curbMaterial;
    case Surface::Sidewalk:return m_sidewalkMaterial;case Surface::Barrier:return m_barrierMaterial;}
    return m_roadMaterial;
}
bool RoadGeneratorPanel::Save(const std::string& root,std::string* error){
    std::filesystem::path path=m_path.empty()?std::filesystem::path(root)/"GameAssets"/"Roads"/(std::string(m_name.data())+".3dgroad"):m_path;
    std::error_code ec;std::filesystem::create_directories(path.parent_path(),ec);std::ofstream out(path,std::ios::trunc);
    if(!out){if(error)*error="Could not save road asset.";return false;}if(!m_assetId.Valid())m_assetId=engine::AssetHandle::Generate();
    out<<"3DG_ROAD 1 "<<m_assetId.ToString()<<'\n'<<"name "<<std::quoted(std::string(m_name.data()))<<'\n';
    out<<"spline "<<std::quoted(m_splineName)<<'\n';
    out<<"shape "<<m_width<<' '<<m_thickness<<' '<<m_spacing<<' '<<m_lanes<<' '<<m_shoulderWidth<<' '<<m_markingWidth<<' '<<m_markingHeight<<' '<<m_curbWidth<<' '<<m_curbHeight<<' '<<m_sidewalkWidth<<' '<<m_sidewalkHeight<<' '<<m_barrierHeight<<' '<<m_terrainOffset<<'\n';
    out<<"flags "<<m_shoulders<<' '<<m_markings<<' '<<m_curbs<<' '<<m_sidewalks<<' '<<m_barriers<<' '<<m_endCaps<<' '<<m_conformTerrain<<' '<<m_colliders<<'\n';
    out<<"materials "<<std::quoted(m_roadMaterial)<<' '<<std::quoted(m_shoulderMaterial)<<' '<<std::quoted(m_markingMaterial)<<' '<<std::quoted(m_curbMaterial)<<' '<<std::quoted(m_sidewalkMaterial)<<' '<<std::quoted(m_barrierMaterial)<<'\n';
    if(!out.good()){if(error)*error="Failed while writing road asset.";return false;}m_path=path.string();m_dirty=false;return true;
}
bool RoadGeneratorPanel::Load(const std::string& path,std::string* error){
    std::ifstream in(path);std::string magic,id,key,name;int version=0;
    if(!(in>>magic>>version>>id)||magic!="3DG_ROAD"||version!=1||!engine::AssetHandle::Parse(id,&m_assetId)){if(error)*error="Invalid road asset.";return false;}
    in>>key>>std::quoted(name);std::snprintf(m_name.data(),m_name.size(),"%s",name.c_str());in>>key>>std::quoted(m_splineName);
    in>>key>>m_width>>m_thickness>>m_spacing>>m_lanes>>m_shoulderWidth>>m_markingWidth>>m_markingHeight>>m_curbWidth>>m_curbHeight>>m_sidewalkWidth>>m_sidewalkHeight>>m_barrierHeight>>m_terrainOffset;
    in>>key>>m_shoulders>>m_markings>>m_curbs>>m_sidewalks>>m_barriers>>m_endCaps>>m_conformTerrain>>m_colliders;
    in>>key>>std::quoted(m_roadMaterial)>>std::quoted(m_shoulderMaterial)>>std::quoted(m_markingMaterial)>>std::quoted(m_curbMaterial)>>std::quoted(m_sidewalkMaterial)>>std::quoted(m_barrierMaterial);
    if(!in){if(error)*error="Road asset data is incomplete.";return false;}m_path=path;m_dirty=false;return true;
}

std::vector<RoadGeneratorPanel::Part> RoadGeneratorPanel::GenerateParts(const std::vector<glm::vec3>& points,bool closed)const{
    std::vector<Part> result;if(points.size()<2)return result;engine::Spline spline(points,closed);const float length=spline.Length();if(length<.01f)return result;
    const int spans=std::clamp(static_cast<int>(std::ceil(length/std::max(m_spacing,.1f))),1,4096);
    auto add=[&](std::string suffix,const glm::vec3& center,const glm::vec3& scale,const glm::quat& rotation,Surface surface){if(glm::all(glm::greaterThan(scale,glm::vec3(.005f))))result.push_back({std::move(suffix),center,scale,rotation,surface});};
    for(int i=0;i<spans;++i){
        const float d0=length*i/spans,d1=length*(i+1)/spans;const glm::vec3 a=spline.PositionAtDistance(d0),b=spline.PositionAtDistance(d1);const glm::vec3 tangent=b-a;const float span=glm::length(tangent)*1.035f;const glm::quat rotation=Orientation(tangent);const glm::vec3 right=rotation*glm::vec3(1,0,0);const glm::vec3 mid=(a+b)*.5f;
        add("Surface_"+std::to_string(i+1),mid-glm::vec3(0,m_thickness*.5f,0),{m_width,m_thickness,span},rotation,Surface::Road);
        const float roadHalf=m_width*.5f;
        if(m_shoulders)for(int side:{-1,1})add("Shoulder_"+std::to_string(side)+"_"+std::to_string(i+1),mid+right*(side*(roadHalf+m_shoulderWidth*.5f))-glm::vec3(0,m_thickness*.5f,0),{m_shoulderWidth,m_thickness*.75f,span},rotation,Surface::Shoulder);
        if(m_markings&&m_lanes>1)for(int lane=1;lane<m_lanes;++lane){const float lateral=-roadHalf+m_width*lane/m_lanes;add("Marking_"+std::to_string(lane)+"_"+std::to_string(i+1),mid+right*lateral+glm::vec3(0,m_markingHeight*.5f,0),{m_markingWidth,m_markingHeight,span*.65f},rotation,Surface::Marking);}
        const float outside=roadHalf+(m_shoulders?m_shoulderWidth:0.f);
        if(m_curbs)for(int side:{-1,1})add("Curb_"+std::to_string(side)+"_"+std::to_string(i+1),mid+right*(side*(outside+m_curbWidth*.5f))+glm::vec3(0,m_curbHeight*.5f,0),{m_curbWidth,m_curbHeight,span},rotation,Surface::Curb);
        if(m_sidewalks)for(int side:{-1,1})add("Sidewalk_"+std::to_string(side)+"_"+std::to_string(i+1),mid+right*(side*(outside+m_curbWidth+m_sidewalkWidth*.5f))+glm::vec3(0,m_sidewalkHeight*.5f,0),{m_sidewalkWidth,m_sidewalkHeight,span},rotation,Surface::Sidewalk);
        if(m_barriers)for(int side:{-1,1})add("Barrier_"+std::to_string(side)+"_"+std::to_string(i+1),mid+right*(side*(outside+.12f))+glm::vec3(0,m_barrierHeight*.5f,0),{.14f,m_barrierHeight,span},rotation,Surface::Barrier);
    }
    if(m_endCaps&&!closed){for(int end=0;end<2;++end){const float d=end?length:0;const glm::vec3 p=spline.PositionAtDistance(d);const glm::quat r=Orientation(spline.TangentAtDistance(d));add("EndCap_"+std::to_string(end+1),p-glm::vec3(0,m_thickness*.5f,0),{m_width,m_thickness,.16f},r,Surface::Road);}}
    return result;
}

RoadGeneratorPanel::Result RoadGeneratorPanel::Draw(const EditorScene& scene,const std::string& root,bool* open){
    Result result;if(!m_assetId.Valid())NewAsset();if(m_assetRoot!=root)RefreshMaterials(root);
    if(!m_pendingOpen.empty()){std::string error;m_status=Load(m_pendingOpen,&error)?"Loaded "+m_pendingOpen:error;m_pendingOpen.clear();}
    if(!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::RoadGenerator),open)){ImGui::End();return result;}
    std::vector<const EditorScene::Object*> splines;for(const auto& object:scene.Objects())if(object.isSpline&&object.splinePoints.size()>=2)splines.push_back(&object);
    if(!FindSpline(scene,m_splineName)&&!splines.empty())m_splineName=splines.front()->name;
    if(ImGui::Button("New"))NewAsset();ImGui::SameLine();if(ImGui::Button("Save")){std::string error;if(Save(root,&error)){m_status="Saved "+m_path;result.assetsChanged=true;}else m_status=error;}ImGui::SameLine();if(ImGui::Button("Generate / Rebuild"))result.generate=true;ImGui::SameLine();if(ImGui::Button("Delete Generated"))result.remove=true;
    if(ImGui::InputText("Name",m_name.data(),m_name.size()))m_dirty=true;
    if(ImGui::BeginCombo("Spline",m_splineName.empty()?"No spline":m_splineName.c_str())){for(const auto* spline:splines)if(ImGui::Selectable(spline->name.c_str(),spline->name==m_splineName)){m_splineName=spline->name;m_dirty=true;}ImGui::EndCombo();}
    ImGui::TextUnformatted("Presets:");ImGui::SameLine();if(ImGui::SmallButton("Basic"))Preset(0);ImGui::SameLine();if(ImGui::SmallButton("Country"))Preset(1);ImGui::SameLine();if(ImGui::SmallButton("City"))Preset(2);ImGui::SameLine();if(ImGui::SmallButton("Highway"))Preset(3);
    ImGui::SeparatorText("Road Geometry");m_dirty|=ImGui::DragFloat("Width",&m_width,.1f,.5f,100.f,"%.2f m");m_dirty|=ImGui::DragFloat("Thickness",&m_thickness,.01f,.02f,5.f,"%.2f m");m_dirty|=ImGui::DragFloat("Curve Resolution",&m_spacing,.05f,.2f,10.f,"%.2f m");m_dirty|=ImGui::SliderInt("Lanes",&m_lanes,1,12);
    m_dirty|=ImGui::Checkbox("Shoulders",&m_shoulders);if(m_shoulders){ImGui::SameLine();m_dirty|=ImGui::DragFloat("Shoulder Width",&m_shoulderWidth,.05f,.1f,10.f,"%.2f m");}
    m_dirty|=ImGui::Checkbox("Lane Markings",&m_markings);if(m_markings){m_dirty|=ImGui::DragFloat("Marking Width",&m_markingWidth,.01f,.02f,1.f,"%.2f m");}
    m_dirty|=ImGui::Checkbox("Curbs",&m_curbs);if(m_curbs){ImGui::SameLine();m_dirty|=ImGui::DragFloat("Curb Height",&m_curbHeight,.01f,.02f,1.f,"%.2f m");}
    m_dirty|=ImGui::Checkbox("Sidewalks",&m_sidewalks);if(m_sidewalks){ImGui::SameLine();m_dirty|=ImGui::DragFloat("Sidewalk Width",&m_sidewalkWidth,.05f,.2f,10.f,"%.2f m");}
    m_dirty|=ImGui::Checkbox("Side Barriers",&m_barriers);if(m_barriers){ImGui::SameLine();m_dirty|=ImGui::DragFloat("Barrier Height",&m_barrierHeight,.05f,.1f,5.f,"%.2f m");}
    m_dirty|=ImGui::Checkbox("End Caps",&m_endCaps);ImGui::SameLine();m_dirty|=ImGui::Checkbox("Colliders",&m_colliders);ImGui::SameLine();ImGui::Checkbox("Replace Existing",&m_replace);
    m_dirty|=ImGui::Checkbox("Conform to Terrain",&m_conformTerrain);if(m_conformTerrain){ImGui::SameLine();m_dirty|=ImGui::DragFloat("Surface Offset",&m_terrainOffset,.01f,-2.f,2.f,"%.2f m");}
    ImGui::SeparatorText("Materials");MaterialCombo("Road",m_roadMaterial);MaterialCombo("Shoulder",m_shoulderMaterial);MaterialCombo("Markings",m_markingMaterial);MaterialCombo("Curbs",m_curbMaterial);MaterialCombo("Sidewalk",m_sidewalkMaterial);MaterialCombo("Barrier",m_barrierMaterial);
    const auto* spline=FindSpline(scene,m_splineName);if(spline){engine::Spline curve(spline->splinePoints,spline->splineClosed);ImGui::Text("Length %.1f m | %d generated pieces",curve.Length(),static_cast<int>(GenerateParts(spline->splinePoints,spline->splineClosed).size()));}else ImGui::TextDisabled("Create a spline, then select it here.");
    if(m_dirty)ImGui::TextColored({1,.7f,.2f,1},"Unsaved changes");if(!m_status.empty())ImGui::TextWrapped("%s",m_status.c_str());ImGui::End();return result;
}
