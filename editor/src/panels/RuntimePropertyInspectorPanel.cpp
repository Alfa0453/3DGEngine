#include "RuntimePropertyInspectorPanel.h"

#include <imgui.h>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
bool ContainsNoCase(const std::string& text, const char* filter) {
    if (!filter || !*filter) return true;
    std::string a=text,b=filter;
    std::transform(a.begin(),a.end(),a.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    std::transform(b.begin(),b.end(),b.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return a.find(b)!=std::string::npos;
}
bool Different(float a,float b){return std::abs(a-b)>.0001f;}
bool Different(const glm::vec3& a,const glm::vec3& b){return glm::any(glm::greaterThan(glm::abs(a-b),glm::vec3(.0001f)));}

void StartChanged(bool changed) { if(changed) ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(.34f,.20f,.05f,1)); }
void EndChanged(bool changed) { if(changed) ImGui::PopStyleColor(); }
void BaselineVec3(const glm::vec3& value) { ImGui::TextDisabled("Start: %.3f, %.3f, %.3f",value.x,value.y,value.z); }
void BaselineFloat(float value) { ImGui::TextDisabled("Start: %.3f",value); }
}

void RuntimePropertyInspectorPanel::BeginPlay(const engine::ecs::Registry& registry,
    const std::unordered_map<engine::ecs::Entity,std::string>& names) {
    EndPlay();
    for(const auto& [entity,name]:names) if(registry.Valid(entity)) m_start.emplace(entity,RuntimeEntitySnapshot::Capture(registry,entity));
    if(!names.empty()) m_selected=names.begin()->first;
}
void RuntimePropertyInspectorPanel::EndPlay(){m_start.clear();m_history.clear();m_selected=engine::ecs::kNull;}
void RuntimePropertyInspectorPanel::EnsureSnapshot(const engine::ecs::Registry& registry,engine::ecs::Entity entity){
    if(registry.Valid(entity)&&m_start.find(entity)==m_start.end())m_start.emplace(entity,RuntimeEntitySnapshot::Capture(registry,entity));
}
void RuntimePropertyInspectorPanel::Record(const std::string& entity,const std::string& property){
    m_history.push_front({entity,property});if(m_history.size()>64)m_history.pop_back();
}

void RuntimePropertyInspectorPanel::Draw(engine::ecs::Registry* registry,
    const std::unordered_map<engine::ecs::Entity,std::string>& names,
    bool& paused,bool& stepRequested,bool* open) {
    if(!ImGui::Begin("Runtime Property Inspector",open)){ImGui::End();return;}
    if(!registry){ImGui::TextDisabled("Enter Play mode to inspect live entities.");ImGui::End();return;}

    if(ImGui::Button(paused?"Resume Play":"Pause Play"))paused=!paused;
    ImGui::SameLine();ImGui::BeginDisabled(!paused);
    if(ImGui::Button("Step One Frame"))stepRequested=true;
    ImGui::EndDisabled();ImGui::SameLine();
    ImGui::TextColored(paused?ImVec4(1,.75f,.2f,1):ImVec4(.3f,1,.45f,1),paused?"PAUSED":"RUNNING");
    ImGui::Separator();

    std::vector<std::pair<engine::ecs::Entity,std::string>> entities;
    entities.reserve(names.size()+8);
    for(const auto& [e,n]:names)if(registry->Valid(e))entities.push_back({e,n});
    auto addEntity=[&](engine::ecs::Entity e){if(!registry->Valid(e)||std::any_of(entities.begin(),entities.end(),[e](const auto& v){return v.first==e;}))return;
        std::string label="Entity "+std::to_string(e);if(auto* n=registry->TryGet<engine::ecs::RuntimeName>(e);n&&!n->value.empty())label=n->value;entities.push_back({e,std::move(label)});};
    auto addPool=[&](const auto* pool){if(pool)for(auto e:pool->dense)addEntity(e);};
    addPool(registry->TryPool<engine::ecs::RuntimeName>());
    addPool(registry->TryPool<engine::ecs::Transform>());
    addPool(registry->TryPool<engine::Health>());
    addPool(registry->TryPool<engine::ecs::RigidBody>());
    addPool(registry->TryPool<engine::AbilityResource>());
    addPool(registry->TryPool<engine::Projectile>());
    std::sort(entities.begin(),entities.end(),[](const auto&a,const auto&b){return a.second<b.second;});

    ImGui::BeginChild("RuntimeEntities",ImVec2(220,0),true);
    ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##RuntimeEntityFilter","Filter entities",m_entityFilter.data(),m_entityFilter.size());
    ImGui::TextDisabled("%d live entities",static_cast<int>(entities.size()));
    for(const auto& [e,n]:entities){if(!ContainsNoCase(n,m_entityFilter.data()))continue;EnsureSnapshot(*registry,e);
        bool changed=m_start[e].Changed(*registry,e);ImGui::PushID(static_cast<int>(e));
        if(changed)ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.72f,.2f,1));
        if(ImGui::Selectable((n+"##runtime").c_str(),m_selected==e))m_selected=e;
        if(changed)ImGui::PopStyleColor();ImGui::PopID();
    }
    ImGui::EndChild();ImGui::SameLine();
    ImGui::BeginChild("RuntimeProperties",ImVec2(0,0),true);
    if(m_selected==engine::ecs::kNull||!registry->Valid(m_selected)){ImGui::TextDisabled("Select a live entity.");ImGui::EndChild();ImGui::End();return;}
    EnsureSnapshot(*registry,m_selected);auto& start=m_start[m_selected];
    std::string entityName="Entity "+std::to_string(m_selected);if(auto* n=registry->TryGet<engine::ecs::RuntimeName>(m_selected))entityName=n->value;
    ImGui::TextUnformatted(entityName.c_str());ImGui::SameLine();
    if(start.Changed(*registry,m_selected)){ImGui::TextColored(ImVec4(1,.72f,.2f,1),"Modified at runtime");ImGui::SameLine();if(ImGui::SmallButton("Reset Entity")){start.Restore(*registry,m_selected);Record(entityName,"Reset entity");}}
    else ImGui::TextDisabled("Matches Play start");
    ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##RuntimePropertyFilter","Filter components or properties",m_propertyFilter.data(),m_propertyFilter.size());

    auto section=[&](const char* label,RuntimeEntitySnapshot::Component component){
        const char* fields="";
        switch(component){
        case RuntimeEntitySnapshot::Component::Transform: fields="position rotation scale";break;
        case RuntimeEntitySnapshot::Component::LinearVelocity: fields="velocity";break;
        case RuntimeEntitySnapshot::Component::AngularVelocity: fields="axis radians speed";break;
        case RuntimeEntitySnapshot::Component::RigidBody: fields="velocity mass gravity kinematic sleeping damping";break;
        case RuntimeEntitySnapshot::Component::Collider: fields="radius extents height trigger restitution friction layer mask";break;
        case RuntimeEntitySnapshot::Component::Health: fields="hp max alive";break;
        case RuntimeEntitySnapshot::Component::Rotator: fields="axis degrees speed";break;
        case RuntimeEntitySnapshot::Component::Mover: fields="axis distance speed phase origin";break;
        case RuntimeEntitySnapshot::Component::AbilityResource: fields="mana stamina maximum";break;
        case RuntimeEntitySnapshot::Component::Projectile: fields="direction speed range damage radius";break;
        default:break;
        }
        if(!ContainsNoCase(label,m_propertyFilter.data())&&!ContainsNoCase(fields,m_propertyFilter.data())&&m_propertyFilter[0])return false;
        bool changed=start.Changed(*registry,m_selected,component);if(changed)ImGui::PushStyleColor(ImGuiCol_Header,ImVec4(.34f,.20f,.05f,1));
        bool visible=ImGui::CollapsingHeader(label,ImGuiTreeNodeFlags_DefaultOpen);if(changed)ImGui::PopStyleColor();
        if(visible&&changed){ImGui::SameLine();ImGui::PushID(label);if(ImGui::SmallButton("Reset Component")){start.Restore(*registry,m_selected,component);Record(entityName,std::string("Reset ")+label);}ImGui::PopID();}
        return visible;
    };
    auto edited=[&](const char* property){if(ImGui::IsItemDeactivatedAfterEdit())Record(entityName,property);};
    auto resetField=[&](const char* id,auto&& action){ImGui::SameLine();ImGui::PushID(id);if(ImGui::SmallButton("Reset")){action();Record(entityName,std::string("Reset ")+id);}ImGui::PopID();};

    if(auto* v=registry->TryGet<engine::ecs::Transform>(m_selected);v&&start.transform&&section("Transform",RuntimeEntitySnapshot::Component::Transform)){
        auto& b=*start.transform;bool d=Different(v->position,b.position);StartChanged(d);ImGui::DragFloat3("Position",&v->position.x,.02f);EndChanged(d);edited("Transform.Position");BaselineVec3(b.position);resetField("Transform.Position",[&]{v->position=b.position;});
        glm::vec3 deg=glm::degrees(glm::eulerAngles(v->rotation)),baseDeg=glm::degrees(glm::eulerAngles(b.rotation));d=Different(deg,baseDeg);StartChanged(d);if(ImGui::DragFloat3("Rotation",&deg.x,.25f))v->rotation=glm::quat(glm::radians(deg));EndChanged(d);edited("Transform.Rotation");BaselineVec3(baseDeg);resetField("Transform.Rotation",[&]{v->rotation=b.rotation;});
        d=Different(v->scale,b.scale);StartChanged(d);ImGui::DragFloat3("Scale",&v->scale.x,.01f,.001f,10000.f);EndChanged(d);edited("Transform.Scale");BaselineVec3(b.scale);resetField("Transform.Scale",[&]{v->scale=b.scale;});
    }
    if(auto* v=registry->TryGet<engine::Health>(m_selected);v&&start.health&&section("Health",RuntimeEntitySnapshot::Component::Health)){
        auto& b=*start.health;bool d=Different(v->hp,b.hp);StartChanged(d);ImGui::DragFloat("HP",&v->hp,.1f,0,v->maxHp);EndChanged(d);edited("Health.HP");BaselineFloat(b.hp);resetField("Health.HP",[&]{v->hp=b.hp;});
        d=Different(v->maxHp,b.maxHp);StartChanged(d);ImGui::DragFloat("Max HP",&v->maxHp,.1f,.001f,100000);EndChanged(d);edited("Health.MaxHP");BaselineFloat(b.maxHp);resetField("Health.MaxHP",[&]{v->maxHp=b.maxHp;});
        d=v->alive!=b.alive;StartChanged(d);ImGui::Checkbox("Alive",&v->alive);EndChanged(d);edited("Health.Alive");ImGui::TextDisabled("Start: %s",b.alive?"true":"false");resetField("Health.Alive",[&]{v->alive=b.alive;});
    }
    if(auto* v=registry->TryGet<engine::ecs::RigidBody>(m_selected);v&&start.rigidBody&&section("Rigid Body",RuntimeEntitySnapshot::Component::RigidBody)){
        auto& b=*start.rigidBody;bool d=Different(v->velocity,b.velocity);StartChanged(d);ImGui::DragFloat3("Velocity",&v->velocity.x,.02f);EndChanged(d);edited("RigidBody.Velocity");BaselineVec3(b.velocity);
        float mass=v->invMass>0?1.f/v->invMass:0,baseMass=b.invMass>0?1.f/b.invMass:0;d=Different(mass,baseMass);StartChanged(d);if(ImGui::DragFloat("Mass",&mass,.1f,0,100000))v->invMass=mass>0?1.f/mass:0;EndChanged(d);edited("RigidBody.Mass");BaselineFloat(baseMass);
        d=v->useGravity!=b.useGravity;StartChanged(d);ImGui::Checkbox("Use Gravity",&v->useGravity);EndChanged(d);edited("RigidBody.UseGravity");
        d=v->kinematic!=b.kinematic;StartChanged(d);ImGui::Checkbox("Kinematic",&v->kinematic);EndChanged(d);edited("RigidBody.Kinematic");
        ImGui::Checkbox("Sleeping",&v->sleeping);edited("RigidBody.Sleeping");
    }
    if(auto* v=registry->TryGet<engine::ecs::Collider>(m_selected);v&&start.collider&&section("Collider",RuntimeEntitySnapshot::Component::Collider)){
        auto& b=*start.collider;bool d=Different(v->radius,b.radius);StartChanged(d);ImGui::DragFloat("Radius",&v->radius,.01f,0,10000);EndChanged(d);edited("Collider.Radius");BaselineFloat(b.radius);
        d=Different(v->halfExtents,b.halfExtents);StartChanged(d);ImGui::DragFloat3("Half Extents",&v->halfExtents.x,.01f,0,10000);EndChanged(d);edited("Collider.HalfExtents");BaselineVec3(b.halfExtents);
        d=Different(v->halfHeight,b.halfHeight);StartChanged(d);ImGui::DragFloat("Half Height",&v->halfHeight,.01f,0,10000);EndChanged(d);edited("Collider.HalfHeight");BaselineFloat(b.halfHeight);
        ImGui::Checkbox("Trigger",&v->isTrigger);edited("Collider.Trigger");
        ImGui::DragFloat("Restitution",&v->restitution,.01f,0,1);edited("Collider.Restitution");
        ImGui::DragFloat("Friction",&v->friction,.01f,0,10);edited("Collider.Friction");
    }
    if(auto* v=registry->TryGet<engine::ecs::LinearVelocity>(m_selected);v&&start.linearVelocity&&section("Linear Velocity",RuntimeEntitySnapshot::Component::LinearVelocity)){
        bool d=Different(v->velocity,start.linearVelocity->velocity);StartChanged(d);ImGui::DragFloat3("Velocity##Linear",&v->velocity.x,.02f);EndChanged(d);edited("LinearVelocity.Velocity");BaselineVec3(start.linearVelocity->velocity);
    }
    if(auto* v=registry->TryGet<engine::ecs::Rotator>(m_selected);v&&start.rotator&&section("Rotator",RuntimeEntitySnapshot::Component::Rotator)){
        ImGui::DragFloat3("Axis##Rotator",&v->axis.x,.01f,-1,1);edited("Rotator.Axis");float degrees=glm::degrees(v->radiansPerSecond);if(ImGui::DragFloat("Degrees / Second",&degrees,.1f,-10000,10000))v->radiansPerSecond=glm::radians(degrees);edited("Rotator.Speed");
    }
    if(auto* v=registry->TryGet<engine::ecs::Mover>(m_selected);v&&start.mover&&section("Mover",RuntimeEntitySnapshot::Component::Mover)){
        ImGui::DragFloat3("Axis##Mover",&v->axis.x,.01f,-1,1);edited("Mover.Axis");ImGui::DragFloat("Distance",&v->distance,.02f,0,10000);edited("Mover.Distance");ImGui::DragFloat("Speed##Mover",&v->speed,.02f,0,10000);edited("Mover.Speed");ImGui::DragFloat("Phase",&v->phase,.01f);edited("Mover.Phase");
    }
    if(auto* v=registry->TryGet<engine::AbilityResource>(m_selected);v&&start.abilityResource&&section("Ability Resources",RuntimeEntitySnapshot::Component::AbilityResource)){
        ImGui::DragFloat("Mana",&v->mana,.1f,0,v->maxMana);edited("Ability.Mana");ImGui::DragFloat("Max Mana",&v->maxMana,.1f,0,100000);edited("Ability.MaxMana");ImGui::DragFloat("Stamina",&v->stamina,.1f,0,v->maxStamina);edited("Ability.Stamina");ImGui::DragFloat("Max Stamina",&v->maxStamina,.1f,0,100000);edited("Ability.MaxStamina");
    }
    if(auto* v=registry->TryGet<engine::Projectile>(m_selected);v&&start.projectile&&section("Projectile",RuntimeEntitySnapshot::Component::Projectile)){
        ImGui::DragFloat3("Direction",&v->dir.x,.01f,-1,1);edited("Projectile.Direction");ImGui::DragFloat("Speed##Projectile",&v->speed,.1f,0,100000);edited("Projectile.Speed");ImGui::DragFloat("Range",&v->range,.1f,0,100000);edited("Projectile.Range");ImGui::DragFloat("Damage",&v->damage,.1f,0,100000);edited("Projectile.Damage");
    }
    if(ImGui::CollapsingHeader("Runtime Edit History")){
        if(!m_history.empty()){if(ImGui::SmallButton("Clear History"))m_history.clear();}
        if(m_history.empty())ImGui::TextDisabled("No runtime edits this Play session.");
        for(const auto& h:m_history)ImGui::BulletText("%s - %s",h.entity.c_str(),h.property.c_str());
    }
    ImGui::EndChild();ImGui::End();
}
