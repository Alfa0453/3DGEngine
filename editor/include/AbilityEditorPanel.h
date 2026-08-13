#pragma once

#include <engine/assets/AbilityAsset.h>

#include <string>

class EditorAssets;

class AbilityEditorPanel {
public:
    void Draw(EditorAssets& assets,const std::string& assetRoot,bool* open,
              bool* assetChanged=nullptr,std::string* message=nullptr);
    void QueueOpen(const std::string& path){m_queuedPath=path;}
private:
    bool Save(EditorAssets& assets,const std::string& root,std::string* error);
    bool Load(const std::string& path,std::string* error);
    void DrawTimeline();
    engine::AbilityAssetData m_asset;
    std::string m_path,m_queuedPath,m_status;
    int m_selectedPhase=0,m_selectedEffect=-1;
    float m_previewTime=0.f;
    bool m_playing=false,m_dirty=false;
};
