#include "AbilityEditorPanel.h"

#include "EditorAssets.h"

#include <engine/assets/AssetReference.h>
#include <engine/assets/AssetRegistry.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace {
std::string Safe(std::string value){for(char& c:value)if(!(std::isalnum((unsigned char)c)||c=='_'||c=='-'))c='_';return value.empty()?"Ability":value;}
const char* EffectName(engine::AbilityEffectType t){using T=engine::AbilityEffectType;switch(t){case T::Damage:return"Damage";case T::Heal:return"Heal";case T::Impulse:return"Impulse";case T::AnimationAction:return"Animation Action";case T::Projectile:return"Projectile";case T::Particle:return"Particle";case T::Audio:return"Audio";case T::ScriptEvent:return"Script Event";}return"Effect";}
const char* TargetName(engine::AbilityTargetMode t){using T=engine::AbilityTargetMode;switch(t){case T::Self:return"Self";case T::ExplicitTarget:return"Target";case T::Radius:return"Radius";}return"Target";}
bool AssetCombo(const char* label,std::string& value,const std::vector<std::string>& paths){
    bool changed=false;const std::string preview=value.empty()?"Choose asset...":std::filesystem::path(value).filename().string();
    if(ImGui::BeginCombo(label,preview.c_str())){for(const auto& p:paths){const bool selected=p==value;
        if(ImGui::Selectable(std::filesystem::path(p).filename().string().c_str(),selected)){value=p;changed=true;}
        if(selected)ImGui::SetItemDefaultFocus();}ImGui::EndCombo();}return changed;
}
float Duration(const engine::AbilityAssetData& a){float result=0;for(const auto& p:a.phases)result+=std::max(p.duration,0.f);return result;}
}

bool AbilityEditorPanel::Save(EditorAssets&,const std::string& root,std::string* error){
    engine::AssetRegistry registry;std::string ignored;registry.Load(engine::AssetRegistry::DefaultRegistryPath(root),&ignored);
    for(auto& phase:m_asset.phases)for(auto& effect:phase.effects)if(!effect.assetPath.empty())
        effect.assetId=engine::MakeAssetReference(&registry,root,effect.assetPath).id;
    if(m_path.empty())m_path=(std::filesystem::path(root)/"GameAssets"/"Abilities"/(Safe(m_asset.name)+".3dgability")).string();
    if(!engine::SaveAbilityAsset(m_path,m_asset,error))return false;m_dirty=false;return true;
}
bool AbilityEditorPanel::Load(const std::string& path,std::string* error){engine::AbilityAssetData loaded;
    if(!engine::LoadAbilityAsset(path,&loaded,error))return false;m_asset=std::move(loaded);m_path=path;
    m_selectedPhase=0;m_selectedEffect=-1;m_previewTime=0;m_dirty=false;return true;}

void AbilityEditorPanel::DrawTimeline(){
    const ImVec2 size(ImGui::GetContentRegionAvail().x,120);ImGui::InvisibleButton("##AbilityTimeline",size);
    ImDrawList* draw=ImGui::GetWindowDrawList();const ImVec2 min=ImGui::GetItemRectMin(),max=ImGui::GetItemRectMax();
    draw->AddRectFilled(min,max,IM_COL32(15,18,24,255));const float total=std::max(Duration(m_asset),.001f);float cursor=min.x;
    const ImU32 colors[]={IM_COL32(70,110,175,255),IM_COL32(190,105,45,255),IM_COL32(75,150,95,255),IM_COL32(145,80,165,255)};
    float start=0;for(std::size_t i=0;i<m_asset.phases.size();++i){const auto& phase=m_asset.phases[i];
        const float width=size.x*std::max(phase.duration,0.f)/total;const ImVec2 a(cursor,min.y+24),b(cursor+width,max.y-20);
        draw->AddRectFilled(a,b,colors[i%4]);draw->AddRect(a,b,(int)i==m_selectedPhase?IM_COL32(255,220,80,255):IM_COL32(100,110,125,255),0,0,(int)i==m_selectedPhase?3.f:1.f);
        draw->AddText(ImVec2(a.x+5,a.y+5),IM_COL32_WHITE,phase.name.c_str());
        for(const auto& effect:phase.effects){const float x=a.x+width*std::clamp(effect.time/std::max(phase.duration,.001f),0.f,1.f);
            draw->AddTriangleFilled({x,min.y+16},{x-5,min.y+23},{x+5,min.y+23},IM_COL32(255,220,60,255));}
        cursor+=width;start+=phase.duration;}
    const float playX=min.x+size.x*std::clamp(m_previewTime/total,0.f,1.f);draw->AddLine({playX,min.y},{playX,max.y},IM_COL32(255,255,255,230),2);
    draw->AddText({min.x+5,max.y-17},IM_COL32(180,180,180,255),"Effect markers | phases execute left to right");
}

void AbilityEditorPanel::Draw(EditorAssets& assets,const std::string& root,bool* open,bool* changed,std::string* message){
    if(changed)*changed=false;if(!m_queuedPath.empty()){std::string e;m_status=Load(m_queuedPath,&e)?"Loaded ability.":e;m_queuedPath.clear();}
    if(m_asset.phases.empty()){m_asset.phases={{"Wind Up",.25f,true,{}},{"Active",.1f,false,{}},{"Recovery",.35f,true,{}}};}
    if(!ImGui::Begin("Ability Editor",open)){ImGui::End();return;}
    if(ImGui::Button("New")){m_asset={};m_asset.phases={{"Wind Up",.25f,true,{}},{"Active",.1f,false,{}},{"Recovery",.35f,true,{}}};m_path.clear();m_dirty=true;}
    ImGui::SameLine();if(ImGui::Button("Save")){std::string e;if(Save(assets,root,&e)){m_status="Saved "+m_path;if(changed)*changed=true;}else m_status=e;}
    ImGui::SameLine();ImGui::TextUnformatted(m_dirty?"Unsaved *":"Saved");
    m_dirty|=ImGui::InputText("Name",&m_asset.name);m_dirty|=ImGui::InputTextMultiline("Description",&m_asset.description,{0,50});
    ImGui::SeparatorText("Activation");m_dirty|=ImGui::DragFloat("Cooldown",&m_asset.cooldown,.05f,0,3600,"%.2f s");
    m_dirty|=ImGui::DragInt("Maximum Charges",&m_asset.maxCharges,1,1,99);m_dirty|=ImGui::DragFloat("Charge Recovery",&m_asset.chargeRecovery,.05f,0,3600,"%.2f s");
    m_dirty|=ImGui::DragFloat("Mana Cost",&m_asset.manaCost,.25f,0,100000);m_dirty|=ImGui::DragFloat("Stamina Cost",&m_asset.staminaCost,.25f,0,100000);
    m_dirty|=ImGui::DragFloat("Health Cost",&m_asset.healthCost,.25f,0,100000);m_dirty|=ImGui::DragFloat("Activation Range",&m_asset.activationRange,.1f,0,100000,"%.2f m");
    m_dirty|=ImGui::Checkbox("Require Target",&m_asset.requireTarget);m_dirty|=ImGui::Checkbox("Cancel When Damaged",&m_asset.cancelOnDamage);
    DrawTimeline();const float duration=std::max(Duration(m_asset),.001f);if(m_playing)m_previewTime=std::fmod(m_previewTime+ImGui::GetIO().DeltaTime,duration);
    if(ImGui::Button(m_playing?"Pause":"Play"))m_playing=!m_playing;ImGui::SameLine();ImGui::SetNextItemWidth(-1);ImGui::SliderFloat("##AbilityTime",&m_previewTime,0,duration,"%.2f s");
    ImGui::SeparatorText("Phases");
    for(std::size_t i=0;i<m_asset.phases.size();++i){ImGui::PushID((int)i);if(ImGui::Selectable(m_asset.phases[i].name.c_str(),m_selectedPhase==(int)i))m_selectedPhase=(int)i;ImGui::PopID();}
    if(ImGui::Button("Add Phase")){m_asset.phases.push_back({"Phase",.2f,true,{}});m_selectedPhase=(int)m_asset.phases.size()-1;m_dirty=true;}
    ImGui::SameLine();if(m_asset.phases.size()>1&&ImGui::Button("Remove Phase")){m_asset.phases.erase(m_asset.phases.begin()+m_selectedPhase);m_selectedPhase=std::clamp(m_selectedPhase,0,(int)m_asset.phases.size()-1);m_selectedEffect=-1;m_dirty=true;}
    auto& phase=m_asset.phases[(std::size_t)m_selectedPhase];m_dirty|=ImGui::InputText("Phase Name",&phase.name);m_dirty|=ImGui::DragFloat("Duration",&phase.duration,.01f,0,60,"%.3f s");m_dirty|=ImGui::Checkbox("Interruptible",&phase.interruptible);
    ImGui::SeparatorText("Effects");for(std::size_t i=0;i<phase.effects.size();++i){ImGui::PushID((int)i);std::string label=EffectName(phase.effects[i].type)+std::string(" @ ")+std::to_string(phase.effects[i].time)+"s";
        if(ImGui::Selectable(label.c_str(),m_selectedEffect==(int)i))m_selectedEffect=(int)i;ImGui::PopID();}
    if(ImGui::Button("Add Effect")){phase.effects.push_back({});m_selectedEffect=(int)phase.effects.size()-1;m_dirty=true;}ImGui::SameLine();
    if(m_selectedEffect>=0&&m_selectedEffect<(int)phase.effects.size()&&ImGui::Button("Remove Effect")){phase.effects.erase(phase.effects.begin()+m_selectedEffect);m_selectedEffect=std::min(m_selectedEffect,(int)phase.effects.size()-1);m_dirty=true;}
    if(m_selectedEffect>=0&&m_selectedEffect<(int)phase.effects.size()){auto& effect=phase.effects[(std::size_t)m_selectedEffect];
        if(ImGui::BeginCombo("Effect Type",EffectName(effect.type))){for(int i=0;i<=static_cast<int>(engine::AbilityEffectType::ScriptEvent);++i)if(ImGui::Selectable(EffectName((engine::AbilityEffectType)i),i==(int)effect.type)){effect.type=(engine::AbilityEffectType)i;m_dirty=true;}ImGui::EndCombo();}
        if(ImGui::BeginCombo("Target Mode",TargetName(effect.target))){for(int i=0;i<=2;++i)if(ImGui::Selectable(TargetName((engine::AbilityTargetMode)i),i==(int)effect.target)){effect.target=(engine::AbilityTargetMode)i;m_dirty=true;}ImGui::EndCombo();}
        m_dirty|=ImGui::DragFloat("Effect Time",&effect.time,.01f,0,std::max(phase.duration,0.f),"%.3f s");m_dirty|=ImGui::DragFloat("Magnitude",&effect.magnitude,.1f,-100000,100000);
        if(effect.target==engine::AbilityTargetMode::Radius||effect.type==engine::AbilityEffectType::Projectile)m_dirty|=ImGui::DragFloat("Radius",&effect.radius,.02f,0,10000);
        if(effect.type==engine::AbilityEffectType::Projectile){m_dirty|=ImGui::DragFloat("Speed",&effect.speed,.1f,0,10000);m_dirty|=ImGui::DragFloat("Range",&effect.range,.1f,0,100000);}
        if(effect.type==engine::AbilityEffectType::Impulse||effect.type==engine::AbilityEffectType::Projectile)m_dirty|=ImGui::DragFloat3("Direction",&effect.direction.x,.02f,-1,1);
        m_dirty|=ImGui::InputText(effect.type==engine::AbilityEffectType::ScriptEvent?"Event Name":"Action / Effect Name",&effect.name);
        std::vector<std::string> paths;if(effect.type==engine::AbilityEffectType::AnimationAction)paths=assets.ContentAssetPaths(EditorAssets::Type::AnimationClip);
        else if(effect.type==engine::AbilityEffectType::Particle)paths=assets.ContentAssetPaths(EditorAssets::Type::Particle);
        else if(effect.type==engine::AbilityEffectType::Audio)paths=assets.ContentAssetPaths(EditorAssets::Type::Audio);
        else if(effect.type==engine::AbilityEffectType::Projectile)paths=assets.ContentAssetPaths(EditorAssets::Type::Prefab);
        if(!paths.empty())m_dirty|=AssetCombo("Asset",effect.assetPath,paths);
    }
    int missing=0;for(const auto& p:m_asset.phases)for(const auto& e:p.effects)if((e.type==engine::AbilityEffectType::Particle||e.type==engine::AbilityEffectType::Audio||e.type==engine::AbilityEffectType::AnimationAction)&&e.assetPath.empty())++missing;
    if(missing)ImGui::TextColored({1,.65f,.15f,1},"Validation: %d effect dependency slot(s) are empty.",missing);else ImGui::TextColored({.35f,1,.55f,1},"Validation: ability is ready to save.");
    if(!m_status.empty())ImGui::TextWrapped("%s",m_status.c_str());if(message&&changed&&*changed)*message=m_status;ImGui::End();
}
