#include "WorldPartitionPanel.h"
#include "EditorPanels.h"

#include <engine/scene/LevelStreamingManager.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace {
std::filesystem::path Resolve(const std::string& value,const std::string& worldPath){
    std::filesystem::path p(value);if(p.is_relative()&&!worldPath.empty())p=std::filesystem::path(worldPath).parent_path()/p;return p.lexically_normal();
}
WorldPartitionPanel::Cell EstimateCell(int x,int z){WorldPartitionPanel::Cell c;c.x=x;c.z=z;return c;}
void EstimateScene(const std::filesystem::path& path,WorldPartitionPanel::Cell& cell){
    std::error_code ec;cell.sourceBytes+=std::filesystem::file_size(path,ec);if(ec)return;
    std::ifstream in(path);std::string line;
    std::set<std::string> textures;
    while(std::getline(in,line)){
        if(line.rfind("object ",0)==0||line.rfind("entity ",0)==0)++cell.estimatedObjects;
        const auto pos=line.find("triangles ");if(pos!=std::string::npos){try{cell.estimatedTriangles+=std::stoi(line.substr(pos+10));}catch(...){}}
        std::size_t cursor=0;while((cursor=line.find(".3dgtex",cursor))!=std::string::npos){std::size_t begin=line.rfind(' ',cursor);textures.insert(line.substr(begin==std::string::npos?0:begin+1,cursor-(begin==std::string::npos?0:begin+1)+7));cursor+=7;}
    }
    cell.estimatedTextures+=static_cast<int>(textures.size());
}
}

glm::ivec2 WorldPartitionPanel::CellFor(const engine::WorldPartitionSettings& settings,const glm::vec3& p){
    const float size=std::max(settings.cellSize,1.0f);
    return {static_cast<int>(std::floor((p.x-settings.origin.x)/size)),static_cast<int>(std::floor((p.z-settings.origin.y)/size))};
}
void WorldPartitionPanel::AssignCells(engine::WorldManifest& world,bool radii){
    world.partition.cellSize=std::clamp(world.partition.cellSize,1.0f,100000.0f);
    world.partition.defaultLoadRadius=std::max(world.partition.defaultLoadRadius,world.partition.cellSize*.5f);
    world.partition.defaultUnloadRadius=std::max(world.partition.defaultUnloadRadius,world.partition.defaultLoadRadius);
    for(auto& level:world.levels){const glm::ivec2 c=CellFor(world.partition,level.WorldBoundsCenter());level.partitionX=c.x;level.partitionZ=c.y;if(radii&&level.rule==engine::LevelStreamRule::Distance){level.loadRadius=world.partition.defaultLoadRadius;level.unloadRadius=world.partition.defaultUnloadRadius;}}
}
std::vector<WorldPartitionPanel::Cell> WorldPartitionPanel::BuildCells(const engine::WorldManifest& world,const std::string& path){
    std::map<std::pair<int,int>,Cell> cells;
    for(const auto& level:world.levels){auto key=std::make_pair(level.partitionX,level.partitionZ);auto [it,inserted]=cells.try_emplace(key,EstimateCell(key.first,key.second));(void)inserted;Cell& c=it->second;++c.instances;if(level.rule==engine::LevelStreamRule::AlwaysLoaded)++c.alwaysLoaded;EstimateScene(Resolve(level.scenePath,path),c);}
    std::vector<Cell> result;for(auto& [key,cell]:cells){(void)key;result.push_back(cell);}return result;
}
std::vector<WorldPartitionPanel::Issue> WorldPartitionPanel::Validate(const engine::WorldManifest& world){
    std::vector<Issue> issues;const float halfDiagonal=world.partition.cellSize*.707107f;
    std::set<std::tuple<int,int,std::string>> placements;
    for(std::size_t i=0;i<world.levels.size();++i){const auto& level=world.levels[i];const glm::ivec2 expected=CellFor(world.partition,level.WorldBoundsCenter());
        if(expected.x!=level.partitionX||expected.y!=level.partitionZ)issues.push_back({static_cast<int>(i),"Placement is outside its assigned cell."});
        const glm::vec3 extent=glm::abs(level.boundsMax-level.boundsMin);if(extent.x>world.partition.cellSize||extent.z>world.partition.cellSize)issues.push_back({static_cast<int>(i),"Level bounds exceed one partition cell."});
        if(level.rule==engine::LevelStreamRule::Distance&&level.loadRadius<halfDiagonal)issues.push_back({static_cast<int>(i),"Load radius may leave gaps at cell corners."});
        auto key=std::make_tuple(level.partitionX,level.partitionZ,level.scenePath);if(!placements.insert(key).second)issues.push_back({static_cast<int>(i),"Duplicate source placement in this cell."});
    }return issues;
}

void WorldPartitionPanel::DrawGrid(const engine::WorldManifest& world,const std::vector<Cell>& cells){
    ImVec2 size(ImGui::GetContentRegionAvail().x,300);size.x=std::max(size.x,240.f);const ImVec2 p=ImGui::GetCursorScreenPos();ImGui::InvisibleButton("partition_grid",size,ImGuiButtonFlags_MouseButtonLeft);auto* dl=ImGui::GetWindowDrawList();dl->AddRectFilled(p,{p.x+size.x,p.y+size.y},IM_COL32(19,23,29,255));
    int minX=-2,maxX=2,minZ=-2,maxZ=2;for(const auto& c:cells){minX=std::min(minX,c.x);maxX=std::max(maxX,c.x);minZ=std::min(minZ,c.z);maxZ=std::max(maxZ,c.z);}const int cols=maxX-minX+1,rows=maxZ-minZ+1;const float cell=std::min(size.x/cols,size.y/rows);const ImVec2 origin(p.x+(size.x-cols*cell)*.5f,p.y+(size.y-rows*cell)*.5f);
    for(int z=minZ;z<=maxZ;++z)for(int x=minX;x<=maxX;++x){const ImVec2 a(origin.x+(x-minX)*cell,origin.y+(maxZ-z)*cell),b(a.x+cell,a.y+cell);const Cell* found=nullptr;for(const auto& c:cells)if(c.x==x&&c.z==z){found=&c;break;}ImU32 color=found?IM_COL32(46,108,160,180):IM_COL32(38,43,51,255);if(found&&found->alwaysLoaded)color=IM_COL32(128,92,35,210);if(x==m_selectedCellX&&z==m_selectedCellZ)color=IM_COL32(54,145,89,220);dl->AddRectFilled(a,b,color);dl->AddRect(a,b,IM_COL32(100,110,125,255));if(found){const std::string label=std::to_string(x)+","+std::to_string(z)+"  ["+std::to_string(found->instances)+"]";dl->AddText({a.x+4,a.y+4},IM_COL32_WHITE,label.c_str());}}
    const glm::ivec2 viewer=CellFor(world.partition,m_previewViewer);if(viewer.x>=minX&&viewer.x<=maxX&&viewer.y>=minZ&&viewer.y<=maxZ){const ImVec2 a(origin.x+(viewer.x-minX)*cell,origin.y+(maxZ-viewer.y)*cell);dl->AddCircleFilled({a.x+cell*.5f,a.y+cell*.5f},5,IM_COL32(255,235,80,255));}
    if(ImGui::IsItemClicked()){const ImVec2 m=ImGui::GetIO().MousePos;m_selectedCellX=minX+static_cast<int>((m.x-origin.x)/cell);m_selectedCellZ=maxZ-static_cast<int>((m.y-origin.y)/cell);}
}

WorldPartitionPanel::Result WorldPartitionPanel::Draw(engine::WorldManifest& world,const std::string& root,int selectedCount,bool* open){
    Result result;if(!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::WorldPartition),open)){ImGui::End();return result;}auto& s=world.partition;
    ImGui::TextDisabled("Organize linked levels into streamable world cells.");result.changed|=ImGui::Checkbox("Enable World Partition",&s.enabled);ImGui::SameLine();if(ImGui::Button("Save World"))result.saveWorld=true;
    ImGui::SeparatorText("Grid Settings");result.changed|=ImGui::DragFloat("Cell Size",&s.cellSize,1,1,100000,"%.1f m");result.changed|=ImGui::DragFloat2("World Origin",&s.origin.x,1);result.changed|=ImGui::DragFloat("Default Load Radius",&s.defaultLoadRadius,1,1,100000,"%.1f m");result.changed|=ImGui::DragFloat("Default Unload Radius",&s.defaultUnloadRadius,1,1,100000,"%.1f m");
    if(ImGui::Button("Assign / Refresh Cells")){AssignCells(world,false);result.changed=true;}ImGui::SameLine();if(ImGui::Button("Assign + Apply Default Ranges")){AssignCells(world,true);result.changed=true;}
    ImGui::SeparatorText("Convert Loose Actors To Cell");ImGui::InputText("Cell Level Name",m_cellName.data(),m_cellName.size());ImGui::DragInt("Target Cell X",&m_selectedCellX);ImGui::SameLine();ImGui::DragInt("Z",&m_selectedCellZ);if(selectedCount<=0)ImGui::BeginDisabled();if(ImGui::Button("Create Cell Level From Selection")){result.createCellFromSelection=true;result.cellX=m_selectedCellX;result.cellZ=m_selectedCellZ;result.cellScenePath=(std::filesystem::path(root)/"GameAssets"/"WorldCells"/(std::string(m_cellName.data())+".scene")).string();}if(selectedCount<=0)ImGui::EndDisabled();ImGui::SameLine();ImGui::TextDisabled("%d selected",selectedCount);
    const auto cells=BuildCells(world);ImGui::SeparatorText("Streaming Preview");ImGui::DragFloat3("Preview Viewer",&m_previewViewer.x,1);DrawGrid(world,cells);
    std::set<std::string> layers;for(const auto& l:world.levels)layers.insert(l.dataLayer.empty()?"Default":l.dataLayer);ImGui::SeparatorText("Active Data Layers");for(const auto& layer:layers){bool active=s.activeDataLayers.empty()||std::find(s.activeDataLayers.begin(),s.activeDataLayers.end(),layer)!=s.activeDataLayers.end();if(ImGui::Checkbox(layer.c_str(),&active)){if(s.activeDataLayers.empty())for(const auto& value:layers)s.activeDataLayers.push_back(value);auto it=std::find(s.activeDataLayers.begin(),s.activeDataLayers.end(),layer);if(active&&it==s.activeDataLayers.end())s.activeDataLayers.push_back(layer);else if(!active&&it!=s.activeDataLayers.end())s.activeDataLayers.erase(it);result.changed=true;}ImGui::SameLine();}
    int resident=0;for(const auto& l:world.levels){const bool layerActive=!s.enabled||s.activeDataLayers.empty()||std::find(s.activeDataLayers.begin(),s.activeDataLayers.end(),l.dataLayer)!=s.activeDataLayers.end();if(layerActive&&engine::LevelStreamingManager::WantsResident(l,false,m_previewViewer))++resident;}ImGui::NewLine();ImGui::Text("%d cells | %d instances | %d predicted resident",static_cast<int>(cells.size()),static_cast<int>(world.levels.size()),resident);
    if(ImGui::BeginTable("cell_cost",7,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg)){for(const char* h:{"Cell","Instances","Objects*","Triangles*","Textures*","Source MB","Always"})ImGui::TableSetupColumn(h);ImGui::TableHeadersRow();for(const auto& c:cells){ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::Text("%d,%d",c.x,c.z);ImGui::TableSetColumnIndex(1);ImGui::Text("%d",c.instances);ImGui::TableSetColumnIndex(2);ImGui::Text("%d",c.estimatedObjects);ImGui::TableSetColumnIndex(3);ImGui::Text("%d",c.estimatedTriangles);ImGui::TableSetColumnIndex(4);ImGui::Text("%d",c.estimatedTextures);ImGui::TableSetColumnIndex(5);ImGui::Text("%.2f",c.sourceBytes/1048576.0);ImGui::TableSetColumnIndex(6);ImGui::Text("%d",c.alwaysLoaded);}ImGui::EndTable();}ImGui::TextDisabled("* Estimates are read from source scene metadata; cooked costs may differ.");
    const auto issues=Validate(world);if(!issues.empty()){ImGui::SeparatorText("Validation");for(const auto& issue:issues)ImGui::BulletText("Instance %d: %s",issue.level+1,issue.message.c_str());}
    ImGui::End();return result;
}
