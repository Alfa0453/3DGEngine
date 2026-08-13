#pragma once

#include "AssetDependencyGraph.h"

#include <array>
#include <string>

class AssetDependencyViewerPanel {
public:
    struct Result {
        bool synchronizeRegistry = false;
        std::string openRelativePath;
        engine::AssetType openType = engine::AssetType::Unknown;
        std::string revealRelativePath;
        std::string message;
    };
    Result Draw(const engine::AssetRegistry& registry,const std::string& contentRoot,bool* open);
    void Invalidate(){m_built=false;}
private:
    static std::string RelativePath(const std::string& virtualPath);
    void DrawGraphView();
    void DrawTreeView();
    void DrawListView();
    bool ExportReport(const std::string& contentRoot,std::string* path,std::string* error)const;

    AssetDependencyGraph m_graph;
    engine::AssetHandle m_selected;
    std::array<char,160> m_search{};
    int m_typeFilter=0;
    int m_view=0;
    bool m_issuesOnly=false;
    bool m_built=false;
};
