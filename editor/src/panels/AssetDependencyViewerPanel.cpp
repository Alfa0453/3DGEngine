#include "AssetDependencyViewerPanel.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <unordered_set>

namespace {
const char* IssueName(AssetDependencyGraph::IssueKind kind){using K=AssetDependencyGraph::IssueKind;switch(kind){case K::Missing:return "Missing";case K::Circular:return "Circular";case K::DuplicateDependency:return "Duplicate reference";case K::DuplicateContent:return "Duplicate content";case K::Unreferenced:return "Unreferenced";case K::StalePath:return "Stale path";}return "Issue";}
ImU32 TypeColor(engine::AssetType type){const unsigned n=static_cast<unsigned>(type);return IM_COL32(75+(n*47)%150,95+(n*71)%130,120+(n*31)%120,255);}
std::string Leaf(const std::string& path){const auto p=path.find_last_of('/');return p==std::string::npos?path:path.substr(p+1);}
}

std::string AssetDependencyViewerPanel::RelativePath(const std::string& path){return path.rfind("/Game/",0)==0?path.substr(6):path;}

AssetDependencyViewerPanel::Result AssetDependencyViewerPanel::Draw(const engine::AssetRegistry& registry,const std::string& contentRoot,bool* open){
    Result result;if(!ImGui::Begin("Asset Dependency Viewer",open)){ImGui::End();return result;}
    if(!m_built){m_graph.Build(registry,contentRoot);m_built=true;if(!m_selected.Valid()&&!m_graph.Nodes().empty())m_selected=m_graph.Nodes().front().asset.id;}
    if(ImGui::Button("Refresh Graph")){m_graph.Build(registry,contentRoot);m_built=true;}ImGui::SameLine();
    if(ImGui::Button("Sync Registry"))result.synchronizeRegistry=true;ImGui::SameLine();
    std::string reportPath,error;if(ImGui::Button("Export Report")){if(ExportReport(contentRoot,&reportPath,&error))result.message="Exported dependency report: "+reportPath;else result.message=error;}
    ImGui::SameLine();ImGui::TextDisabled("%d assets | %d findings",static_cast<int>(m_graph.Nodes().size()),static_cast<int>(m_graph.Issues().size()));
    ImGui::SetNextItemWidth(260);ImGui::InputTextWithHint("##DependencySearch","Search path, type, or asset ID",m_search.data(),m_search.size());ImGui::SameLine();
    ImGui::SetNextItemWidth(180);const char* preview=m_typeFilter==0?"All asset types":engine::AssetTypeName(static_cast<engine::AssetType>(m_typeFilter));
    if(ImGui::BeginCombo("##DependencyType",preview)){if(ImGui::Selectable("All asset types",m_typeFilter==0))m_typeFilter=0;for(int i=1;i<=static_cast<int>(engine::AssetType::ScatterGraph);++i)if(ImGui::Selectable(engine::AssetTypeName(static_cast<engine::AssetType>(i)),m_typeFilter==i))m_typeFilter=i;ImGui::EndCombo();}
    ImGui::SameLine();ImGui::Checkbox("Findings only",&m_issuesOnly);
    ImGui::Separator();

    const auto matches=m_graph.Search(m_search.data(),static_cast<engine::AssetType>(m_typeFilter),m_issuesOnly);
    ImGui::BeginChild("DependencyAssets",ImVec2(285,0),true);ImGui::TextDisabled("Assets (%d)",static_cast<int>(matches.size()));
    for(const auto* n:matches){bool issue=!n->issues.empty();if(issue)ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.68f,.25f,1));ImGui::PushID(n->asset.id.ToString().c_str());if(ImGui::Selectable((Leaf(n->asset.virtualPath)+"##asset").c_str(),m_selected==n->asset.id))m_selected=n->asset.id;if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\n%s",engine::AssetTypeName(n->asset.type),n->asset.virtualPath.c_str());ImGui::PopID();if(issue)ImGui::PopStyleColor();}
    ImGui::EndChild();ImGui::SameLine();ImGui::BeginChild("DependencyMain",ImVec2(0,0),true);
    const auto* selected=m_graph.Find(m_selected);if(!selected){ImGui::TextDisabled("Select an asset.");ImGui::EndChild();ImGui::End();return result;}
    ImGui::Text("%s",Leaf(selected->asset.virtualPath).c_str());ImGui::SameLine();ImGui::TextDisabled("[%s]",engine::AssetTypeName(selected->asset.type));
    ImGui::TextWrapped("%s",selected->asset.virtualPath.c_str());
    if(ImGui::Button("Open Asset")){result.openRelativePath=RelativePath(selected->asset.virtualPath);result.openType=selected->asset.type;}ImGui::SameLine();if(ImGui::Button("Reveal in Content"))result.revealRelativePath=RelativePath(selected->asset.virtualPath);ImGui::SameLine();if(ImGui::Button("Copy ID"))ImGui::SetClipboardText(selected->asset.id.ToString().c_str());
    ImGui::Separator();if(ImGui::RadioButton("Graph",m_view==0))m_view=0;ImGui::SameLine();if(ImGui::RadioButton("Tree",m_view==1))m_view=1;ImGui::SameLine();if(ImGui::RadioButton("List",m_view==2))m_view=2;
    if(m_view==0)DrawGraphView();else if(m_view==1)DrawTreeView();else DrawListView();
    ImGui::Separator();
    if(ImGui::CollapsingHeader("Findings",ImGuiTreeNodeFlags_DefaultOpen)){if(selected->issues.empty())ImGui::TextDisabled("No findings for this asset.");for(auto index:selected->issues){const auto& issue=m_graph.Issues()[index];ImGui::BulletText("%s: %s",IssueName(issue.kind),issue.message.c_str());}}
    if(ImGui::CollapsingHeader("Registry Details")){ImGui::TextWrapped("ID: %s",selected->asset.id.ToString().c_str());ImGui::TextWrapped("Source: %s",selected->asset.sourcePath.empty()?"(authored in project)":selected->asset.sourcePath.c_str());ImGui::Text("Source hash: %llu",static_cast<unsigned long long>(selected->asset.sourceHash));ImGui::Text("Importer version: %u",selected->asset.importerVersion);}
    ImGui::EndChild();ImGui::End();return result;
}

void AssetDependencyViewerPanel::DrawGraphView(){
    const auto* center=m_graph.Find(m_selected);if(!center)return;const ImVec2 size(ImGui::GetContentRegionAvail().x,std::max(260.f,ImGui::GetContentRegionAvail().y*.55f));ImGui::BeginChild("DependencyGraphCanvas",size,true,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* draw=ImGui::GetWindowDrawList();const ImVec2 origin=ImGui::GetWindowPos(),avail=ImGui::GetWindowSize();const ImVec2 c(origin.x+avail.x*.5f,origin.y+avail.y*.5f);const ImVec2 nodeSize(170,42);
    auto drawNode=[&](engine::AssetHandle id,ImVec2 p,bool selected){const auto* n=m_graph.Find(id);std::string label=n?Leaf(n->asset.virtualPath):"Missing asset";ImU32 color=n?TypeColor(n->asset.type):IM_COL32(210,55,55,255);draw->AddRectFilled(p,ImVec2(p.x+nodeSize.x,p.y+nodeSize.y),selected?IM_COL32(52,88,130,255):IM_COL32(36,39,45,255),5.0f);draw->AddRect(p,ImVec2(p.x+nodeSize.x,p.y+nodeSize.y),color,5.0f,ImDrawFlags_None,2.0f);draw->AddText(ImVec2(p.x+7,p.y+6),IM_COL32_WHITE,label.c_str());if(n)draw->AddText(ImVec2(p.x+7,p.y+23),IM_COL32(160,170,185,255),engine::AssetTypeName(n->asset.type));ImGui::SetCursorScreenPos(p);ImGui::PushID(id.ToString().c_str());if(ImGui::InvisibleButton("node",nodeSize)&&n)m_selected=id;ImGui::PopID();};
    ImVec2 cp(c.x-nodeSize.x*.5f,c.y-nodeSize.y*.5f);const float gap=std::max(52.f,(avail.y-30)/std::max<size_t>(1,std::max(center->incoming.size(),center->outgoing.size())));
    for(size_t i=0;i<center->incoming.size();++i){ImVec2 p(origin.x+14,origin.y+15+i*gap);draw->AddBezierCubic(ImVec2(p.x+nodeSize.x,p.y+21),ImVec2(c.x-130,p.y+21),ImVec2(c.x-130,c.y),ImVec2(cp.x,cp.y+21),IM_COL32(125,150,180,255),1.5f);drawNode(center->incoming[i],p,false);}
    for(size_t i=0;i<center->outgoing.size();++i){ImVec2 p(origin.x+avail.x-nodeSize.x-14,origin.y+15+i*gap);draw->AddBezierCubic(ImVec2(cp.x+nodeSize.x,cp.y+21),ImVec2(c.x+130,c.y),ImVec2(c.x+130,p.y+21),ImVec2(p.x,p.y+21),m_graph.Find(center->outgoing[i])?IM_COL32(125,150,180,255):IM_COL32(220,65,65,255),1.5f);drawNode(center->outgoing[i],p,false);}
    drawNode(center->asset.id,cp,true);ImGui::EndChild();
}

void AssetDependencyViewerPanel::DrawTreeView(){
    const auto* root=m_graph.Find(m_selected);if(!root)return;std::unordered_set<engine::AssetHandle,engine::AssetHandleHash> path;
    std::function<void(const AssetDependencyGraph::Node*,int)> draw=[&](const AssetDependencyGraph::Node* n,int depth){if(!n||depth>8)return;path.insert(n->asset.id);ImGui::PushID(n->asset.id.ToString().c_str());bool open=ImGui::TreeNodeEx(Leaf(n->asset.virtualPath).c_str(),n->outgoing.empty()?ImGuiTreeNodeFlags_Leaf:0,"%s [%s]",Leaf(n->asset.virtualPath).c_str(),engine::AssetTypeName(n->asset.type));if(ImGui::IsItemClicked())m_selected=n->asset.id;if(open){for(auto id:n->outgoing){if(path.count(id)){ImGui::BulletText("%s (cycle)",m_graph.Find(id)?Leaf(m_graph.Find(id)->asset.virtualPath).c_str():"Missing");continue;}if(const auto* child=m_graph.Find(id))draw(child,depth+1);else ImGui::BulletText("Missing: %s",id.ToString().c_str());}ImGui::TreePop();}ImGui::PopID();path.erase(n->asset.id);};draw(root,0);
}

void AssetDependencyViewerPanel::DrawListView(){const auto* n=m_graph.Find(m_selected);if(!n)return;if(ImGui::BeginTable("DependencyLists",2,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg)){ImGui::TableSetupColumn("Referenced by");ImGui::TableSetupColumn("Depends on");ImGui::TableHeadersRow();const size_t rows=std::max(n->incoming.size(),n->outgoing.size());for(size_t i=0;i<rows;++i){ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);if(i<n->incoming.size()){const auto* x=m_graph.Find(n->incoming[i]);if(ImGui::Selectable(x?Leaf(x->asset.virtualPath).c_str():"Missing"))m_selected=n->incoming[i];}ImGui::TableSetColumnIndex(1);if(i<n->outgoing.size()){const auto* x=m_graph.Find(n->outgoing[i]);if(ImGui::Selectable(x?Leaf(x->asset.virtualPath).c_str():"Missing"))if(x)m_selected=n->outgoing[i];}}ImGui::EndTable();}}

bool AssetDependencyViewerPanel::ExportReport(const std::string& root,std::string* path,std::string* error)const{std::filesystem::path out=std::filesystem::path(root)/"Reports"/"AssetDependencies.txt";std::error_code ec;std::filesystem::create_directories(out.parent_path(),ec);std::ofstream f(out);if(ec||!f){if(error)*error="Could not write dependency report.";return false;}f<<"3DG Asset Dependency Report\nAssets: "<<m_graph.Nodes().size()<<"\nFindings: "<<m_graph.Issues().size()<<"\n\n";for(const auto& n:m_graph.Nodes()){f<<n.asset.virtualPath<<" ["<<engine::AssetTypeName(n.asset.type)<<"]\n";for(auto d:n.outgoing){const auto* x=m_graph.Find(d);f<<"  -> "<<(x?x->asset.virtualPath:d.ToString())<<"\n";}for(auto i:n.issues)f<<"  ! "<<IssueName(m_graph.Issues()[i].kind)<<": "<<m_graph.Issues()[i].message<<"\n";}if(!f){if(error)*error="Could not finish dependency report.";return false;}if(path)*path=out.string();return true;}
