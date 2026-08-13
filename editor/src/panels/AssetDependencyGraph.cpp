#include "AssetDependencyGraph.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <unordered_set>

namespace {
std::string Lower(std::string value){std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return value;}
std::string RelativePath(std::string virtualPath){if(virtualPath.rfind("/Game/",0)==0)virtualPath.erase(0,6);return virtualPath;}
}

void AssetDependencyGraph::AddIssue(Issue issue){
    const std::size_t i=m_issues.size();m_issues.push_back(std::move(issue));
    auto found=m_index.find(m_issues.back().asset);if(found!=m_index.end())m_nodes[found->second].issues.push_back(i);
}

void AssetDependencyGraph::Build(const engine::AssetRegistry& registry,const std::string& contentRoot){
    m_nodes.clear();m_issues.clear();m_index.clear();m_nodes.reserve(registry.Entries().size());
    for(const auto& e:registry.Entries()){m_index[e.id]=m_nodes.size();m_nodes.push_back({e,e.dependencies,{},{}});}
    for(auto& node:m_nodes){
        std::unordered_set<engine::AssetHandle,engine::AssetHandleHash> unique;
        for(auto dependency:node.outgoing){
            if(!unique.insert(dependency).second){AddIssue({IssueKind::DuplicateDependency,node.asset.id,dependency,"Dependency is listed more than once."});continue;}
            auto found=m_index.find(dependency);
            if(found==m_index.end())AddIssue({IssueKind::Missing,node.asset.id,dependency,"Referenced asset is missing from the registry."});
            else m_nodes[found->second].incoming.push_back(node.asset.id);
        }
        if(!contentRoot.empty()){
            const std::filesystem::path authored=std::filesystem::path(contentRoot)/RelativePath(node.asset.virtualPath);
            std::error_code ec;if(!std::filesystem::is_regular_file(authored,ec))AddIssue({IssueKind::StalePath,node.asset.id,{},"Registered Content path no longer exists."});
            if(!node.asset.sourcePath.empty()&&!std::filesystem::exists(node.asset.sourcePath,ec))AddIssue({IssueKind::StalePath,node.asset.id,{},"Original import source no longer exists."});
        }
    }

    enum class Mark{White,Gray,Black};std::unordered_map<engine::AssetHandle,Mark,engine::AssetHandleHash> marks;
    std::vector<engine::AssetHandle> stack;std::unordered_set<engine::AssetHandle,engine::AssetHandleHash> circular;
    std::function<void(engine::AssetHandle)> visit=[&](engine::AssetHandle id){marks[id]=Mark::Gray;stack.push_back(id);const Node* n=Find(id);
        if(n)for(auto next:n->outgoing){if(!Find(next))continue;if(marks[next]==Mark::White)visit(next);else if(marks[next]==Mark::Gray){auto it=std::find(stack.begin(),stack.end(),next);for(;it!=stack.end();++it)circular.insert(*it);}}
        stack.pop_back();marks[id]=Mark::Black;};
    for(const auto& n:m_nodes)marks[n.asset.id]=Mark::White;for(const auto& n:m_nodes)if(marks[n.asset.id]==Mark::White)visit(n.asset.id);
    for(auto id:circular)AddIssue({IssueKind::Circular,id,{},"Asset participates in a circular dependency."});

    std::unordered_map<std::string,std::vector<engine::AssetHandle>> hashes;
    for(const auto& n:m_nodes)if(n.asset.sourceHash)hashes[std::to_string(static_cast<unsigned>(n.asset.type))+":"+std::to_string(n.asset.sourceHash)].push_back(n.asset.id);
    for(const auto& [hash,ids]:hashes)if(ids.size()>1)for(auto id:ids)AddIssue({IssueKind::DuplicateContent,id,ids.front(),"Another asset has the same imported source hash."});
    for(const auto& n:m_nodes)if(n.incoming.empty()&&n.asset.type!=engine::AssetType::Scene&&n.asset.type!=engine::AssetType::World)
        AddIssue({IssueKind::Unreferenced,n.asset.id,{},"No registered asset references this asset."});
}

const AssetDependencyGraph::Node* AssetDependencyGraph::Find(engine::AssetHandle id)const{auto f=m_index.find(id);return f==m_index.end()?nullptr:&m_nodes[f->second];}
bool AssetDependencyGraph::HasCircularDependency(engine::AssetHandle id)const{const Node* n=Find(id);if(!n)return false;for(auto i:n->issues)if(m_issues[i].kind==IssueKind::Circular)return true;return false;}
std::vector<const AssetDependencyGraph::Node*> AssetDependencyGraph::Search(const std::string& text,engine::AssetType type,bool issuesOnly)const{
    std::vector<const Node*> result;const std::string needle=Lower(text);for(const auto& n:m_nodes){if(type!=engine::AssetType::Unknown&&n.asset.type!=type)continue;if(issuesOnly&&n.issues.empty())continue;
        const std::string hay=Lower(n.asset.virtualPath+" "+engine::AssetTypeName(n.asset.type)+" "+n.asset.id.ToString());if(!needle.empty()&&hay.find(needle)==std::string::npos)continue;result.push_back(&n);}
    std::sort(result.begin(),result.end(),[](const Node*a,const Node*b){return a->asset.virtualPath<b->asset.virtualPath;});return result;
}
