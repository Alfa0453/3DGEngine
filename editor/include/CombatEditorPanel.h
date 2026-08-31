#pragma once
#include "EditorAssets.h"
#include <engine/assets/CombatAsset.h>
#include <string>
class CombatEditorPanel{public:struct Result{bool applySelected=false,saved=false;std::string message;};Result Draw(EditorAssets&assets,const std::string&root,bool*open);void QueueOpen(std::string path){m_pendingOpen=std::move(path);}const engine::CombatAssetData&Asset()const{return m_asset;}const std::string&Path()const{return m_path;}bool IsDirty()const{return m_dirty;}bool SaveForShutdown(const std::string&root,std::string*error);private:void New(const std::string&root);bool AssetCombo(const char*label,EditorAssets::Type type,std::string&path,engine::AssetHandle&id,EditorAssets&assets);void DrawTimeline();engine::CombatAssetData m_asset;std::string m_path,m_pendingOpen,m_status;int m_selectedStep=0;float m_previewTime=0,m_debugHealth=100,m_debugPoise=0;bool m_playing=false,m_debugBlocking=false,m_dirty=false;};
