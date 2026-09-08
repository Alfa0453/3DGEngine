#include "ProjectMigrationPanel.h"

#include "AnimationClipAsset.h"
#include "AnimationGraphAsset.h"
#include "CharacterAsset.h"
#include "EditorPanels.h"
#include "ParticleAsset.h"
#include "PrefabAsset.h"

#include <engine/assets/InteractionAsset.h>
#include <engine/assets/FoliageAsset.h>
#include <engine/assets/MaterialAssetLoader.h>
#include <engine/assets/SkeletalAsset.h>
#include <engine/assets/ShaderAsset.h>
#include <engine/assets/StaticMeshAsset.h>
#include <engine/assets/TextureAsset.h>
#include <engine/core/Config.h>

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {
std::string Lower(std::string value) { std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); return value; }
bool EndsWith(const std::string& value, const std::string& suffix) { return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0; }
std::string Timestamp() { const auto now = std::chrono::system_clock::now(); const std::time_t time = std::chrono::system_clock::to_time_t(now); std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    std::ostringstream out; out << std::put_time(&local, "%Y%m%d-%H%M%S"); return out.str(); }
bool SamePath(const std::filesystem::path& a, const std::filesystem::path& b) { if (a.empty() || b.empty()) return false; std::error_code ecA, ecB; const auto ca = std::filesystem::weakly_canonical(a, ecA); const auto cb = std::filesystem::weakly_canonical(b, ecB); return !ecA && !ecB ? ca == cb : a.lexically_normal() == b.lexically_normal(); }
struct Format { const char* extension; const char* magic; const char* name; int current; };
constexpr Format kFormats[] = {
    {".scene", "3DGEditorScene", "Scene", EditorScene::CurrentFileVersion},
    {".3dgcharacter", "3DG_CHARACTER", "Character", 30},
    {".3dggraph", "3DG_GRAPH", "Animation Graph", 8},
    {".3dgclip", "3DG_CLIP", "Animation Clip", 5},
    {".3dgprefab", "3DG_PREFAB", "Prefab", 5},
    {".particle", "3DG_PARTICLE", "Particle System", 14},
    {".3dgshader", "3DG_SHADER", "Shader", 5},
    {".3dgmat", "3DG_MATERIAL", "Material", 5},
    {".3dginteraction", "3DG_INTERACTION", "Interaction", 2},
};
const Format* FindFormat(const std::filesystem::path& path) { const std::string ext = Lower(path.extension().string()); for (const auto& format : kFormats) if (ext == format.extension) return &format; return nullptr; }
const Format* FindNativeFormat(const std::filesystem::path& path) { static constexpr Format formats[] = {{".3dgmesh", "NATIVE", "Static Mesh", static_cast<int>(engine::kStaticMeshAssetVersion)}, {".3dgskmesh", "NATIVE", "Skeletal Mesh", static_cast<int>(engine::kSkeletalMeshAssetVersion)}, {".3dgtex", "NATIVE", "Texture", static_cast<int>(engine::kTextureAssetVersion)}, {".3dgfoliage", "NATIVE", "Foliage", static_cast<int>(engine::kFoliageAssetVersion)}}; const std::string ext=Lower(path.extension().string()); for(const auto& f:formats)if(ext==f.extension)return &f;return nullptr; }
bool SkipDirectory(const std::filesystem::path& path) { const std::string name = Lower(path.filename().string()); return name == ".git" || name == "build" || name == "binaries" || name == "intermediate" || name == "projectmigrations"; }
const char* StateName(ProjectMigrationPanel::State state) { switch (state) { case ProjectMigrationPanel::State::Current:return "Current"; case ProjectMigrationPanel::State::Legacy:return "Upgrade"; case ProjectMigrationPanel::State::Future:return "Newer engine"; case ProjectMigrationPanel::State::Invalid:return "Invalid"; case ProjectMigrationPanel::State::Unknown:return "Unknown"; } return "Unknown"; }
}

void ProjectMigrationPanel::StartScan(const std::filesystem::path& root) {
    if (m_scanning || root.empty()) return; m_scanning = true; m_status = "Scanning project formats...";
    m_scan = std::async(std::launch::async, [root] {
        ScanResult result; std::error_code ec;
        if (!std::filesystem::is_directory(root, ec)) { result.error = "Project folder does not exist."; return result; }
        auto addProject = [&](const std::filesystem::path& path) {
            Entry entry; entry.path = path; entry.relativePath = std::filesystem::relative(path, root, ec).generic_string(); entry.type = "Project"; entry.magic = "3DG Project Config"; entry.targetVersion = 1;
            engine::Config config; if (!config.Load(path.string())) { entry.state = State::Invalid; entry.detail = "Could not read project settings."; }
            else { entry.version = config.GetInt("project.format_version", 0); entry.state = entry.version < 1 ? State::Legacy : (entry.version > 1 ? State::Future : State::Current); entry.migratable = entry.state == State::Legacy; entry.selected = entry.migratable; entry.detail = entry.migratable ? "Adds explicit format metadata and normalizes the asset-root key." : ""; }
            result.entries.push_back(std::move(entry));
        };
        for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_directory(ec) && SkipDirectory(it->path())) { it.disable_recursion_pending(); continue; }
            if (!it->is_regular_file(ec)) continue;
            const auto& path = it->path(); const std::string lowerName = Lower(path.filename().string());
            if (EndsWith(lowerName, ".autosave.scene") || EndsWith(lowerName, ".runtime.scene")) continue;
            if (Lower(path.extension().string()) == ".3dgproject") { addProject(path); continue; }
            const Format* format = FindFormat(path); const Format* nativeFormat = FindNativeFormat(path); if (!format && !nativeFormat) continue;
            if (!format) { Entry entry; entry.path=path; entry.relativePath=std::filesystem::relative(path,root,ec).generic_string(); entry.type=nativeFormat->name; entry.magic="Native Asset"; entry.targetVersion=nativeFormat->current; std::ifstream native(path,std::ios::binary); engine::NativeAssetHeader header; std::string error; if(!native||!engine::ReadNativeAssetHeader(native,&header,&error)){entry.state=State::Invalid;entry.detail=error.empty()?"Native header could not be read.":error;}else{entry.version=static_cast<int>(header.assetVersion);if(entry.version<1){entry.state=State::Invalid;entry.detail="Invalid native asset version.";}else if(entry.version>entry.targetVersion){entry.state=State::Future;entry.detail="Created by a newer engine; it will not be changed.";}else if(entry.version==entry.targetVersion)entry.state=State::Current;else{entry.state=State::Legacy;entry.migratable=true;entry.selected=true;entry.detail="Will be decoded and re-encoded using the current native asset container.";}} result.entries.push_back(std::move(entry));continue; }
            Entry entry; entry.path = path; entry.relativePath = std::filesystem::relative(path, root, ec).generic_string(); entry.type = format->name; entry.targetVersion = format->current;
            std::ifstream in(path, std::ios::binary); if (!(in >> entry.magic >> entry.version)) { entry.state = State::Invalid; entry.detail = "Header could not be read."; }
            else if (entry.magic != format->magic && !(std::string(format->extension) == ".particle" && entry.magic == "3DGParticle")) { entry.state = State::Invalid; entry.detail = "Unexpected file signature: " + entry.magic; }
            else if (entry.version < 1) { entry.state = State::Invalid; entry.detail = "Invalid format version."; }
            else if (entry.version > entry.targetVersion) { entry.state = State::Future; entry.detail = "Created by a newer engine; it will not be changed."; }
            else if (entry.version == entry.targetVersion) entry.state = State::Current;
            else { entry.state = State::Legacy; entry.migratable = true; entry.selected = true; entry.detail = "Will be loaded through its legacy reader and re-saved as the current format."; }
            result.entries.push_back(std::move(entry));
        }
        if (ec) result.error = "Scan stopped early: " + ec.message();
        std::sort(result.entries.begin(), result.entries.end(), [](const Entry& a, const Entry& b) { if (a.state != b.state) return a.state == State::Legacy; return a.relativePath < b.relativePath; });
        return result;
    });
}

void ProjectMigrationPanel::PollScan() { if (!m_scanning || m_scan.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return; auto result = m_scan.get(); m_scanning = false; m_entries = std::move(result.entries); const auto legacy = std::count_if(m_entries.begin(), m_entries.end(), [](const Entry& e) { return e.state == State::Legacy; }); m_status = result.error.empty() ? std::to_string(legacy) + " upgrade(s) available." : result.error; }

bool ProjectMigrationPanel::Migrate(const Entry& entry, bool write, const SceneMigrator& sceneMigrator, std::string* error) const {
    const std::string ext = Lower(entry.path.extension().string());
    if (ext == ".scene") return sceneMigrator && sceneMigrator(entry.path, write, error);
    if (ext == ".3dgproject") { engine::Config config; if (!config.Load(entry.path.string())) { if (error) *error = "Could not load project settings."; return false; } if (!write) return true; config.Set("project.assets", config.GetString("project.assets", config.GetString("project.asses", "Content"))); config.Remove("project.asses"); config.Set("project.format_version", 1); if (!config.Save(entry.path.string())) { if (error) *error = "Could not save project settings."; return false; } return true; }
    if (ext == ".3dgcharacter") { CharacterAsset asset; if (!asset.Load(entry.path.string(), error)) return false; return !write || asset.Save(entry.path.string(), error); }
    if (ext == ".3dggraph") { AnimationGraphAsset asset; if (!asset.Load(entry.path.string(), error)) return false; return !write || asset.Save(entry.path.string(), error); }
    if (ext == ".3dgclip") { AnimationClipAsset asset; if (!asset.Load(entry.path.string(), error)) return false; return !write || asset.Save(entry.path.string(), error); }
    if (ext == ".3dgprefab") { PrefabAsset asset; if (!asset.Load(entry.path.string(), error)) return false; return !write || asset.Save(entry.path.string(), error); }
    if (ext == ".particle") { engine::ParticleSystemComponent asset; if (!particle_asset::Load(entry.path.string(), &asset, error)) return false; return !write || particle_asset::Save(entry.path.string(), asset, error); }
    if (ext == ".3dgshader") { engine::ShaderAsset asset; if (!engine::LoadShaderAsset(entry.path.string(), &asset, error)) return false; return !write || engine::SaveShaderAsset(entry.path.string(), asset, error); }
    if (ext == ".3dgmat") { engine::RuntimeMaterialAsset asset; if (!engine::LoadMaterialAssetFile(entry.path.string(), &asset, error)) return false; return !write || engine::SaveMaterialAssetFile(entry.path.string(), asset, error); }
    if (ext == ".3dginteraction") { engine::InteractionAssetData asset; if (!engine::LoadInteractionAsset(entry.path.string(), &asset, error)) return false; return !write || engine::SaveInteractionAsset(entry.path.string(), asset, error); }
    if (ext == ".3dgmesh") { engine::StaticMeshAssetData asset; if (!engine::LoadStaticMeshAsset(entry.path.string(), &asset, error)) return false; return !write || engine::SaveStaticMeshAsset(entry.path.string(), asset, error); }
    if (ext == ".3dgskmesh") { engine::SkeletalMeshAssetData asset; if (!engine::LoadSkeletalMeshAsset(entry.path.string(), &asset, error)) return false; return !write || engine::SaveSkeletalMeshAsset(entry.path.string(), asset, error); }
    if (ext == ".3dgtex") { engine::TextureAssetData asset; if (!engine::LoadTextureAsset(entry.path.string(), &asset, error)) return false; return !write || engine::SaveTextureAsset(entry.path.string(), asset, error); }
    if (ext == ".3dgfoliage") { engine::FoliageAssetData asset; if (!engine::LoadFoliageAsset(entry.path.string(), &asset, error)) return false; return !write || engine::SaveFoliageAsset(entry.path.string(), asset, error); }
    if (error) *error = "No migration adapter is registered for this file type."; return false;
}

void ProjectMigrationPanel::WriteReport(const std::filesystem::path& path, const std::string& outcome, const std::vector<std::string>& details) const { std::ofstream out(path); out << "3DG Project Migration Report\nOutcome: " << outcome << "\nProject: " << m_projectRoot.string() << "\nBackup: " << m_lastBackup << "\n\n"; for (const auto& line : details) out << line << '\n'; }

ProjectMigrationPanel::Result ProjectMigrationPanel::ApplySelected(const SceneMigrator& sceneMigrator, const std::filesystem::path& currentScene) {
    Result result; std::vector<Entry*> selected; for (auto& entry : m_entries) if (entry.selected && entry.migratable) selected.push_back(&entry); if (selected.empty()) { result.message = "No migratable files are selected."; return result; }
    for (Entry* entry : selected) { std::string error; if (!Migrate(*entry, false, sceneMigrator, &error)) { result.message = "Preflight validation failed before any files were changed: " + entry->relativePath + ": " + error; return result; } }
    const std::string stamp = Timestamp(); const auto backupRoot = m_projectRoot / "Saved" / "ProjectMigrations" / stamp; const auto reportRoot = m_projectRoot / "Saved" / "Reports"; std::error_code ec; std::filesystem::create_directories(backupRoot, ec); std::filesystem::create_directories(reportRoot, ec); if (ec) { result.message = "Could not create migration backup folders: " + ec.message(); return result; }
    m_lastBackup = backupRoot.string(); m_lastReport = (reportRoot / ("ProjectMigration-" + stamp + ".txt")).string(); std::vector<std::string> details; std::vector<Entry*> backedUp;
    for (Entry* entry : selected) { const auto destination = backupRoot / entry->relativePath; std::filesystem::create_directories(destination.parent_path(), ec); std::filesystem::copy_file(entry->path, destination, std::filesystem::copy_options::overwrite_existing, ec); if (ec) { details.push_back("BACKUP FAILED " + entry->relativePath + ": " + ec.message()); break; } backedUp.push_back(entry); details.push_back("BACKUP " + entry->relativePath); }
    std::string failure; if (backedUp.size() == selected.size()) { for (Entry* entry : selected) { std::string error; if (!Migrate(*entry, true, sceneMigrator, &error) || !Migrate(*entry, false, sceneMigrator, &error)) { failure = entry->relativePath + ": " + error; details.push_back("FAILED " + failure); break; } details.push_back("MIGRATED " + entry->relativePath + " v" + std::to_string(entry->version) + " -> v" + std::to_string(entry->targetVersion)); if (SamePath(entry->path, currentScene)) result.currentSceneChanged = true; } } else failure = "The complete backup could not be created.";
    if (!failure.empty()) { for (Entry* entry : backedUp) { const auto source = backupRoot / entry->relativePath; std::filesystem::copy_file(source, entry->path, std::filesystem::copy_options::overwrite_existing, ec); } details.push_back("ROLLBACK restored all backed-up files."); WriteReport(m_lastReport, "FAILED AND ROLLED BACK", details); result.message = "Migration failed and was rolled back. " + failure; return result; }
    WriteReport(m_lastReport, "SUCCESS", details); result.assetsChanged = true; result.message = std::to_string(selected.size()) + " file(s) migrated and validated. Backup and report were retained."; StartScan(m_projectRoot); return result;
}

ProjectMigrationPanel::Result ProjectMigrationPanel::Draw(const std::filesystem::path& projectRoot, const std::filesystem::path& currentScene, bool currentSceneDirty, const SceneMigrator& sceneMigrator, bool* open) {
    Result result; PollScan(); if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::ProjectMigration), open)) { ImGui::End(); return result; }
    if (!m_initialized || projectRoot != m_projectRoot) { m_projectRoot = projectRoot; m_initialized = true; StartScan(projectRoot); }
    ImGui::TextWrapped("Preview and safely upgrade legacy project data. Nothing is changed by scanning."); if (ImGui::Button("Scan Project") && !m_scanning) StartScan(projectRoot); ImGui::SameLine(); if (m_scanning) ImGui::TextDisabled("Scanning..."); else ImGui::TextUnformatted(m_status.c_str());
    ImGui::InputTextWithHint("##MigrationFilter", "Filter files or asset types", m_filter, sizeof(m_filter)); ImGui::SameLine(); ImGui::Checkbox("Show Current", &m_showCurrent); ImGui::SameLine(); ImGui::Checkbox("Show Unknown", &m_showUnknown);
    for (auto& entry : m_entries) if (currentSceneDirty && SamePath(entry.path, currentScene)) entry.selected = false;
    int selectedCount = 0; for (const auto& entry : m_entries) if (entry.selected && entry.migratable) ++selectedCount;
    if (ImGui::Button("Select Upgrades")) for (auto& e : m_entries) e.selected = e.migratable; ImGui::SameLine(); if (ImGui::Button("Clear Selection")) for (auto& e : m_entries) e.selected = false; ImGui::SameLine(); ImGui::Text("%d selected", selectedCount);
    const std::string filter = Lower(m_filter); if (ImGui::BeginTable("MigrationFiles", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, {0, 430})) { ImGui::TableSetupColumn("Use", ImGuiTableColumnFlags_WidthFixed, 40); ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 95); ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120); ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 75); ImGui::TableSetupColumn("File"); ImGui::TableSetupColumn("Details"); ImGui::TableHeadersRow();
        for (std::size_t i=0;i<m_entries.size();++i) { auto& e=m_entries[i]; if (!m_showCurrent && e.state==State::Current) continue; if (!m_showUnknown && e.state==State::Unknown) continue; if (!filter.empty() && Lower(e.relativePath+" "+e.type).find(filter)==std::string::npos) continue; const bool dirtyCurrent = currentSceneDirty && SamePath(e.path,currentScene); ImGui::PushID(static_cast<int>(i)); ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::BeginDisabled(!e.migratable || dirtyCurrent); ImGui::Checkbox("##Use",&e.selected); ImGui::EndDisabled(); ImGui::TableSetColumnIndex(1); if(e.state==State::Legacy) ImGui::TextColored({1,.75f,.2f,1},"Upgrade"); else if(e.state==State::Invalid||e.state==State::Future) ImGui::TextColored({1,.3f,.2f,1},"%s",StateName(e.state)); else ImGui::TextUnformatted(StateName(e.state)); ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(e.type.c_str()); ImGui::TableSetColumnIndex(3); ImGui::Text("%d -> %d",e.version,e.targetVersion); ImGui::TableSetColumnIndex(4); ImGui::TextUnformatted(e.relativePath.c_str()); ImGui::TableSetColumnIndex(5); ImGui::TextWrapped("%s",dirtyCurrent?"Save the open scene before migrating it.":e.detail.c_str()); ImGui::PopID(); }
        ImGui::EndTable(); }
    ImGui::TextDisabled("Apply first copies every selected file to Saved/ProjectMigrations. Any failure restores the full selection."); ImGui::BeginDisabled(m_scanning || selectedCount==0); if (ImGui::Button("Apply Selected Upgrades")) result = ApplySelected(sceneMigrator,currentScene); ImGui::EndDisabled();
    if (!m_lastBackup.empty()) ImGui::TextWrapped("Last backup: %s",m_lastBackup.c_str()); if (!m_lastReport.empty()) ImGui::TextWrapped("Last report: %s",m_lastReport.c_str()); if (!result.message.empty()) m_status=result.message; ImGui::End(); return result;
}
