#pragma once

// =============================================================================
// Visual Scripting — Pass 4 : production gameplay node bindings.
//
// Follows the Phase 20 rule: these nodes are GENERATED from Script API metadata
// (registered into script::ScriptApiRegistry, so Pass 2's GenerateFunctionNodes
// emits them) and their runtime call is a thin invoker that forwards to the
// existing engine::Script gameplay API through the per-entity VisualScriptHost.
// No combat/ability/inventory/dialogue/audio/... logic is reimplemented here
// (Phases 8/10–19) — the nodes orchestrate the current runtime systems.
//
// Ordering: call RegisterVisualScriptGameplay() BEFORE RegisterVisualScriptPass2()
// so the descriptors exist when the function-node generator runs.
// =============================================================================

#include "VisualScriptInstance.h"     // VisualScriptHost (public gameplay wrappers)
#include "VisualScriptReflection.h"   // VisualScriptFunctionBindings, FromScriptFieldType

#include <engine/gameplay/ScriptReflection.h>

#include <string>
#include <vector>

namespace engine::vs {

namespace detail {

inline void RegisterGameplayFn(const char* name, const char* category,
                               script::ScriptFieldType returnType, bool hasReturn,
                               std::vector<script::ScriptParam> params, NodeExecutor invoker) {
    script::ScriptFunctionDescriptor d;
    d.name = name;
    d.category = category;
    d.returnType = returnType;
    d.hasReturn = hasReturn;
    d.params = std::move(params);
    d.flags = script::SFF_VisualScriptVisible | script::SFF_RuntimeOnly | script::SFF_NativeVisible;
    script::ScriptApiRegistry::Get().RegisterFunction(d);              // id = StableId(name)
    VisualScriptFunctionBindings::Get().Bind(script::StableId(name), std::move(invoker));
}

inline VisualScriptHost* Host(INodeContext& c) {
    VisualScriptHost* h = c.ScriptHost();
    if (!h) c.Fail("Gameplay node: Script host / services unavailable");
    return h;
}

} // namespace detail

inline void RegisterVisualScriptGameplay() {
    using detail::RegisterGameplayFn;
    using detail::Host;
    using S = script::ScriptFieldType;
    using P = script::ScriptParam;

    // ---- Ability / Combat (Phase 10) --------------------------------------
    RegisterGameplayFn("Ability.Grant", "Ability", S::Bool, true, {{"AbilityAsset", S::String}},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Bool(h->VsGrantAbility(c.ReadInput("AbilityAsset").AsString()))); });
    RegisterGameplayFn("Ability.Activate", "Ability", S::Bool, true, {{"AbilityName", S::String}, {"Victim", S::Entity}},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Bool(h->VsActivateAbility(c.ReadInput("AbilityName").AsString(), c.ReadInput("Victim").AsEntity()))); });
    RegisterGameplayFn("Ability.Cancel", "Ability", S::Bool, true, {},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Bool(h->VsCancelAbility())); });
    RegisterGameplayFn("Ability.IsActive", "Ability", S::Bool, true, {{"AbilityName", S::String}},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Bool(h->VsIsAbilityActive(c.ReadInput("AbilityName").AsString()))); });
    RegisterGameplayFn("Combat.Start", "Combat", S::Bool, true, {{"Victim", S::Entity}},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Bool(h->VsStartCombat(c.ReadInput("Victim").AsEntity()))); });
    RegisterGameplayFn("Combat.DealDamage", "Combat", S::String, true, {{"Victim", S::Entity}, {"Damage", S::Float}, {"DamageType", S::String}},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Str(h->VsDealCombatDamage(c.ReadInput("Victim").AsEntity(), c.ReadInput("Damage").AsFloat(), c.ReadInput("DamageType").AsString()))); });

    // ---- Inventory (Phase 11) ---------------------------------------------
    RegisterGameplayFn("Inventory.AddItem", "Inventory", S::Bool, true, {{"ItemAsset", S::String}, {"Count", S::Int}},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Bool(h->VsAddItem(c.ReadInput("ItemAsset").AsString(), c.ReadInput("Count").AsInt()))); });
    RegisterGameplayFn("Inventory.RemoveItem", "Inventory", S::Int, true, {{"ItemName", S::String}, {"Count", S::Int}},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Int(h->VsRemoveItem(c.ReadInput("ItemName").AsString(), c.ReadInput("Count").AsInt()))); });
    RegisterGameplayFn("Inventory.HasItem", "Inventory", S::Bool, true, {{"ItemName", S::String}, {"Count", S::Int}},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Bool(h->VsHasItem(c.ReadInput("ItemName").AsString(), c.ReadInput("Count").AsInt()))); });
    RegisterGameplayFn("Inventory.ItemCount", "Inventory", S::Int, true, {{"ItemName", S::String}},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Int(h->VsItemCount(c.ReadInput("ItemName").AsString()))); });

    // ---- Quest / Dialogue (Phase 12) --------------------------------------
    RegisterGameplayFn("Quest.Start", "Quest", S::Bool, true, {{"QuestName", S::String}},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Bool(h->VsStartQuest(c.ReadInput("QuestName").AsString()))); });
    RegisterGameplayFn("Quest.Advance", "Quest", S::Bool, true, {{"QuestName", S::String}, {"Objective", S::String}, {"Amount", S::Int}},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Bool(h->VsAdvanceQuest(c.ReadInput("QuestName").AsString(), c.ReadInput("Objective").AsString(), c.ReadInput("Amount").AsInt()))); });
    RegisterGameplayFn("Dialogue.Start", "Dialogue", S::Bool, true, {{"DialogueAsset", S::String}},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Bool(h->VsStartDialogue(c.ReadInput("DialogueAsset").AsString()))); });
    RegisterGameplayFn("Dialogue.Choose", "Dialogue", S::Bool, true, {{"ChoiceIndex", S::Int}},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Bool(h->VsChooseDialogue(c.ReadInput("ChoiceIndex").AsInt()))); });
    RegisterGameplayFn("Dialogue.Continue", "Dialogue", S::Bool, true, {},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Bool(h->VsContinueDialogue())); });

    // ---- Audio / Particles / Camera (Phase 15/16) -------------------------
    RegisterGameplayFn("Audio.Play", "Audio", S::Bool, true, {},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Bool(h->VsPlayAudio())); });
    RegisterGameplayFn("Audio.Stop", "Audio", S::Bool, true, {},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Bool(h->VsStopAudio())); });
    RegisterGameplayFn("Audio.PlayCue", "Audio", S::Bool, true, {{"AudioPath", S::String}},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Bool(h->VsPlayAudioCue(c.ReadInput("AudioPath").AsString()))); });
    RegisterGameplayFn("Particles.Play", "Particles", S::Bool, true, {},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Bool(h->VsPlayParticles())); });
    RegisterGameplayFn("Particles.Stop", "Particles", S::Bool, true, {},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Bool(h->VsStopParticles())); });
    RegisterGameplayFn("Particles.Burst", "Particles", S::Bool, true, {{"Count", S::Int}},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Bool(h->VsBurstParticles(c.ReadInput("Count").AsInt()))); });
    RegisterGameplayFn("Camera.Shake", "Camera", S::Bool, true, {{"Intensity", S::Float}, {"Duration", S::Float}, {"Frequency", S::Float}},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Bool(h->VsShakeCamera(c.ReadInput("Intensity").AsFloat(), c.ReadInput("Duration").AsFloat(), c.ReadInput("Frequency").AsFloat()))); });

    // ---- Animation (Phase 7/19) -------------------------------------------
    RegisterGameplayFn("Animation.PlayAction", "Animation", S::Bool, true, {{"ClipName", S::String}},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Bool(h->VsPlayAnimationAction(c.ReadInput("ClipName").AsString()))); });
    RegisterGameplayFn("Animation.SetParameter", "Animation", S::Bool, true, {{"Name", S::String}, {"Value", S::Float}},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Bool(h->VsSetAnimationParameter(c.ReadInput("Name").AsString(), c.ReadInput("Value").AsFloat()))); });
    RegisterGameplayFn("Animation.SetTrigger", "Animation", S::Bool, true, {{"Name", S::String}},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Bool(h->VsSetAnimationTrigger(c.ReadInput("Name").AsString()))); });

    // ---- Scene / Level / Save (Phase 17/18) -------------------------------
    RegisterGameplayFn("Scene.RequestLoad", "Scene", S::Bool, false, {{"ScenePath", S::String}},
        [](INodeContext& c) { if (auto* h = Host(c)) h->VsRequestSceneLoad(c.ReadInput("ScenePath").AsString()); });   // queued, not synchronous
    RegisterGameplayFn("Level.RequestLoad", "Scene", S::Bool, false, {{"Level", S::String}},
        [](INodeContext& c) { if (auto* h = Host(c)) h->VsRequestLevelLoad(c.ReadInput("Level").AsString()); });
    RegisterGameplayFn("Save.ToSlot", "Save", S::Bool, false, {{"Slot", S::Int}},
        [](INodeContext& c) { if (auto* h = Host(c)) h->VsSaveGameToSlot(c.ReadInput("Slot").AsInt()); });
    RegisterGameplayFn("Save.Checkpoint", "Save", S::Bool, true, {{"Name", S::String}, {"Position", S::Vec3}},
        [](INodeContext& c) { if (auto* h = Host(c)) c.WriteOutput("Return", VisualValue::Bool(h->VsSaveCheckpoint(c.ReadInput("Name").AsString(), c.ReadInput("Position").AsVec3()))); });
}

} // namespace engine::vs
