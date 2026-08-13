#include "DayNightTimelinePanel.h"

#include "EditorPanels.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <filesystem>

#include <imgui.h>

namespace {
std::string Safe(std::string value) {
    for (char& c : value) if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') c = '_';
    return value.empty() ? "DayNightTimeline" : value;
}
const char* FileName(const std::string& path) {
    static std::string value; value = path.empty() ? "None" : std::filesystem::path(path).filename().string(); return value.c_str();
}
void Color3(const char* label, glm::vec3& value, bool& changed) { changed |= ImGui::ColorEdit3(label, &value.x); }
}

void DayNightTimelinePanel::NewTimeline(const std::string& root) {
    m_timeline = {}; m_timeline.name = "NewDayNightTimeline";
    auto make = [](float time, float sun, float sky, glm::vec3 fog, float clouds, const char* event) {
        engine::DayNightKeyframe k; k.time=time;k.sunIntensity=sun;k.skyLightIntensity=sky;
        k.skyIntensity=sky;k.fogColor=fog;k.cloudCoverage=clouds;k.eventName=event;return k;
    };
    m_timeline.keys = {
        make(0.00f,0.05f,0.12f,{0.05f,0.07f,0.14f},0.30f,"Midnight"),
        make(0.25f,0.35f,0.45f,{0.72f,0.48f,0.34f},0.42f,"Sunrise"),
        make(0.50f,1.25f,1.00f,{0.58f,0.68f,0.80f},0.20f,"Noon"),
        make(0.75f,0.30f,0.42f,{0.76f,0.38f,0.28f},0.45f,"Sunset")};
    m_path = (std::filesystem::path(root) / "GameAssets" / "Timelines" /
              "NewDayNightTimeline.3dgdaynight").string();
    m_time = 0.25f; m_selectedKey = 1; m_playing = false; m_dirty = true;
}

void DayNightTimelinePanel::CaptureKey(const EditorScene::Environment& e,
                                       engine::DayNightKeyframe& k) const {
    k.time=m_time;k.skyIntensity=e.skyIntensity;k.skyLightIntensity=e.skyLightIntensity;
    k.sunIntensity=e.sunIntensity;k.cloudCoverage=e.cloudCoverage;k.cloudDensity=e.cloudDensity;
    k.cloudWindSpeed=e.cloudWindSpeed;k.cloudWindDirection=e.cloudWindDirection;
    k.cloudColor=e.cloudColor;k.fogDensity=e.fogDensity;k.fogHeight=e.fogHeight;
    k.fogHeightFalloff=e.fogHeightFalloff;k.fogColor=e.fogColor;
}

void DayNightTimelinePanel::ApplySample(EditorScene& scene) const {
    const auto k = engine::SampleDayNightTimeline(m_timeline,m_time); auto e=scene.GetEnvironment();
    e.timeOfDay=m_time;e.skyIntensity=k.skyIntensity;e.skyLightIntensity=k.skyLightIntensity;
    e.sunIntensity=k.sunIntensity;e.cloudCoverage=k.cloudCoverage;e.cloudDensity=k.cloudDensity;
    e.cloudWindSpeed=k.cloudWindSpeed;e.cloudWindDirection=k.cloudWindDirection;
    e.cloudColor=k.cloudColor;e.fogDensity=k.fogDensity;e.fogHeight=k.fogHeight;
    e.fogHeightFalloff=k.fogHeightFalloff;e.fogColor=k.fogColor;scene.SetEnvironment(e);
}

void DayNightTimelinePanel::DrawPreview() const {
    ImVec2 size(ImGui::GetContentRegionAvail().x,std::max(190.0f,ImGui::GetContentRegionAvail().y));
    ImGui::InvisibleButton("##DayNightPreview",size); const ImVec2 p=ImGui::GetItemRectMin();
    auto* d=ImGui::GetWindowDrawList(); const auto k=engine::SampleDayNightTimeline(m_timeline,m_time);
    const float sun=std::max(0.0f,std::sin(m_time*6.2831853f-1.5707963f));
    const ImVec4 sky(std::clamp(k.fogColor.r*.3f+sun*.35f,0.f,1.f),std::clamp(k.fogColor.g*.3f+sun*.48f,0.f,1.f),std::clamp(k.fogColor.b*.4f+sun*.65f,0.f,1.f),1);
    d->AddRectFilled(p,{p.x+size.x,p.y+size.y},ImGui::ColorConvertFloat4ToU32(sky));
    const float angle=m_time*6.2831853f-1.5707963f;const ImVec2 center(p.x+size.x*.5f,p.y+size.y*.7f);
    const ImVec2 sunPos(center.x+std::cos(angle)*size.x*.38f,center.y-std::sin(angle)*size.y*.52f);
    d->AddCircleFilled(sunPos,18,IM_COL32(255,220,130,255),32);
    d->AddRectFilled({p.x,p.y+size.y*.72f},{p.x+size.x,p.y+size.y},IM_COL32(35,52,44,255));
    char clock[64];const int minutes=static_cast<int>(m_time*1440.0f)%1440;
    std::snprintf(clock,sizeof(clock),"%02d:%02d  |  %.1f s day",minutes/60,minutes%60,m_timeline.dayLengthSeconds);
    d->AddText({p.x+12,p.y+12},IM_COL32_WHITE,clock);
}

DayNightTimelinePanel::Result DayNightTimelinePanel::Draw(EditorScene& scene, EditorAssets& assets,
                                                          const std::string& root, bool* open) {
    Result result;
    if (!m_pendingOpen.empty()) { std::string error; if(engine::LoadDayNightTimelineAsset(m_pendingOpen,&m_timeline,&error)){m_path=m_pendingOpen;m_time=m_timeline.keys.front().time;m_selectedKey=0;m_dirty=false;m_status="Loaded "+m_pendingOpen;}else m_status=error;m_pendingOpen.clear(); }
    if (m_timeline.keys.empty()) NewTimeline(root);
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::DayNightTimeline),open)){ImGui::End();return result;}
    if(ImGui::Button("New"))NewTimeline(root);ImGui::SameLine();
    if(ImGui::Button("Save")){if(m_path.empty())m_path=(std::filesystem::path(root)/"GameAssets"/"Timelines"/(Safe(m_timeline.name)+".3dgdaynight")).string();std::string error;if(engine::SaveDayNightTimelineAsset(m_path,m_timeline,&error)){m_dirty=false;result.saved=true;result.message="Saved day/night timeline: "+m_path;}else result.message=error;}ImGui::SameLine();
    if(ImGui::Button(m_playing?"Pause":"Play"))m_playing=!m_playing;ImGui::SameLine();
    if(ImGui::Button("Stop")){m_playing=false;m_time=m_timeline.keys.front().time;}ImGui::SameLine();
    if(ImGui::Button("Use as Level Default")){
        if(m_path.empty()||m_dirty)m_status="Save the timeline before assigning it to the level.";
        else{auto environment=scene.GetEnvironment();environment.dayNightTimelinePath=m_path;
            environment.dayNightTimelineId=assets.AssetIdForPath(m_path);
            environment.dayNightTimelineAutoplay=m_timeline.autoplay;scene.SetEnvironment(environment);
            m_status="Assigned timeline as the active level default.";}
    }ImGui::SameLine();
    ImGui::Checkbox("Preview in Level",&m_previewInLevel);ImGui::SameLine();ImGui::TextDisabled("%s",m_dirty?"Unsaved":"Saved");
    if(!m_status.empty())ImGui::TextDisabled("%s",m_status.c_str());
    char timelineName[128]{};std::snprintf(timelineName,sizeof(timelineName),"%s",m_timeline.name.c_str());
    ImGui::SetNextItemWidth(260);if(ImGui::InputText("Name",timelineName,sizeof(timelineName))){m_timeline.name=timelineName;m_dirty=true;}
    m_dirty|=ImGui::DragFloat("Day Length",&m_timeline.dayLengthSeconds,1.0f,1.0f,86400.0f,"%.1f s");
    m_dirty|=ImGui::DragFloat("Playback Rate",&m_timeline.playbackRate,0.05f,0.0f,100.0f,"%.2fx");ImGui::SameLine();m_dirty|=ImGui::Checkbox("Loop",&m_timeline.loop);ImGui::SameLine();m_dirty|=ImGui::Checkbox("Autoplay",&m_timeline.autoplay);
    if(m_playing){m_time+=ImGui::GetIO().DeltaTime*m_timeline.playbackRate/std::max(m_timeline.dayLengthSeconds,1.f);if(m_time>=1){if(m_timeline.loop)m_time=std::fmod(m_time,1.f);else{m_time=0.999999f;m_playing=false;}}}
    bool scrub=ImGui::SliderFloat("24 Hour Track",&m_time,0,0.999999f,"%.3f");
    ImGui::Separator();
    const float details=365.0f;
    if(ImGui::BeginChild("##TimelineKeys",{details,0},true)){
        if(ImGui::Button("Add Key")){engine::DayNightKeyframe k=engine::SampleDayNightTimeline(m_timeline,m_time);k.time=m_time;k.eventName.clear();m_timeline.keys.push_back(k);engine::NormalizeDayNightTimeline(m_timeline);for(int i=0;i<(int)m_timeline.keys.size();++i)if(std::abs(m_timeline.keys[i].time-m_time)<.0001f)m_selectedKey=i;m_dirty=true;}ImGui::SameLine();
        if(ImGui::Button("Capture Level")&&m_selectedKey>=0&&m_selectedKey<(int)m_timeline.keys.size()){CaptureKey(scene.GetEnvironment(),m_timeline.keys[m_selectedKey]);m_dirty=true;}
        for(int i=0;i<(int)m_timeline.keys.size();++i){ImGui::PushID(i);char label[96];const int min=(int)(m_timeline.keys[i].time*1440)%1440;std::snprintf(label,sizeof(label),"%02d:%02d  %s",min/60,min%60,m_timeline.keys[i].eventName.c_str());if(ImGui::Selectable(label,m_selectedKey==i)){m_selectedKey=i;m_time=m_timeline.keys[i].time;scrub=true;}ImGui::PopID();}
        if(m_selectedKey>=0&&m_selectedKey<(int)m_timeline.keys.size()){
            auto& k=m_timeline.keys[m_selectedKey];ImGui::Separator();bool changed=false;
            changed|=ImGui::SliderFloat("Key Time",&k.time,0,0.999999f);changed|=ImGui::DragFloat("Sun",&k.sunIntensity,.02f,0,16);changed|=ImGui::DragFloat("Sky Light",&k.skyLightIntensity,.02f,0,8);changed|=ImGui::DragFloat("Sky Intensity",&k.skyIntensity,.02f,0,16);
            changed|=ImGui::SliderFloat("Cloud Coverage",&k.cloudCoverage,0,1);changed|=ImGui::DragFloat("Cloud Density",&k.cloudDensity,.01f,0,4);changed|=ImGui::DragFloat("Cloud Wind",&k.cloudWindSpeed,.005f,0,4);changed|=ImGui::SliderFloat("Cloud Direction",&k.cloudWindDirection,0,360);Color3("Cloud Color",k.cloudColor,changed);
            changed|=ImGui::DragFloat("Fog Density",&k.fogDensity,.001f,0,1,"%.4f");changed|=ImGui::DragFloat("Fog Height",&k.fogHeight,.05f,-10000,10000);changed|=ImGui::DragFloat("Fog Falloff",&k.fogHeightFalloff,.01f,0,10);Color3("Fog Color",k.fogColor,changed);
            changed|=ImGui::DragFloat("Wind Strength",&k.windStrength,.01f,0,4);changed|=ImGui::SliderFloat("Wind Direction",&k.windDirection,0,360);
            const auto audio=assets.ContentAssetPaths(EditorAssets::Type::Audio);if(ImGui::BeginCombo("Ambient Audio",FileName(k.ambientAudioPath))){if(ImGui::Selectable("None",k.ambientAudioPath.empty())){k.ambientAudioPath.clear();k.ambientAudioId={};changed=true;}for(const auto&p:audio)if(ImGui::Selectable(FileName(p),p==k.ambientAudioPath)){k.ambientAudioPath=p;k.ambientAudioId=assets.AssetIdForPath(p);changed=true;}ImGui::EndCombo();}
            char event[128]{};std::snprintf(event,sizeof(event),"%s",k.eventName.c_str());if(ImGui::InputText("Event",event,sizeof(event))){k.eventName=event;changed=true;}
            if(ImGui::Button("Delete Key")&&m_timeline.keys.size()>1){m_timeline.keys.erase(m_timeline.keys.begin()+m_selectedKey);m_selectedKey=std::clamp(m_selectedKey,0,(int)m_timeline.keys.size()-1);changed=true;}
            if(changed){engine::NormalizeDayNightTimeline(m_timeline);m_dirty=true;scrub=true;}
        }
    }ImGui::EndChild();ImGui::SameLine();if(ImGui::BeginChild("##TimelinePreview",{0,0},true))DrawPreview();ImGui::EndChild();
    if((scrub||m_playing)&&m_previewInLevel)ApplySample(scene);
    ImGui::End();return result;
}
