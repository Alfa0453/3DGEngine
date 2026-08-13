#include "ProceduralScatterGraphPanel.h"
#include "EditorPanels.h"

#include <engine/assets/StaticMeshAsset.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace {
std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c){return static_cast<char>(std::tolower(c));});
    return value;
}
ImU32 NodeColor(engine::ScatterNodeType type) {
    switch(type){
    case engine::ScatterNodeType::Region:return IM_COL32(58,105,160,255);
    case engine::ScatterNodeType::Density:return IM_COL32(115,78,155,255);
    case engine::ScatterNodeType::HeightFilter:
    case engine::ScatterNodeType::SlopeFilter:return IM_COL32(170,108,42,255);
    case engine::ScatterNodeType::Transform:return IM_COL32(65,135,112,255);
    case engine::ScatterNodeType::ExclusionCircle:return IM_COL32(165,67,70,255);
    case engine::ScatterNodeType::MeshOutput:return IM_COL32(70,145,72,255);
    }
    return IM_COL32(80,80,80,255);
}
}

void ProceduralScatterGraphPanel::NewGraph(const std::string& root) {
    m_graph={};m_graph.name="NewScatterGraph";m_graph.seed=1337;m_graph.maximumInstances=10000;
    m_graph.regionMinimum={-10,-100000,-10};m_graph.regionMaximum={10,100000,10};
    m_nextNodeId=1;m_graph.nodes.clear();
    AddNode(engine::ScatterNodeType::Region);AddNode(engine::ScatterNodeType::Density);
    AddNode(engine::ScatterNodeType::Transform);AddNode(engine::ScatterNodeType::MeshOutput);
    for(std::size_t i=1;i<m_graph.nodes.size();++i)m_graph.nodes[i].input=m_graph.nodes[i-1].id;
    if(!m_meshes.empty()){m_graph.nodes.back().meshPath=m_meshes[0].path;m_graph.nodes.back().meshId=m_meshes[0].id;}
    m_path=(std::filesystem::path(root)/"GameAssets"/"Scatter"/"NewScatterGraph.3dgscatter").string();
    std::snprintf(m_name.data(),m_name.size(),"%s",m_graph.name.c_str());
    std::snprintf(m_pathBuffer.data(),m_pathBuffer.size(),"%s",m_path.c_str());
    m_selectedNode=m_graph.nodes.back().id;m_dirty=true;RefreshPreview();
}

void ProceduralScatterGraphPanel::RefreshMeshes(const std::string& root) {
    m_meshes.clear();m_scannedRoot=root;std::error_code ec;
    if(!std::filesystem::is_directory(root,ec))return;
    for(std::filesystem::recursive_directory_iterator it(root,std::filesystem::directory_options::skip_permission_denied,ec),end;it!=end;it.increment(ec)){
        if(ec||!it->is_regular_file(ec)||Lower(it->path().extension().string())!=".3dgmesh")continue;
        MeshChoice choice;choice.path=it->path().string();choice.name=it->path().stem().string();
        choice.relative=std::filesystem::relative(it->path(),root,ec).generic_string();if(ec){ec.clear();choice.relative=it->path().filename().string();}
        engine::NativeAssetHeader header;std::string ignored;if(engine::ReadNativeAssetHeaderFile(choice.path,&header,&ignored))choice.id=header.id;
        m_meshes.push_back(std::move(choice));
    }
    std::sort(m_meshes.begin(),m_meshes.end(),[](const auto&a,const auto&b){return Lower(a.relative)<Lower(b.relative);});
}

bool ProceduralScatterGraphPanel::Load(const std::string& path,std::string* error){
    engine::ScatterGraphAssetData graph;if(!engine::LoadScatterGraphAsset(path,&graph,error))return false;
    m_graph=std::move(graph);m_path=path;m_nextNodeId=1;for(const auto&n:m_graph.nodes)m_nextNodeId=std::max(m_nextNodeId,n.id+1);
    m_selectedNode=m_graph.nodes.empty()?0:m_graph.nodes.front().id;m_dirty=false;
    std::snprintf(m_name.data(),m_name.size(),"%s",m_graph.name.c_str());std::snprintf(m_pathBuffer.data(),m_pathBuffer.size(),"%s",m_path.c_str());RefreshPreview();return true;
}
bool ProceduralScatterGraphPanel::Save(std::string* error){
    m_graph.name=m_name.data();m_path=m_pathBuffer.data();if(m_path.empty()){if(error)*error="Scatter graph path is empty.";return false;}
    if(std::filesystem::path(m_path).extension()!=".3dgscatter")m_path+=".3dgscatter";
    if(!engine::SaveScatterGraphAsset(m_path,m_graph,error))return false;
    std::snprintf(m_pathBuffer.data(),m_pathBuffer.size(),"%s",m_path.c_str());m_dirty=false;return true;
}

void ProceduralScatterGraphPanel::AddNode(engine::ScatterNodeType type){
    engine::ScatterGraphNode node;node.id=m_nextNodeId++;node.type=type;node.name=engine::ScatterNodeTypeName(type);
    node.editorPosition={40.0f+static_cast<float>(m_graph.nodes.size()%3)*210.0f,40.0f+static_cast<float>(m_graph.nodes.size()/3)*120.0f};
    if(type==engine::ScatterNodeType::HeightFilter){node.minimum=-100000;node.maximum=100000;}
    if(type==engine::ScatterNodeType::SlopeFilter){node.minimum=0;node.maximum=50;}
    if(type==engine::ScatterNodeType::MeshOutput&&!m_meshes.empty()){node.meshPath=m_meshes[0].path;node.meshId=m_meshes[0].id;}
    if(!m_graph.nodes.empty())node.input=m_graph.nodes.back().id;m_graph.nodes.push_back(std::move(node));m_selectedNode=m_graph.nodes.back().id;m_dirty=true;
}
engine::ScatterGraphNode* ProceduralScatterGraphPanel::SelectedNode(){for(auto&n:m_graph.nodes)if(n.id==m_selectedNode)return&n;return nullptr;}
void ProceduralScatterGraphPanel::RefreshPreview(){m_preview=engine::EvaluateScatterGraph(m_graph,[](float x,float z){engine::ScatterSurfaceSample s;s.height=std::sin(x*.17f)*.6f+std::cos(z*.13f)*.4f;s.normal=glm::normalize(glm::vec3(-.102f*std::cos(x*.17f),1,.052f*std::sin(z*.13f)));return s;});}

void ProceduralScatterGraphPanel::DrawCanvas(){
    const ImVec2 origin=ImGui::GetCursorScreenPos(),size=ImGui::GetContentRegionAvail();ImGui::InvisibleButton("##ScatterGraphCanvas",size,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonMiddle);
    ImDrawList*draw=ImGui::GetWindowDrawList();draw->AddRectFilled(origin,{origin.x+size.x,origin.y+size.y},IM_COL32(20,23,29,255));
    if(ImGui::IsItemHovered()&&ImGui::GetIO().MouseWheel!=0)m_canvasZoom=std::clamp(m_canvasZoom*std::pow(1.12f,ImGui::GetIO().MouseWheel),.35f,2.5f);
    if(ImGui::IsItemHovered()&&ImGui::IsMouseDragging(ImGuiMouseButton_Middle))m_canvasPan+=glm::vec2(ImGui::GetIO().MouseDelta.x,ImGui::GetIO().MouseDelta.y);
    const float grid=32*m_canvasZoom;for(float x=std::fmod(m_canvasPan.x,grid);x<size.x;x+=grid)draw->AddLine({origin.x+x,origin.y},{origin.x+x,origin.y+size.y},IM_COL32(42,46,55,255));for(float y=std::fmod(m_canvasPan.y,grid);y<size.y;y+=grid)draw->AddLine({origin.x,origin.y+y},{origin.x+size.x,origin.y+y},IM_COL32(42,46,55,255));
    auto pos=[&](const engine::ScatterGraphNode&n){return ImVec2(origin.x+m_canvasPan.x+n.editorPosition.x*m_canvasZoom,origin.y+m_canvasPan.y+n.editorPosition.y*m_canvasZoom);};
    for(const auto&n:m_graph.nodes)if(n.input){const auto it=std::find_if(m_graph.nodes.begin(),m_graph.nodes.end(),[&](const auto&x){return x.id==n.input;});if(it!=m_graph.nodes.end()){const ImVec2 a=pos(*it),b=pos(n);draw->AddBezierCubic({a.x+180*m_canvasZoom,a.y+32*m_canvasZoom},{a.x+240*m_canvasZoom,a.y+32*m_canvasZoom},{b.x-60*m_canvasZoom,b.y+32*m_canvasZoom},{b.x,b.y+32*m_canvasZoom},IM_COL32(150,170,195,220),2);}}
    for(auto&n:m_graph.nodes){const ImVec2 p=pos(n),nodeSize(180*m_canvasZoom,64*m_canvasZoom);ImGui::SetCursorScreenPos(p);ImGui::PushID(static_cast<int>(n.id));ImGui::InvisibleButton("node",nodeSize);if(ImGui::IsItemClicked())m_selectedNode=n.id;if(ImGui::IsItemActive()&&ImGui::IsMouseDragging(ImGuiMouseButton_Left)){n.editorPosition+=glm::vec2(ImGui::GetIO().MouseDelta.x,ImGui::GetIO().MouseDelta.y)/m_canvasZoom;m_dirty=true;}
        draw->AddRectFilled(p,{p.x+nodeSize.x,p.y+nodeSize.y},n.enabled?IM_COL32(39,43,52,255):IM_COL32(31,32,36,255),6);draw->AddRectFilled(p,{p.x+nodeSize.x,p.y+22*m_canvasZoom},NodeColor(n.type),6,ImDrawFlags_RoundCornersTop);draw->AddRect(p,{p.x+nodeSize.x,p.y+nodeSize.y},n.id==m_selectedNode?IM_COL32(255,180,45,255):IM_COL32(85,95,110,255),6,0,n.id==m_selectedNode?2.5f:1);
        draw->AddText({p.x+7,p.y+4},IM_COL32_WHITE,n.name.c_str());draw->AddText({p.x+7,p.y+31*m_canvasZoom},IM_COL32(178,188,205,255),engine::ScatterNodeTypeName(n.type));ImGui::PopID();}
}

void ProceduralScatterGraphPanel::DrawDetails(){
    engine::ScatterGraphNode*n=SelectedNode();if(!n){ImGui::TextDisabled("Select a node.");return;}ImGui::PushID(static_cast<int>(n->id));
    char label[96];std::snprintf(label,sizeof(label),"%s",n->name.c_str());if(ImGui::InputText("Name",label,sizeof(label))){n->name=label;m_dirty=true;}m_dirty|=ImGui::Checkbox("Enabled",&n->enabled);
    const char* inputLabel="None";for(const auto&candidate:m_graph.nodes)if(candidate.id==n->input)inputLabel=candidate.name.c_str();if(ImGui::BeginCombo("Input",inputLabel)){if(ImGui::Selectable("None",n->input==0)){n->input=0;m_dirty=true;}for(const auto&candidate:m_graph.nodes)if(candidate.id!=n->id&&ImGui::Selectable(candidate.name.c_str(),n->input==candidate.id)){n->input=candidate.id;m_dirty=true;}ImGui::EndCombo();}
    switch(n->type){
    case engine::ScatterNodeType::Region:ImGui::TextDisabled("Uses graph region bounds.");break;
    case engine::ScatterNodeType::Density:m_dirty|=ImGui::DragFloat("Instances / m2",&n->density,.01f,0,20,"%.3f");break;
    case engine::ScatterNodeType::HeightFilter:m_dirty|=ImGui::DragFloatRange2("World Height",&n->minimum,&n->maximum,.1f,-100000,100000,"%.2f","%.2f");break;
    case engine::ScatterNodeType::SlopeFilter:m_dirty|=ImGui::DragFloatRange2("Slope",&n->minimum,&n->maximum,1,0,180,"%.0f deg","%.0f deg");break;
    case engine::ScatterNodeType::Transform:m_dirty|=ImGui::DragFloat3("Min Scale",&n->minimumScale.x,.01f,.01f,100);m_dirty|=ImGui::DragFloat3("Max Scale",&n->maximumScale.x,.01f,.01f,100);m_dirty|=ImGui::DragFloatRange2("Yaw",&n->minimumYaw,&n->maximumYaw,1,-360,360,"%.0f","%.0f");m_dirty|=ImGui::Checkbox("Align to Surface",&n->alignToSurface);break;
    case engine::ScatterNodeType::ExclusionCircle:m_dirty|=ImGui::DragFloat3("Center",&n->position.x,.1f);m_dirty|=ImGui::DragFloat("Radius",&n->radius,.1f,0,100000);break;
    case engine::ScatterNodeType::MeshOutput:{const std::string preview=n->meshPath.empty()?"Choose static mesh...":std::filesystem::path(n->meshPath).filename().string();if(ImGui::BeginCombo("Mesh",preview.c_str())){for(const auto&mesh:m_meshes)if(ImGui::Selectable(mesh.relative.c_str(),mesh.path==n->meshPath)){n->meshPath=mesh.path;n->meshId=mesh.id;m_dirty=true;}ImGui::EndCombo();}m_dirty|=ImGui::DragFloat("Weight",&n->weight,.05f,0,100,"%.2f");break;}
    }
    if(n->type!=engine::ScatterNodeType::Region&&ImGui::Button("Delete Node",{-1,0})){const auto id=n->id;m_graph.nodes.erase(std::remove_if(m_graph.nodes.begin(),m_graph.nodes.end(),[&](const auto&x){return x.id==id;}),m_graph.nodes.end());for(auto&x:m_graph.nodes)if(x.input==id)x.input=0;m_selectedNode=0;m_dirty=true;}
    ImGui::PopID();
}

void ProceduralScatterGraphPanel::DrawPreview(){
    const ImVec2 origin=ImGui::GetCursorScreenPos(),size=ImGui::GetContentRegionAvail();ImGui::InvisibleButton("##ScatterPreview",size);ImDrawList*draw=ImGui::GetWindowDrawList();draw->AddRectFilled(origin,{origin.x+size.x,origin.y+size.y},IM_COL32(27,32,38,255));
    const glm::vec3 span=glm::max(m_graph.regionMaximum-m_graph.regionMinimum,glm::vec3(.001f));for(const auto&p:m_preview){const float u=(p.position.x-m_graph.regionMinimum.x)/span.x,v=(p.position.z-m_graph.regionMinimum.z)/span.z;const ImVec2 q(origin.x+u*size.x,origin.y+(1-v)*size.y);std::uint32_t hash=2166136261u;for(char c:p.meshPath)hash=(hash^static_cast<unsigned char>(c))*16777619u;draw->AddCircleFilled(q,2.5f,IM_COL32(70+hash%150,110+(hash>>8)%120,75+(hash>>16)%150,220));}
    draw->AddText({origin.x+8,origin.y+8},IM_COL32_WHITE,(std::to_string(m_preview.size())+" preview instances").c_str());
}

ProceduralScatterGraphPanel::Result ProceduralScatterGraphPanel::Draw(const std::string&root,bool*open){
    Result result;if(m_scannedRoot!=root)RefreshMeshes(root);if(!m_pendingOpen.empty()){std::string error;Load(m_pendingOpen,&error);m_pendingOpen.clear();}if(m_graph.nodes.empty())NewGraph(root);
    if(!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::ProceduralScatterGraph),open)){ImGui::End();return result;}
    if(ImGui::Button("New"))NewGraph(root);ImGui::SameLine();if(ImGui::Button(m_dirty?"Save *":"Save")){std::string error;if(Save(&error)){result.saveRequested=true;result.refreshAssets=true;}}ImGui::SameLine();if(ImGui::Button("Refresh Meshes"))RefreshMeshes(root);
    ImGui::InputText("Graph Name",m_name.data(),m_name.size());ImGui::InputText("Asset Path",m_pathBuffer.data(),m_pathBuffer.size());
    ImGui::DragScalar("Seed",ImGuiDataType_U32,&m_graph.seed,.0f);ImGui::DragScalar("Maximum Instances",ImGuiDataType_U32,&m_graph.maximumInstances,.0f);m_graph.maximumInstances=std::clamp(m_graph.maximumInstances,1u,1000000u);
    m_dirty|=ImGui::DragFloat3("Region Minimum",&m_graph.regionMinimum.x,.1f);m_dirty|=ImGui::DragFloat3("Region Maximum",&m_graph.regionMaximum.x,.1f);
    const char*types[]={"Region Input","Density","Height Filter","Slope Filter","Random Transform","Exclusion Circle","Weighted Mesh Output"};ImGui::SetNextItemWidth(180);ImGui::Combo("##AddScatterType",&m_addType,types,IM_ARRAYSIZE(types));ImGui::SameLine();if(ImGui::Button("Add Node"))AddNode(static_cast<engine::ScatterNodeType>(m_addType));
    ImGui::SameLine();ImGui::Checkbox("Auto Preview",&m_autoPreview);ImGui::SameLine();if(ImGui::Button("Generate Preview")||m_autoPreview&&m_dirty&&ImGui::GetFrameCount()%20==0)RefreshPreview();
    const float bottom=240;const float details=290;if(ImGui::BeginChild("##ScatterGraph",{ImGui::GetContentRegionAvail().x-details,-bottom},true))DrawCanvas();ImGui::EndChild();ImGui::SameLine();if(ImGui::BeginChild("##ScatterDetails",{0,-bottom},true))DrawDetails();ImGui::EndChild();
    if(ImGui::BeginChild("##ScatterPreviewHost",{ImGui::GetContentRegionAvail().x*.68f,0},true))DrawPreview();ImGui::EndChild();ImGui::SameLine();if(ImGui::BeginChild("##ScatterBake",{0,0},true)){ImGui::TextUnformatted("BAKE");const char*targets[]={"Editable Objects","Foliage Instances"};ImGui::Combo("Target",&m_bakeTarget,targets,2);ImGui::TextWrapped("Preview is temporary. Bake only when the result is ready.");if(ImGui::Button("Bake Generated Result",{-1,0})){RefreshPreview();result.bakeRequested=true;result.bakeTarget=static_cast<BakeTarget>(m_bakeTarget);result.placements=m_preview;}}ImGui::EndChild();
    ImGui::End();return result;
}
