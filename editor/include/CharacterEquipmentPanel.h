#pragma once
#include "CharacterAsset.h"
#include "EditorAssets.h"
#include <engine/assets/EquipmentAsset.h>
#include <string>
class CharacterEquipmentPanel {
public:
 struct Result{bool saved=false;std::string message;};
 Result Draw(EditorAssets& assets,const std::string& root,bool* open);
 void QueueOpen(std::string path){m_pendingOpen=std::move(path);} bool IsDirty()const{return m_dirty;}const std::string& Path()const{return m_path;}bool SaveForShutdown(const std::string& root,std::string* error);
private:
 void New(const std::string& root);bool Load(const std::string& path,const std::string& root,std::string* error);bool LoadCharacter(const std::string& root,std::string* error);void DrawPreview();
 engine::EquipmentAssetData m_asset;CharacterAsset m_character;std::string m_path,m_pendingOpen,m_status,m_search;int m_selected=-1;bool m_characterLoaded=false,m_dirty=false;
};
