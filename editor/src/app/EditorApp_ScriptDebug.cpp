// EditorApp — the Script Debug panel. In Play mode it lists every scripted entity and
// its live field values, and lets you tweak them without recompiling: edits write the
// running script's field storage, which its next GetField* call reads.

#include "EditorApp.h"
#include "EditorGeneratedScriptTools.h"
#include "EditorScriptTools.h"
#include "GameBtScripts.h"

#include <game/GameModule.h>
#include <engine/ai/BtScript.h>
#include <engine/gameplay/GameMode.h>

#include <imgui.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace {

bool IsNativeScriptSource(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".h" || extension == ".hpp" || extension == ".inl"
        || extension == ".cpp" || extension == ".cc" || extension == ".cxx";
}

std::unordered_map<std::string, std::uint64_t> ScanNativeScriptSources(
    const std::filesystem::path& contentRoot) {
    std::unordered_map<std::string, std::uint64_t> snapshot;
    const std::filesystem::path root = contentRoot / "Scripts";
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) return snapshot;

    for (std::filesystem::recursive_directory_iterator it(
             root, std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec) || !IsNativeScriptSource(it->path())) continue;
        const auto written = it->last_write_time(ec);
        if (ec) { ec.clear(); continue; }
        const std::uint64_t time = static_cast<std::uint64_t>(
            written.time_since_epoch().count());
        const std::uint64_t size = static_cast<std::uint64_t>(it->file_size(ec));
        if (ec) { ec.clear(); continue; }
        snapshot[it->path().lexically_normal().generic_string()] =
            time ^ (size + 0x9e3779b97f4a7c15ull + (time << 6) + (time >> 2));
    }
    return snapshot;
}

std::vector<std::string> ScanLuaTestSources(
    const std::filesystem::path& contentRoot) {
    std::vector<std::string> sources;
    const std::filesystem::path root = contentRoot / "Scripts";
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) return sources;

    for (std::filesystem::recursive_directory_iterator it(
             root, std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string extension = it->path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".lua")
            sources.push_back(it->path().lexically_normal().string());
    }
    std::sort(sources.begin(), sources.end());
    return sources;
}

// Render one script slot's fields as live-editable widgets. Mutates slot.fields[i].value
// directly (the running script reads these on its next update).
void DrawScriptDebugSlot(engine::NativeScriptSlot& slot, int idSalt) {
    ImGui::PushID(idSalt);
    bool enabled = slot.enabled;
    if (ImGui::Checkbox("Runtime Enabled", &enabled)) slot.enabled = enabled;
    ImGui::SameLine();
    if (slot.active) ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.45f, 1.0f), "Active");
    else if (!slot.enabled) ImGui::TextDisabled("Disabled");
    else ImGui::TextDisabled("Waiting for activation");
    ImGui::PopID();
    ImGui::Text("Execution order: %d", slot.executionOrder);
    if (!slot.dependencies.empty()) {
        std::string required;
        for (const std::string& dependency : slot.dependencies) {
            if (!required.empty()) required += ", ";
            required += dependency;
        }
        ImGui::TextWrapped("Requires: %s", required.c_str());
    }
    if (!slot.dependencyError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
                           "Dependency error: %s", slot.dependencyError.c_str());
    }
    if (slot.fields.empty()) {
        ImGui::TextDisabled("(no fields)");
        return;
    }
    using FieldType = engine::ScriptField::Type;
    for (std::size_t i = 0; i < slot.fields.size(); ++i) {
        engine::ScriptField& field = slot.fields[i];
        ImGui::PushID(static_cast<int>(i) + idSalt * 1000);
        switch (field.type) {
        case FieldType::Bool: {
            bool v = field.value == "1" || field.value == "true" || field.value == "True";
            if (ImGui::Checkbox(field.name.c_str(), &v)) field.value = v ? "1" : "0";
            break;
        }
        case FieldType::Float: {
            float v = 0.0f;
            std::sscanf(field.value.c_str(), "%f", &v);
            bool edited = false;
            if (field.maxValue > field.minValue) {
                edited = ImGui::SliderFloat(field.name.c_str(), &v, field.minValue, field.maxValue);
            } else {
                edited = ImGui::DragFloat(field.name.c_str(), &v, 0.05f);
            }
            if (edited) {
                char buffer[64];
                std::snprintf(buffer, sizeof(buffer), "%g", v);
                field.value = buffer;
            }
            break;
        }
        case FieldType::Int: {
            int v = 0;
            std::sscanf(field.value.c_str(), "%d", &v);
            if (ImGui::DragInt(field.name.c_str(), &v)) field.value = std::to_string(v);
            break;
        }
        case FieldType::Vec3:
        case FieldType::Color: {
            float v[3] = {0.0f, 0.0f, 0.0f};
            std::sscanf(field.value.c_str(), "%f %f %f", &v[0], &v[1], &v[2]);
            const bool edited = field.type == FieldType::Color
                ? ImGui::ColorEdit3(field.name.c_str(), v)
                : ImGui::DragFloat3(field.name.c_str(), v, 0.05f);
            if (edited) {
                char buffer[96];
                std::snprintf(buffer, sizeof(buffer), "%g %g %g", v[0], v[1], v[2]);
                field.value = buffer;
            }
            break;
        }
        default: {   // String / Entity / Asset — plain text
            std::array<char, 128> buffer{};
            std::snprintf(buffer.data(), buffer.size(), "%s", field.value.c_str());
            if (ImGui::InputText(field.name.c_str(), buffer.data(), buffer.size())) {
                field.value = buffer.data();
            }
            break;
        }
        }
        ImGui::PopID();
    }
}

}  // namespace

void EditorApp::DrawScriptDebugPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::ScriptDebug)) return;

    bool open = true;
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::ScriptDebug), &open)) {
        ImGui::End();
        m_panels.SetOpen(EditorPanels::Panel::ScriptDebug, open);
        return;
    }

    static std::vector<engine::ScriptTestResult> testResults;
    static bool testsHaveRun = false;
    if (ImGui::CollapsingHeader("Script Tests", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Run All Tests")) {
            testResults = engine::RunScriptTests(
                ScanLuaTestSources(m_project.AssetRoot()));
            testsHaveRun = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("C++ DefineTests + Lua ScriptTests");

        if (testsHaveRun) {
            const int passed = static_cast<int>(std::count_if(
                testResults.begin(), testResults.end(),
                [](const engine::ScriptTestResult& result) { return result.passed; }));
            const int failed = static_cast<int>(testResults.size()) - passed;
            if (testResults.empty()) {
                ImGui::TextDisabled(
                    "No tests found. Override DefineTests or add a ScriptTests table.");
            } else {
                ImGui::TextColored(
                    failed == 0 ? ImVec4(0.35f, 0.9f, 0.45f, 1.0f)
                                : ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
                    "%d passed, %d failed", passed, failed);
                if (ImGui::BeginTable("##scripttestresults", 4,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
                            | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                        ImVec2(0.0f, 190.0f))) {
                    ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed, 55.0f);
                    ImGui::TableSetupColumn("Suite");
                    ImGui::TableSetupColumn("Test");
                    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                    ImGui::TableHeadersRow();
                    for (std::size_t i = 0; i < testResults.size(); ++i) {
                        const engine::ScriptTestResult& result = testResults[i];
                        ImGui::PushID(static_cast<int>(i));
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextColored(
                            result.passed ? ImVec4(0.35f, 0.9f, 0.45f, 1.0f)
                                          : ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
                            "%s", result.passed ? "PASS" : "FAIL");
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(result.suite.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%s (%d assertions)", result.name.c_str(),
                                    result.assertions);
                        for (const std::string& failure : result.failures) {
                            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
                                               "  %s", failure.c_str());
                        }
                        ImGui::TableNextColumn();
                        ImGui::Text("%.2f ms", result.milliseconds);
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }
        }
    }
    ImGui::Separator();

    if (m_mode != EditorMode::Play || !m_playRegistry) {
        ImGui::TextDisabled("Enter Play mode to inspect and tweak live script fields.");
        ImGui::End();
        m_panels.SetOpen(EditorPanels::Panel::ScriptDebug, open);
        return;
    }

    engine::ScriptDebugState debugState = engine::GetScriptDebugState();
    bool debuggerEnabled = debugState.enabled;
    if (ImGui::Checkbox("Enable callback debugger", &debuggerEnabled)) {
        engine::SetScriptDebuggingEnabled(debuggerEnabled);
        debugState = engine::GetScriptDebugState();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!debuggerEnabled);
    if (ImGui::Button(debugState.paused ? "Resume Scripts" : "Pause Scripts")) {
        engine::SetScriptExecutionPaused(!debugState.paused);
        debugState = engine::GetScriptDebugState();
    }
    ImGui::SameLine();
    if (ImGui::Button("Step Callback")) {
        engine::RequestScriptExecutionStep();
        debugState = engine::GetScriptDebugState();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Timings")) engine::ClearScriptExecutionStatistics();
    ImGui::EndDisabled();
    if (!debugState.stopReason.empty()) {
        ImGui::TextColored(debugState.paused ? ImVec4(1.0f, 0.75f, 0.25f, 1.0f)
                                             : ImVec4(0.65f, 0.75f, 0.85f, 1.0f),
                           "%s", debugState.stopReason.c_str());
    }
    ImGui::TextDisabled("Breakpoints stop after a safe lifecycle callback boundary.");

    static int breakpointClassIndex = 0;
    const std::vector<std::string> scriptClasses = engine::ScriptRegistry::Instance().Names();
    if (!scriptClasses.empty()) {
        breakpointClassIndex = std::clamp(
            breakpointClassIndex, 0, static_cast<int>(scriptClasses.size()) - 1);
        if (ImGui::BeginCombo("Breakpoint Class",
                scriptClasses[static_cast<std::size_t>(breakpointClassIndex)].c_str())) {
            for (int i = 0; i < static_cast<int>(scriptClasses.size()); ++i) {
                if (ImGui::Selectable(scriptClasses[static_cast<std::size_t>(i)].c_str(),
                        breakpointClassIndex == i)) breakpointClassIndex = i;
            }
            ImGui::EndCombo();
        }
        const std::string& selectedClass =
            scriptClasses[static_cast<std::size_t>(breakpointClassIndex)];
        const engine::ScriptCallbackKind callbacks[] = {
            engine::ScriptCallbackKind::OnCreate,
            engine::ScriptCallbackKind::OnEnable,
            engine::ScriptCallbackKind::OnDisable,
            engine::ScriptCallbackKind::OnUpdate,
            engine::ScriptCallbackKind::OnFixedUpdate,
            engine::ScriptCallbackKind::OnEvent,
            engine::ScriptCallbackKind::OnScriptCall
        };
        for (const engine::ScriptCallbackKind callback : callbacks) {
            bool breakpoint = engine::HasScriptCallbackBreakpoint(selectedClass, callback);
            if (ImGui::Checkbox(engine::ScriptCallbackKindName(callback), &breakpoint)) {
                engine::SetScriptCallbackBreakpoint(selectedClass, callback, breakpoint);
            }
            ImGui::SameLine();
        }
        ImGui::NewLine();
        if (ImGui::Button("Clear Breakpoints")) engine::ClearScriptCallbackBreakpoints();
    }

    debugState = engine::GetScriptDebugState();
    if (debuggerEnabled && !debugState.statistics.empty()
        && ImGui::CollapsingHeader("Execution Timings", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("##scripttimings", 7,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
                    | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                ImVec2(0.0f, 150.0f))) {
            ImGui::TableSetupColumn("Class");
            ImGui::TableSetupColumn("Entity");
            ImGui::TableSetupColumn("Callback");
            ImGui::TableSetupColumn("Last ms");
            ImGui::TableSetupColumn("Average ms");
            ImGui::TableSetupColumn("Maximum ms");
            ImGui::TableSetupColumn("Calls");
            ImGui::TableHeadersRow();
            for (const engine::ScriptExecutionStat& stat : debugState.statistics) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(stat.className.c_str());
                ImGui::TableNextColumn(); ImGui::Text("%u", stat.entity);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(
                    engine::ScriptCallbackKindName(stat.callback));
                ImGui::TableNextColumn(); ImGui::Text("%.4f", stat.lastMilliseconds);
                ImGui::TableNextColumn(); ImGui::Text("%.4f", stat.averageMilliseconds);
                ImGui::TableNextColumn(); ImGui::Text("%.4f", stat.maximumMilliseconds);
                ImGui::TableNextColumn(); ImGui::Text("%llu",
                    static_cast<unsigned long long>(stat.callCount));
            }
            ImGui::EndTable();
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Live values - edits apply on the script's next update.");
    ImGui::Separator();
    ImGui::BeginChild("##scriptdebuglist");

    int shown = 0;
    for (const auto& entry : m_playEntityNames) {
        const engine::ecs::Entity entity = entry.first;
        if (!m_playRegistry->Valid(entity)) continue;
        engine::NativeScriptComponent* script =
            m_playRegistry->TryGet<engine::NativeScriptComponent>(entity);
        if (!script) continue;

        ++shown;
        ImGui::PushID(shown);
        std::string header = entry.second + "  ["
            + (script->className.empty() ? std::string("script") : script->className) + "]";
        if (!script->enabled) header += " (disabled)";
        if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            DrawScriptDebugSlot(*script, 0);
            for (std::size_t i = 0; i < script->additional.size(); ++i) {
                ImGui::SeparatorText(script->additional[i].className.empty()
                    ? "additional script"
                    : script->additional[i].className.c_str());
                DrawScriptDebugSlot(script->additional[i], static_cast<int>(i) + 1);
            }
        }
        ImGui::PopID();
    }
    if (shown == 0) {
        ImGui::TextDisabled("No scripted objects in the running scene.");
    }

    ImGui::EndChild();
    ImGui::End();
    m_panels.SetOpen(EditorPanels::Panel::ScriptDebug, open);
}

void EditorApp::HotReloadScripts() {
    if (m_scriptBuildRunning) {
        m_scriptBuildPending = true;
        m_log.Warning("A script build is already running; one follow-up build was queued.");
        return;
    }
    m_scriptSourceSnapshot = ScanNativeScriptSources(m_project.AssetRoot());
    m_scriptSourceSnapshotInitialized = true;
    StartScriptAutoBuild();
}

void EditorApp::ResetScriptAutoReloadWatcher() {
    m_scriptSourceSnapshot.clear();
    m_scriptSourceSnapshotInitialized = false;
    m_scriptBuildPending = false;
    m_scriptWatchPoll = 0.0f;
    m_scriptBuildDebounce = 0.0f;
    m_scriptBuildStatus = m_autoCompileScripts
        ? "Watching Content/Scripts" : "Automatic compilation is off";
}

void EditorApp::StartScriptAutoBuild() {
    if (m_scriptShuttingDown) return;
    if (m_scriptBuildRunning || m_scriptModuleInstallInProgress) {
        m_scriptBuildPending = true;
        return;
    }

    std::error_code ec;
    const std::filesystem::path contentRoot =
        std::filesystem::absolute(m_project.AssetRoot(), ec);
    const std::filesystem::path projectRoot = contentRoot.parent_path();
    if (ec || projectRoot.empty()) {
        m_scriptBuildStatus = "Could not resolve the project folder";
        m_log.Error("Automatic script build could not resolve the project folder.");
        return;
    }

    m_scriptBuildRunning = true;
    m_scriptBuildPending = false;
    const std::uint64_t generation = ++m_scriptBuildGeneration;
    m_activeScriptBuildGeneration = generation;
    m_projectScriptModuleState = ProjectScriptModuleState::Building;
    m_scriptBuildStatus = "Building project scripts...";
    m_log.Info("Script change detected; compiling in the background...");
    try {
        m_scriptBuildFuture = std::async(std::launch::async,
            [contentRoot, projectRoot, generation]() {
                ScriptBuildResult result;
                result.projectRoot = projectRoot;
                result.generation = generation;
                std::string error;
                if (!EditorGeneratedScriptTools::RegenerateGeneratedScripts(
                        contentRoot, &error)) {
                    result.error = "Script registration generation failed: " + error;
                    return result;
                }
                if (!EditorScriptTools::BuildTarget(
                        projectRoot, "Debug", "game_scripts", &result.error)) {
                    return result;
                }
                const std::filesystem::path built =
                    EditorScriptTools::ProjectScriptBinary(projectRoot);
                result.candidateDll = EditorScriptTools::ProjectScriptCandidatePath(
                    projectRoot, generation);
                std::error_code copyError;
                std::filesystem::create_directories(
                    result.candidateDll.parent_path(), copyError);
                copyError.clear();
                std::filesystem::copy_file(built, result.candidateDll,
                    std::filesystem::copy_options::overwrite_existing, copyError);
                if (copyError) {
                    result.error = "Could not prepare script candidate: " + copyError.message();
                    result.candidateDll.clear();
                    return result;
                }
                result.success = true;
                return result;
            });
    } catch (const std::exception& e) {
        m_scriptBuildRunning = false;
        m_projectScriptModuleState = ProjectScriptModuleState::BuildFailed;
        m_scriptBuildStatus = "Could not start background compilation";
        m_log.Error("Automatic script build could not start: " + std::string(e.what()));
    }
}

void EditorApp::UpdateScriptAutoReload(float dt) {
    if (m_scriptBuildRunning && m_scriptBuildFuture.valid()
        && m_scriptBuildFuture.wait_for(std::chrono::seconds(0))
            == std::future_status::ready) {
        ScriptBuildResult result;
        try {
            result = m_scriptBuildFuture.get();
        } catch (const std::exception& exception) {
            result.error = "Background script build failed unexpectedly: "
                + std::string(exception.what());
            m_projectScriptModuleState = ProjectScriptModuleState::BuildFailed;
            m_scriptBuildStatus = "Compilation failed - open Last Build Log";
            m_log.Error(result.error);
        } catch (...) {
            result.error = "Background script build failed unexpectedly.";
            m_projectScriptModuleState = ProjectScriptModuleState::BuildFailed;
            m_scriptBuildStatus = "Compilation failed - open Last Build Log";
            m_log.Error(result.error);
        }
        m_scriptBuildRunning = false;

        std::error_code ec;
        const std::filesystem::path currentProjectRoot =
            std::filesystem::absolute(m_project.AssetRoot(), ec).parent_path();
        const bool currentResult = !m_scriptShuttingDown && !ec
            && result.generation == m_activeScriptBuildGeneration
            && result.projectRoot.lexically_normal() == currentProjectRoot.lexically_normal();
        if (currentResult) {
            if (result.success) {
                m_projectScriptModuleState = ProjectScriptModuleState::CandidateReady;
                if (InstallProjectScriptCandidate(
                        result.candidateDll, result.generation, true)) {
                    m_scriptBuildStatus = "Scripts compiled and reloaded";
                    m_log.Info("Automatic script compilation and hot reload completed.");
                } else {
                    m_scriptBuildStatus = "Compiled, but reload failed";
                }
            } else {
                m_projectScriptModuleState = ProjectScriptModuleState::BuildFailed;
                m_scriptBuildStatus = "Compilation failed - open Last Build Log";
                m_log.Error("Automatic script compilation failed: " + result.error);
            }
        } else if (!result.candidateDll.empty()) {
            std::filesystem::remove(result.candidateDll, ec);
        }
    }

    if (!m_autoCompileScripts) {
        if (!m_scriptBuildRunning) m_scriptBuildStatus = "Automatic compilation is off";
        return;
    }

    m_scriptWatchPoll -= std::max(dt, 0.0f);
    m_scriptBuildDebounce -= std::max(dt, 0.0f);
    if (m_scriptWatchPoll <= 0.0f) {
        m_scriptWatchPoll = 0.25f;
        const auto current = ScanNativeScriptSources(m_project.AssetRoot());
        if (!m_scriptSourceSnapshotInitialized) {
            m_scriptSourceSnapshot = current;
            m_scriptSourceSnapshotInitialized = true;
        } else if (current != m_scriptSourceSnapshot) {
            m_scriptSourceSnapshot = current;
            m_scriptBuildPending = true;
            m_scriptBuildDebounce = 0.65f;
            m_scriptBuildStatus = m_scriptBuildRunning
                ? "Changes queued behind current build" : "Waiting for file save to settle...";
        }
    }

    if (m_scriptBuildPending && !m_scriptBuildRunning
        && m_scriptBuildDebounce <= 0.0f) {
        StartScriptAutoBuild();
    }
}

bool EditorApp::LoadProjectScriptModule(bool reportMissing) {
    std::error_code ec;
    const std::filesystem::path projectRoot =
        std::filesystem::absolute(m_project.AssetRoot(), ec).parent_path();
    const std::filesystem::path binary =
        EditorScriptTools::ProjectScriptBinary(projectRoot);
    if (!std::filesystem::is_regular_file(binary, ec)) {
        if (reportMissing) {
            m_log.Warning("No compiled project script module found at " + binary.string()
                + ". Compile scripts first.");
        }
        return false;
    }
    const std::uint64_t generation = ++m_scriptBuildGeneration;
    const std::filesystem::path candidate =
        EditorScriptTools::ProjectScriptCandidatePath(projectRoot, generation);
    std::filesystem::create_directories(candidate.parent_path(), ec);
    ec.clear();
    std::filesystem::copy_file(binary, candidate,
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        m_log.Error("Could not stage the project script module: " + ec.message());
        return false;
    }
    return InstallProjectScriptCandidate(candidate, generation, reportMissing);
}

bool EditorApp::InstallProjectScriptCandidate(
    const std::filesystem::path& candidate, std::uint64_t generation,
    bool reportMissing) {
    std::error_code ec;
    const std::filesystem::path projectRoot =
        std::filesystem::absolute(m_project.AssetRoot(), ec).parent_path();
    if (ec || projectRoot.empty() || !std::filesystem::is_regular_file(candidate, ec)) {
        if (reportMissing) m_log.Error("Project script candidate is missing: " + candidate.string());
        m_projectScriptModuleState = ProjectScriptModuleState::LoadFailed;
        return false;
    }

    EditorScriptTools::ScriptModuleLoadMarker marker;
    marker.candidateDll = candidate;
    marker.buildProductDll = EditorScriptTools::ProjectScriptBinary(projectRoot);
    marker.generation = generation;
    std::string error;
    if (!EditorScriptTools::WriteScriptModuleLoadMarker(projectRoot, marker, &error)) {
        m_log.Error(error + " Candidate loading was cancelled so startup recovery remains reliable.");
        m_projectScriptModuleState = ProjectScriptModuleState::LoadFailed;
        return false;
    }
    m_scriptModuleInstallInProgress = true;
    m_projectScriptModuleState = ProjectScriptModuleState::Validating;

    // Register into isolated registries first. The active factories and module remain
    // untouched until the candidate DLL has loaded and its export has run successfully.
    engine::ScriptRegistry candidateScripts;
    engine::ai::BtScriptRegistry candidateBtScripts;
    candidateScripts.SetStrictValidation(true);
    candidateBtScripts.SetStrictValidation(true);
    engine::ScriptModule candidateModule;
    if (!candidateModule.Load(candidate.string(), candidateScripts,
            candidateBtScripts, &error)) {
        candidateScripts.Clear();
        candidateBtScripts.Clear();
        candidateModule.Unload();
        std::string quarantineError;
        EditorScriptTools::QuarantineScriptModule(
            projectRoot, candidate, generation, nullptr, &quarantineError);
        std::string productError;
        EditorScriptTools::QuarantineScriptModule(projectRoot,
            EditorScriptTools::ProjectScriptBinary(projectRoot), generation,
            nullptr, &productError);
        EditorScriptTools::ClearScriptModuleLoadMarker(projectRoot, nullptr);
        m_scriptModuleInstallInProgress = false;
        m_projectScriptModuleState = ProjectScriptModuleState::LoadFailed;
        m_log.Error("Could not load project scripts: " + error);
        if (!quarantineError.empty()) m_log.Warning(quarantineError);
        if (!productError.empty()) m_log.Warning(productError);
        return false;
    }

    if (!candidateScripts.Valid(&error) || !candidateBtScripts.Valid(&error)) {
        candidateScripts.Clear();
        candidateBtScripts.Clear();
        candidateModule.Unload();
        std::string quarantineError;
        EditorScriptTools::QuarantineScriptModule(
            projectRoot, candidate, generation, nullptr, &quarantineError);
        std::string productError;
        EditorScriptTools::QuarantineScriptModule(projectRoot,
            EditorScriptTools::ProjectScriptBinary(projectRoot), generation,
            nullptr, &productError);
        EditorScriptTools::ClearScriptModuleLoadMarker(projectRoot, nullptr);
        m_scriptModuleInstallInProgress = false;
        m_projectScriptModuleState = ProjectScriptModuleState::LoadFailed;
        m_log.Error("Project script candidate validation failed: " + error);
        if (!quarantineError.empty()) m_log.Warning(quarantineError);
        if (!productError.empty()) m_log.Warning(productError);
        return false;
    }

    std::vector<std::string> candidateScriptNames = candidateScripts.Names();
    std::vector<std::string> candidateBtNames = candidateBtScripts.Names();
    const bool wasPaused = engine::GetScriptDebugState().paused;
    engine::SetScriptExecutionPaused(true);
    m_projectScriptModuleState = ProjectScriptModuleState::Reloading;

    // Preserve runtime values before destroying objects whose vtables live in the old DLL.
    // ScriptField values stay on each ECS component. Custom transient values can opt into
    // Script::OnBeforeHotReload/OnAfterHotReload. AI blackboards are restored by agent name.
    std::unordered_map<std::string, engine::ai::Blackboard> aiBlackboards;
    std::unordered_map<std::string, engine::ecs::Entity> playEntitiesByName;
    struct SavedReloadState {
        engine::ecs::Entity entity = engine::ecs::kNull;
        int attachment = 0;
        engine::Script::ReloadState state;
    };
    std::vector<SavedReloadState> savedReloadStates;
    if (m_mode == EditorMode::Play && m_playRegistry) {
        for (const PlayAgent& agent : m_playAgents) {
            if (agent.useGraph) aiBlackboards[agent.name] = agent.ctx.blackboard;
        }
        for (const auto& entry : m_playEntityNames) {
            playEntitiesByName[entry.second] = entry.first;
        }
        engine::PrepareScriptsForHotReload(*m_playRegistry);
        m_playRegistry->view<engine::NativeScriptComponent>().each(
            [&](engine::ecs::Entity entity, engine::NativeScriptComponent& component) {
                if (component.restoreReloadState) {
                    savedReloadStates.push_back({entity, 0, component.reloadState});
                }
                for (std::size_t index = 0; index < component.additional.size(); ++index) {
                    const engine::NativeScriptSlot& slot = component.additional[index];
                    if (slot.restoreReloadState) {
                        savedReloadStates.push_back({entity,
                            static_cast<int>(index) + 1, slot.reloadState});
                    }
                }
            });
        m_playAgents.clear();
        m_playBtGraphCache.clear();
    }

    engine::ScriptRegistry& scripts = engine::ScriptRegistry::Instance();
    engine::ai::BtScriptRegistry& btScripts = engine::ai::BtScriptRegistry::Instance();
    engine::ScriptRegistry oldScripts = scripts.Extract(m_projectScriptClasses);
    engine::ai::BtScriptRegistry oldBtScripts = btScripts.Extract(m_projectBtScriptClasses);
    bool committed = false;
    try {
        scripts.MergeFrom(std::move(candidateScripts));
        btScripts.MergeFrom(std::move(candidateBtScripts));
        if (m_mode == EditorMode::Play && m_playRegistry) {
            BuildPlayAgents(playEntitiesByName);
            for (PlayAgent& agent : m_playAgents) {
                const auto saved = aiBlackboards.find(agent.name);
                if (saved != aiBlackboards.end() && agent.useGraph) {
                    agent.ctx.blackboard = saved->second;
                }
            }
            if (!engine::RecreateScriptsAfterHotReload(
                    *m_playRegistry, &m_runtimeAudio, &m_cameraShake,
                    &m_cameraDirector, &engine::GameMode::Instance(),
                    &m_playPhysics, &error)) {
                throw std::runtime_error(error.empty()
                    ? "live script reconstruction failed" : error);
            }
        }
        // Destroy the old DLL-owned factories before unloading its image. The old module
        // remains resident until candidate registration and runtime reconstruction finish.
        m_activeScriptCandidate = candidate;
        m_projectScriptClasses.swap(candidateScriptNames);
        m_projectBtScriptClasses.swap(candidateBtNames);
        oldScripts.Clear();
        oldBtScripts.Clear();
        m_scriptModule.Swap(candidateModule);
        std::string unloadError;
        if (!candidateModule.Unload(&unloadError)) m_log.Warning(unloadError);
        committed = true;
    } catch (const std::exception& exception) {
        error = exception.what();
    } catch (...) {
        error = "unknown runtime reconstruction failure";
    }

    if (!committed) {
        if (m_mode == EditorMode::Play && m_playRegistry) {
            engine::ShutdownScripts(*m_playRegistry);
            for (const SavedReloadState& saved : savedReloadStates) {
                engine::NativeScriptComponent* component =
                    m_playRegistry->TryGet<engine::NativeScriptComponent>(saved.entity);
                if (!component) continue;
                engine::NativeScriptSlot* slot = saved.attachment == 0
                    ? static_cast<engine::NativeScriptSlot*>(component)
                    : (static_cast<std::size_t>(saved.attachment - 1)
                            < component->additional.size()
                        ? &component->additional[static_cast<std::size_t>(saved.attachment - 1)]
                        : nullptr);
                if (!slot) continue;
                slot->reloadState = saved.state;
                slot->restoreReloadState = true;
                slot->missingFactory = false;
            }
        }
        m_playAgents.clear();
        m_playBtGraphCache.clear();
        for (const std::string& name : candidateScriptNames) scripts.Remove(name);
        for (const std::string& name : candidateBtNames) btScripts.Remove(name);
        scripts.MergeFrom(std::move(oldScripts));
        btScripts.MergeFrom(std::move(oldBtScripts));
        candidateScripts.Clear();
        candidateBtScripts.Clear();
        candidateModule.Unload();
        if (m_mode == EditorMode::Play && m_playRegistry) {
            try {
                BuildPlayAgents(playEntitiesByName);
                std::string restoreError;
                engine::RecreateScriptsAfterHotReload(
                    *m_playRegistry, &m_runtimeAudio, &m_cameraShake,
                    &m_cameraDirector, &engine::GameMode::Instance(),
                    &m_playPhysics, &restoreError);
            } catch (...) {}
        }
        engine::SetScriptExecutionPaused(wasPaused);
        EditorScriptTools::ClearScriptModuleLoadMarker(projectRoot, nullptr);
        m_scriptModuleInstallInProgress = false;
        m_projectScriptModuleState = ProjectScriptModuleState::LoadFailed;
        std::string quarantineError;
        EditorScriptTools::QuarantineScriptModule(
            projectRoot, candidate, generation, nullptr, &quarantineError);
        if (!quarantineError.empty()) m_log.Warning(quarantineError);
        m_log.Error("Project script install rolled back: " + error);
        return false;
    }

    if (!EditorScriptTools::ClearScriptModuleLoadMarker(projectRoot, &error)) {
        m_log.Warning(error);
    }
    m_scriptModuleInstallInProgress = false;
    m_projectScriptSafeMode = false;
    m_projectScriptModuleState = ProjectScriptModuleState::Ready;
    engine::SetScriptExecutionPaused(wasPaused);
    EditorScriptTools::CleanupScriptCandidates(projectRoot, m_activeScriptCandidate);
    m_log.Info("Loaded project scripts from candidate generation "
        + std::to_string(generation) + ": " + candidate.string());
    return true;
}

bool EditorApp::RecoverInterruptedScriptLoad(
    const std::filesystem::path& projectRoot) {
    const std::filesystem::path markerPath =
        EditorScriptTools::ProjectScriptLoadMarkerPath(projectRoot);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(markerPath, ec)) return false;

    EditorScriptTools::ScriptModuleLoadMarker marker;
    std::string markerError;
    if (!EditorScriptTools::ReadScriptModuleLoadMarker(
            projectRoot, &marker, &markerError)) {
        m_log.Error(markerError + " Native project scripts are disabled for this launch.");
    }
    if (marker.buildProductDll.empty()) {
        marker.buildProductDll = EditorScriptTools::ProjectScriptBinary(projectRoot);
    }
    std::string quarantineError;
    std::filesystem::path quarantined;
    if (!EditorScriptTools::QuarantineScriptModule(projectRoot, marker.candidateDll,
            marker.generation, &quarantined, &quarantineError)) {
        m_log.Warning(quarantineError);
    }
    // The build product produced the suspect candidate. Quarantine it too so the next
    // startup cannot copy and load the exact same bytes again.
    std::string productError;
    if (!EditorScriptTools::QuarantineScriptModule(projectRoot, marker.buildProductDll,
            marker.generation, nullptr, &productError)) {
        m_log.Warning(productError);
    }
    EditorScriptTools::ClearScriptModuleLoadMarker(projectRoot, nullptr);
    m_projectScriptSafeMode = true;
    m_projectScriptModuleState = ProjectScriptModuleState::SafeMode;
    m_scriptBuildStatus = "SAFE MODE - rebuild project scripts to recover";
    m_log.Error("The previous project script module load did not complete. "
        "3DGEngine opened the project in Script Safe Mode; the suspect module was quarantined.");
    return true;
}

void EditorApp::DrainScriptBuild(bool shuttingDown) {
    if (shuttingDown) m_scriptShuttingDown = true;
    m_scriptBuildPending = false;
    if (!m_scriptBuildFuture.valid()) {
        m_scriptBuildRunning = false;
        return;
    }
    try {
        ScriptBuildResult result = m_scriptBuildFuture.get();
        std::error_code ec;
        if (!result.candidateDll.empty()) std::filesystem::remove(result.candidateDll, ec);
    } catch (const std::exception& exception) {
        m_log.Warning("Background script build ended during project close: "
            + std::string(exception.what()));
    } catch (...) {
        m_log.Warning("Background script build ended unexpectedly during project close.");
    }
    m_scriptBuildRunning = false;
}

void EditorApp::AcknowledgeCreatedScriptSource() {
    m_scriptSourceSnapshot = ScanNativeScriptSources(m_project.AssetRoot());
    m_scriptSourceSnapshotInitialized = true;
    m_scriptBuildPending = false;
    m_scriptBuildDebounce = 0.0f;
    m_scriptBuildStatus = m_projectScriptSafeMode
        ? "SAFE MODE - edit and save, then rebuild project scripts"
        : "Script created - waiting for the first source edit/save";
}
