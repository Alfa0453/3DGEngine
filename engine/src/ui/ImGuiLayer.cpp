#include "engine/ui/ImGuiLayer.h"

#include "engine/core/Window.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <cstdlib>
#include <filesystem>
#include <string>

engine::ImGuiLayer::~ImGuiLayer()
{
    Shutdown();
}

void engine::ImGuiLayer::Init(Window &window)
{
    if (m_initialized) {
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    // ImGui 1.92+ does not allow an explicitly sized merged source to target a
    // font whose reference size was implicit. Keep the base and icon sources at
    // the same explicit reference size so the atlas can safely merge them.
    constexpr float editorFontSize = 13.0f;
    ImFontConfig textConfig;
    textConfig.SizePixels = editorFontSize;
    io.Fonts->AddFontDefault(&textConfig);
#ifdef _WIN32
    std::string windowsDirectory = "C:\\Windows";
#ifdef _MSC_VER
    char* environmentDirectory = nullptr;
    std::size_t environmentLength = 0;
    if (_dupenv_s(&environmentDirectory, &environmentLength, "WINDIR") == 0
        && environmentDirectory) {
        windowsDirectory = environmentDirectory;
        std::free(environmentDirectory);
    }
#else
    if (const char* environmentDirectory = std::getenv("WINDIR"))
        windowsDirectory = environmentDirectory;
#endif
    const std::filesystem::path iconFontPath =
        std::filesystem::path(windowsDirectory) / "Fonts" / "segmdl2.ttf";
    std::error_code fontError;
    if (std::filesystem::is_regular_file(iconFontPath, fontError)) {
        ImFontConfig iconConfig;
        iconConfig.MergeMode = true;
        iconConfig.PixelSnapH = true;
        iconConfig.GlyphMinAdvanceX = editorFontSize;
        iconConfig.OversampleH = 2;
        static const ImWchar iconRanges[] = {
            0xE70E, 0xE710, 0xE713, 0xE714, 0xE71A, 0xE71B,
            0xE72B, 0xE72C, 0xE735, 0xE735, 0xE74D, 0xE74E,
            0xE768, 0xE768, 0xE77B, 0xE77B, 0xE77F, 0xE77F,
            0xE790, 0xE790, 0xE7B8, 0xE7B8, 0xE81E, 0xE81E,
            0xE896, 0xE896, 0xE8A5, 0xE8A5, 0xE8B7, 0xE8B7,
            0xE8BE, 0xE8BE, 0xE8C6, 0xE8C8, 0xE8D6, 0xE8D6,
            0xE8E5, 0xE8E5, 0xE909, 0xE909, 0xE91B, 0xE91B,
            0xE943, 0xE943, 0xE9D2, 0xE9D2, 0
        };
        io.Fonts->AddFontFromFileTTF(iconFontPath.string().c_str(), editorFontSize,
            &iconConfig, iconRanges);
    }
#endif

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window.Native(), true);
    ImGui_ImplOpenGL3_Init("#version 330");

    m_initialized = true;
}

void engine::ImGuiLayer::BeginFrame()
{
    if (!m_initialized) {
        return;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void engine::ImGuiLayer::EndFrame()
{
    if (!m_initialized) {
        return;
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    ImGuiIO& io = ImGui::GetIO();
    if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
        GLFWwindow* backup = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup);
    }
}

void engine::ImGuiLayer::Shutdown()
{
    if (!m_initialized) {
        return;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    m_initialized = false;
}
