#pragma once

#include <engine/core/Application.h>
#include <engine/core/Config.h>
#include <engine/ui/ImGuiLayer.h>

#include <array>
#include <string>
#include <vector>

class LauncherApp final : public engine::Application {
public:
    LauncherApp();

protected:
    void OnInit() override;
    void OnUpdate(float deltaTime) override;
    void OnRender() override;
    void OnShutdown() override;

private:
    struct ProjectEntry {
        std::string name;
        std::string file;
        std::string root;
        bool available = false;
    };

    void LoadRecentProjects();
    void SaveRecentProjects();
    void AddRecentProject(const std::string& projectFile);
    bool LaunchEditor(const std::string& projectFile, std::string& error);
    void DrawLauncher();
    void DrawProjectList();
    void DrawCreateProject();

    engine::ImGuiLayer m_imgui;
    engine::Config m_settings;
    std::vector<ProjectEntry> m_projects;
    int m_selectedProject = -1;
    std::array<char, 128> m_projectName{};
    std::array<char, 1024> m_projectLocation{};
    std::string m_status;
    bool m_openProjectDialogRequested = false;
    bool m_pickLocationRequested = false;
    bool m_showCreateProject = false;
};
