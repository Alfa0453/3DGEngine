#pragma once
#include "EditorAssets.h"
#include <engine/assets/SpawnAsset.h>
#include <string>
class SpawnManagerPanel{public:struct Result{bool applySelected=false,saved=false;std::string message;};Result Draw(EditorAssets& assets,const std::string& root,bool* open);void QueueOpen(std::string path){m_pendingOpen=std::move(path);}const engine::SpawnAssetData& Asset()const{return m_asset;}const std::string& Path()const{return m_path;}bool IsDirty()const{return m_dirty;}bool SaveForShutdown(const std::string& root,std::string* error);private:void New(const std::string&root);bool PrefabCombo(const char*label,std::string&path,engine::AssetHandle&id,EditorAssets&assets);void DrawVolumePreview();engine::SpawnAssetData m_asset;std::string m_path,m_pendingOpen,m_status;int m_selectedEntry=0,m_selectedWave=0;float m_previewDifficulty=1.0f,m_previewTime=0;bool m_previewPlaying=false,m_dirty=false;};
