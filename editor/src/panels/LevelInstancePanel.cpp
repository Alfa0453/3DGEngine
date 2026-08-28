#include "LevelInstancePanel.h"
#include "EditorPanels.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

namespace {
std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return value;
}
std::filesystem::path AbsoluteFrom(const std::string& path, const std::string& base) {
    std::filesystem::path p(path);
    if (p.is_relative() && !base.empty()) p = std::filesystem::path(base) / p;
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(p, ec);
    return (ec ? p : absolute).lexically_normal();
}
bool SamePath(const std::string& a, const std::string& b, const std::string& base) {
    if (a.empty() || b.empty()) return false;
    return Lower(AbsoluteFrom(a, base).string()) == Lower(AbsoluteFrom(b, base).string());
}
void EditPlacement(engine::LevelRef& level) {
    glm::vec3 translation(level.worldTransform[3]);
    glm::vec3 sx(level.worldTransform[0]), sy(level.worldTransform[1]), sz(level.worldTransform[2]);
    glm::vec3 scale(glm::length(sx), glm::length(sy), glm::length(sz));
    if (scale.x < .0001f) scale.x = 1; if (scale.y < .0001f) scale.y = 1; if (scale.z < .0001f) scale.z = 1;
    glm::mat3 basis(sx / scale.x, sy / scale.y, sz / scale.z);
    glm::vec3 rotation = glm::degrees(glm::eulerAngles(glm::normalize(glm::quat_cast(basis))));
    bool changed = ImGui::DragFloat3("Location", &translation.x, .1f);
    changed |= ImGui::DragFloat3("Rotation", &rotation.x, .25f, -360, 360, "%.2f deg");
    changed |= ImGui::DragFloat3("Scale", &scale.x, .01f, .001f, 1000);
    if (changed) level.worldTransform = glm::translate(glm::mat4(1), translation)
        * glm::mat4_cast(glm::quat(glm::radians(rotation))) * glm::scale(glm::mat4(1), scale);
}
}

void LevelInstancePanel::Normalize(engine::LevelRef& level) {
    level.loadRadius = std::clamp(level.loadRadius, 0.0f, 100000.0f);
    level.unloadRadius = std::clamp(level.unloadRadius, level.loadRadius, 100000.0f);
    const glm::vec3 oldMin = level.boundsMin;
    const glm::vec3 oldMax = level.boundsMax;
    level.boundsMin = glm::min(oldMin, oldMax);
    level.boundsMax = glm::max(oldMin, oldMax);
}

std::vector<LevelInstancePanel::Issue> LevelInstancePanel::Validate(
    const engine::WorldManifest& world, const std::string& worldPath,
    const std::string& currentScenePath) {
    std::vector<Issue> issues;
    const std::string base = std::filesystem::path(worldPath).parent_path().string();
    for (std::size_t i=0;i<world.levels.size();++i) {
        const auto& level=world.levels[i];
        if (level.scenePath.empty()) issues.push_back({static_cast<int>(i), "No source level selected."});
        else {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(AbsoluteFrom(level.scenePath, base), ec))
                issues.push_back({static_cast<int>(i), "Source level is missing: " + level.scenePath});
            if (SamePath(level.scenePath, currentScenePath, base)
                || SamePath(level.scenePath, world.persistentScenePath, base))
                issues.push_back({static_cast<int>(i), "Source points at the persistent/current level (self reference)."});
        }
        if (level.unloadRadius < level.loadRadius)
            issues.push_back({static_cast<int>(i), "Unload radius must be greater than or equal to load radius."});
    }
    return issues;
}

void LevelInstancePanel::RefreshScenes(const std::string& root) {
    m_scannedRoot=root; m_scenes.clear(); std::error_code ec;
    for(std::filesystem::recursive_directory_iterator it(root,std::filesystem::directory_options::skip_permission_denied,ec),end;it!=end;it.increment(ec)) {
        if(ec || !it->is_regular_file(ec) || Lower(it->path().extension().string())!=".scene") continue;
        m_scenes.push_back({it->path().string(), std::filesystem::relative(it->path(),root,ec).generic_string()}); ec.clear();
    }
    std::sort(m_scenes.begin(),m_scenes.end(),[](const auto&a,const auto&b){return Lower(a.label)<Lower(b.label);});
}

void LevelInstancePanel::SceneCombo(const char* label,std::string& path) {
    const std::string preview=path.empty()?"Choose a saved level...":std::filesystem::path(path).filename().string();
    if(!ImGui::BeginCombo(label,preview.c_str()))return;
    for(const auto& scene:m_scenes){
        ImGui::PushID(scene.path.c_str());
        if(ImGui::Selectable(scene.label.c_str(),SamePath(scene.path,path,m_scannedRoot)))path=scene.path;
        ImGui::PopID();
    }
    ImGui::EndCombo();
}

LevelInstancePanel::Result LevelInstancePanel::Draw(engine::WorldManifest& world,
    const std::string& root,const std::string& currentScenePath,int selectionCount,bool* open) {
    Result result; if(m_scannedRoot!=root)RefreshScenes(root);
    if(!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::LevelInstances),open)){ImGui::End();return result;}
    ImGui::TextDisabled("Reusable, linked level scenes. Source edits propagate to every instance.");
    if(ImGui::Button("Refresh Levels"))RefreshScenes(root);ImGui::SameLine();
    if(ImGui::Button("Save World"))result.saveWorld=true;
    ImGui::SeparatorText("Create From Selection");
    ImGui::InputText("Asset Name",m_selectionName.data(),m_selectionName.size());
    ImGui::Checkbox("Replace selected objects with instance",&m_removeSelection);
    const bool canCreate=selectionCount>0 && m_selectionName[0];
    if(!canCreate)ImGui::BeginDisabled();
    if(ImGui::Button("Create Linked Level From Selection")) {
        result.createFromSelection=true; result.removeSelection=m_removeSelection;
        result.selectionScenePath=(std::filesystem::path(root)/"GameAssets"/"Levels"/(std::string(m_selectionName.data())+".scene")).string();
    }
    if(!canCreate)ImGui::EndDisabled(); ImGui::SameLine();ImGui::TextDisabled("%d selected",selectionCount);
    ImGui::SeparatorText("Instances");
    if(ImGui::Button("+ Add Instance")){world.levels.emplace_back();m_selected=static_cast<int>(world.levels.size())-1;result.worldChanged=true;}
    ImGui::SameLine(); if(m_selected<0||m_selected>=static_cast<int>(world.levels.size()))ImGui::BeginDisabled();
    if(ImGui::Button("Duplicate")){world.levels.push_back(world.levels[static_cast<std::size_t>(m_selected)]);m_selected=static_cast<int>(world.levels.size())-1;result.worldChanged=true;}
    if(m_selected<0||m_selected>=static_cast<int>(world.levels.size()))ImGui::EndDisabled();
    if(ImGui::BeginTable("instances",3,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Source");ImGui::TableSetupColumn("Rule");ImGui::TableSetupColumn("Status");ImGui::TableHeadersRow();
        const auto issues=Validate(world,"",currentScenePath);
        for(std::size_t i=0;i<world.levels.size();++i){ImGui::PushID(static_cast<int>(i));ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
            const std::string name=world.levels[i].scenePath.empty()?"Unassigned":std::filesystem::path(world.levels[i].scenePath).filename().string();
            if(ImGui::Selectable(name.c_str(),m_selected==static_cast<int>(i),ImGuiSelectableFlags_SpanAllColumns))m_selected=static_cast<int>(i);
            ImGui::TableSetColumnIndex(1);ImGui::TextUnformatted(world.levels[i].rule==engine::LevelStreamRule::Distance?"Distance":world.levels[i].rule==engine::LevelStreamRule::AlwaysLoaded?"Always":"Manual");
            ImGui::TableSetColumnIndex(2);bool bad=false;for(const auto& issue:issues)if(issue.index==static_cast<int>(i))bad=true;ImGui::TextColored(bad?ImVec4(1,.35f,.25f,1):ImVec4(.3f,1,.5f,1),bad?"Needs attention":"Linked");ImGui::PopID();}
        ImGui::EndTable();
    }
    if(m_selected>=0&&m_selected<static_cast<int>(world.levels.size())){
        auto& level=world.levels[static_cast<std::size_t>(m_selected)];ImGui::SeparatorText("Selected Instance");
        result.worldChanged|=ImGui::Checkbox("Enabled",&level.enabled);
        const std::string before=level.scenePath;SceneCombo("Source Level",level.scenePath);result.worldChanged|=before!=level.scenePath;EditPlacement(level);
        std::array<char,96> layer{};std::snprintf(layer.data(),layer.size(),"%s",level.dataLayer.c_str());
        if(ImGui::InputText("Data Layer",layer.data(),layer.size())){level.dataLayer=layer.data();result.worldChanged=true;}
        result.worldChanged|=ImGui::DragInt("Streaming Priority",&level.streamingPriority,1,-1000,1000);
        int rule=static_cast<int>(level.rule);if(ImGui::Combo("Streaming Rule",&rule,"Distance\0Always Loaded\0Manual\0")){level.rule=static_cast<engine::LevelStreamRule>(rule);result.worldChanged=true;}
        if(level.rule==engine::LevelStreamRule::Distance){result.worldChanged|=ImGui::DragFloat("Load Radius",&level.loadRadius,.5f,0,100000,"%.1f m");result.worldChanged|=ImGui::DragFloat("Unload Radius",&level.unloadRadius,.5f,0,100000,"%.1f m");}
        Normalize(level);if(ImGui::Button("Open Source"))result.openSource=m_selected;ImGui::SameLine();if(ImGui::Button("Break Into Editable Objects"))result.breakInstance=m_selected;ImGui::SameLine();
        if(ImGui::Button("Remove Instance")){world.levels.erase(world.levels.begin()+m_selected);m_selected=std::min(m_selected,static_cast<int>(world.levels.size())-1);result.worldChanged=true;}
    }
    const auto issues=Validate(world,"",currentScenePath);if(!issues.empty()){ImGui::SeparatorText("Validation");for(const auto& issue:issues)ImGui::BulletText("Instance %d: %s",issue.index+1,issue.message.c_str());}
    if(!m_status.empty())ImGui::TextWrapped("%s",m_status.c_str());ImGui::End();return result;
}
