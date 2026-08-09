#include "LauncherApp.h"

#include "EditorBranding.h"
#include "EditorIcons.h"
#include "EditorProject.h"
#include "NativeDialog.h"

#include <engine/core/Paths.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cstdlib>
#endif

#include <glad/glad.h>
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace {

engine::WindowProps LauncherWindowProps() {
    engine::WindowProps props;
    props.title = "3DG Launcher";
    props.width = 1060;
    props.height = 680;
    props.vsync = true;
    return props;
}

std::filesystem::path NormalizeProjectFile(std::filesystem::path path) {
    if (path.extension() != ".3dgproject") path /= "Project.3dgproject";
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    return (ec ? path : absolute).lexically_normal();
}

std::string ProjectNameFromFile(const std::filesystem::path& file) {
    engine::Config config(file.string());
    return config.GetString("project.name", file.parent_path().filename().string());
}

#if defined(_WIN32)
std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (length <= 1) return {};
    std::wstring wide(static_cast<std::size_t>(length - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), length);
    return wide;
}
#endif

void DrawBrandMark(ImDrawList* draw, const ImVec2 origin, float scale) {
    const ImU32 cyan = IM_COL32(31, 209, 245, 255);
    const ImU32 blue = IM_COL32(46, 115, 240, 255);
    const ImU32 gold = IM_COL32(255, 171, 46, 255);
    const float thickness = 3.5f * scale;
    const auto point = [&](float x, float y) {
        return ImVec2(origin.x + x * scale, origin.y + y * scale);
    };
    const ImVec2 top = point(32, 7), left = point(8, 21), center = point(32, 35);
    const ImVec2 right = point(56, 21), lowerLeft = point(8, 46);
    const ImVec2 bottom = point(32, 60), lowerRight = point(56, 46);
    draw->AddLine(top, left, cyan, thickness);
    draw->AddLine(left, center, cyan, thickness);
    draw->AddLine(left, lowerLeft, cyan, thickness);
    draw->AddLine(lowerLeft, bottom, cyan, thickness);
    draw->AddLine(top, right, blue, thickness);
    draw->AddLine(right, center, blue, thickness);
    draw->AddLine(right, lowerRight, blue, thickness);
    draw->AddLine(lowerRight, bottom, blue, thickness);
    draw->AddLine(top, bottom, gold, thickness);
    draw->AddCircleFilled(center, 4.5f * scale, gold);
}

} // namespace

LauncherApp::LauncherApp()
    : engine::Application(LauncherWindowProps()),
      m_settings((std::filesystem::path(engine::ExecutableDir()) / "launcher.cfg").string()) {
}

void LauncherApp::OnInit() {
    const editor::branding::WindowIcon& icon = editor::branding::Icon();
    GetWindow().SetIcon(icon.width, icon.height, icon.rgba.data());
    m_imgui.Init(GetWindow());

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 7.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.WindowPadding = ImVec2(16.0f, 16.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(9.0f, 8.0f);

    LoadRecentProjects();
}

void LauncherApp::OnUpdate(float) {
    if (m_openProjectDialogRequested) {
        m_openProjectDialogRequested = false;
        const std::string file = editor::OpenFileDialog(
            "Open 3DG Project", "3DG Project", "3dgproject");
        if (!file.empty()) {
            AddRecentProject(file);
            std::string error;
            if (!LaunchEditor(file, error)) m_status = error;
        }
    }
    if (m_pickLocationRequested) {
        m_pickLocationRequested = false;
        const std::string folder = editor::PickFolderDialog("Choose project location");
        if (!folder.empty()) {
            std::snprintf(m_projectLocation.data(), m_projectLocation.size(),
                          "%s", folder.c_str());
        }
    }
}

void LauncherApp::OnRender() {
    glClearColor(0.018f, 0.027f, 0.047f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_imgui.BeginFrame();
    DrawLauncher();
    m_imgui.EndFrame();
}

void LauncherApp::OnShutdown() {
    SaveRecentProjects();
    m_imgui.Shutdown();
}

void LauncherApp::LoadRecentProjects() {
    m_projects.clear();
    const int count = std::clamp(m_settings.GetInt("recent.count", 0), 0, 20);
    for (int i = 0; i < count; ++i) {
        const std::string file = m_settings.GetString("recent." + std::to_string(i), "");
        if (!file.empty()) AddRecentProject(file);
    }

    // Include the project most recently used directly by the editor.
    const std::filesystem::path editorConfig =
        std::filesystem::path(engine::ExecutableDir()) / "editor.cfg";
    engine::Config config(editorConfig.string());
    const std::string current = config.GetString("editor.current_project", "");
    if (!current.empty()) AddRecentProject(current);
}

void LauncherApp::SaveRecentProjects() {
    m_settings.Set("recent.count", static_cast<int>(m_projects.size()));
    for (int i = 0; i < 20; ++i) {
        m_settings.Set("recent." + std::to_string(i),
            i < static_cast<int>(m_projects.size())
                ? m_projects[static_cast<std::size_t>(i)].file : std::string());
    }
    m_settings.Save((std::filesystem::path(engine::ExecutableDir()) / "launcher.cfg").string());
}

void LauncherApp::AddRecentProject(const std::string& projectFile) {
    const std::filesystem::path normalized = NormalizeProjectFile(projectFile);
    const std::string file = normalized.string();
    m_projects.erase(std::remove_if(m_projects.begin(), m_projects.end(),
        [&](const ProjectEntry& entry) { return entry.file == file; }), m_projects.end());

    std::error_code ec;
    ProjectEntry entry;
    entry.file = file;
    entry.root = normalized.parent_path().string();
    entry.available = std::filesystem::is_regular_file(normalized, ec);
    entry.name = entry.available ? ProjectNameFromFile(normalized)
                                 : normalized.parent_path().filename().string();
    m_projects.insert(m_projects.begin(), std::move(entry));
    if (m_projects.size() > 20) m_projects.resize(20);
    m_selectedProject = 0;
    SaveRecentProjects();
}

bool LauncherApp::LaunchEditor(const std::string& projectFile, std::string& error) {
    const std::filesystem::path project = NormalizeProjectFile(projectFile);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(project, ec)) {
        error = "The selected project file no longer exists.";
        return false;
    }
    const std::filesystem::path editorPath =
        std::filesystem::path(engine::ExecutableDir()) / "3DGEditor.exe";
    if (!std::filesystem::is_regular_file(editorPath, ec)) {
        error = "3DGEditor.exe was not found beside the launcher.";
        return false;
    }

#if defined(_WIN32)
    const std::wstring editor = Utf8ToWide(editorPath.string());
    const std::wstring selected = Utf8ToWide(project.string());
    std::wstring command = L"\"" + editor + L"\" \"" + selected + L"\"";
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const std::wstring workingDirectory = Utf8ToWide(engine::ExecutableDir());
    if (!CreateProcessW(editor.c_str(), mutableCommand.data(), nullptr, nullptr,
            FALSE, 0, nullptr, workingDirectory.c_str(), &startup, &process)) {
        error = "Windows could not start the editor (error "
            + std::to_string(GetLastError()) + ").";
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
#else
    const std::string command = "\"" + editorPath.string() + "\" \""
        + project.string() + "\" &";
    if (std::system(command.c_str()) != 0) {
        error = "Could not start the editor.";
        return false;
    }
#endif

    AddRecentProject(project.string());
    GetWindow().SetShouldClose(true);
    return true;
}

void LauncherApp::DrawLauncher() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("3DG Launcher##Root", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoSavedSettings);

    const ImVec2 logoOrigin = ImGui::GetCursorScreenPos();
    DrawBrandMark(ImGui::GetWindowDrawList(), logoOrigin, 0.82f);
    ImGui::Dummy(ImVec2(62.0f, 54.0f));
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::SetWindowFontScale(1.55f);
    ImGui::TextUnformatted("3DG Engine");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Choose a project to begin creating.");
    ImGui::EndGroup();

    ImGui::Separator();
    if (editor::icons::LabeledButton(editor::icons::Open, "Browse Project"))
        m_openProjectDialogRequested = true;
    ImGui::SameLine();
    if (editor::icons::LabeledButton(editor::icons::Add, "Create Project"))
        m_showCreateProject = true;
    ImGui::SameLine();
    if (editor::icons::LabeledButton(editor::icons::Refresh, "Refresh"))
        LoadRecentProjects();

    ImGui::Spacing();
    DrawProjectList();
    if (!m_status.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.48f, 0.30f, 1.0f), "%s", m_status.c_str());
    }
    DrawCreateProject();
    ImGui::End();
}

void LauncherApp::DrawProjectList() {
    ImGui::TextUnformatted("Recent Projects");
    ImGui::BeginChild("RecentProjects", ImVec2(0.0f, -52.0f), true);
    int removeIndex = -1;
    for (int i = 0; i < static_cast<int>(m_projects.size()); ++i) {
        const ProjectEntry& project = m_projects[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const bool selected = i == m_selectedProject;
        if (selected) {
            ImGui::GetWindowDrawList()->AddRectFilled(start,
                ImVec2(start.x + width, start.y + 62.0f),
                IM_COL32(31, 73, 125, 125), 5.0f);
        }
        if (ImGui::Selectable("##Project", selected,
                ImGuiSelectableFlags_AllowDoubleClick, ImVec2(width, 62.0f))) {
            m_selectedProject = i;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && project.available) {
                std::string error;
                if (!LaunchEditor(project.file, error)) m_status = error;
            }
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            ImGui::OpenPopup("ProjectContext");
        ImGui::SetCursorScreenPos(ImVec2(start.x + 14.0f, start.y + 8.0f));
        ImGui::TextColored(project.available
                ? ImVec4(0.82f, 0.92f, 1.0f, 1.0f)
                : ImVec4(0.95f, 0.45f, 0.35f, 1.0f),
            "%s", project.name.c_str());
        ImGui::SetCursorScreenPos(ImVec2(start.x + 14.0f, start.y + 34.0f));
        ImGui::TextDisabled("%s%s", project.root.c_str(),
            project.available ? "" : "  (missing)");
        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + 66.0f));
        if (ImGui::BeginPopup("ProjectContext")) {
            if (project.available
                && editor::icons::MenuItem(editor::icons::Open, "Open")) {
                std::string error;
                if (!LaunchEditor(project.file, error)) m_status = error;
            }
            if (editor::icons::MenuItem(editor::icons::Delete, "Remove from list"))
                removeIndex = i;
            ImGui::EndPopup();
        }
        // SetCursorScreenPos() no longer implicitly extends child boundaries in
        // ImGui 1.92+. Submit a real layout item at the end of the project card.
        ImGui::Dummy(ImVec2(width, 0.0f));
        ImGui::PopID();
    }
    if (m_projects.empty())
        ImGui::TextDisabled("No recent projects yet. Browse for one or create a new project.");
    if (removeIndex >= 0) {
        m_projects.erase(m_projects.begin() + removeIndex);
        m_selectedProject = std::min(m_selectedProject,
            static_cast<int>(m_projects.size()) - 1);
        SaveRecentProjects();
    }
    ImGui::EndChild();

    const bool canOpen = m_selectedProject >= 0
        && m_selectedProject < static_cast<int>(m_projects.size())
        && m_projects[static_cast<std::size_t>(m_selectedProject)].available;
    ImGui::BeginDisabled(!canOpen);
    if (editor::icons::LabeledButton(editor::icons::Play, "Open Selected Project",
            ImVec2(190.0f, 0.0f))) {
        std::string error;
        if (!LaunchEditor(m_projects[static_cast<std::size_t>(m_selectedProject)].file,
                error)) m_status = error;
    }
    ImGui::EndDisabled();
}

void LauncherApp::DrawCreateProject() {
    if (m_showCreateProject) {
        ImGui::OpenPopup("Create Project");
        m_showCreateProject = false;
    }
    ImGui::SetNextWindowSize(ImVec2(570.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Create Project", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Create an empty 3DG project with an organized Content folder.");
        ImGui::InputText("Project Name", m_projectName.data(), m_projectName.size());
        ImGui::InputText("Location", m_projectLocation.data(), m_projectLocation.size());
        ImGui::SameLine();
        if (editor::icons::LabeledButton(editor::icons::Folder, "Browse##Location"))
            m_pickLocationRequested = true;

        const bool valid = m_projectName[0] != '\0' && m_projectLocation[0] != '\0';
        ImGui::BeginDisabled(!valid);
        if (editor::icons::LabeledButton(editor::icons::Add, "Create and Open")) {
            const std::filesystem::path root =
                std::filesystem::path(m_projectLocation.data()) / m_projectName.data();
            EditorProject project;
            std::string error;
            if (project.CreateProject(root.string(), m_projectName.data(), &error)) {
                AddRecentProject(project.ProjectFilePath());
                if (LaunchEditor(project.ProjectFilePath(), error)) {
                    ImGui::CloseCurrentPopup();
                } else {
                    m_status = error;
                }
            } else {
                m_status = "Project creation failed: " + error;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
