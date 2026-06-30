#pragma once

namespace engine {

class Window;

class ImGuiLayer {
public:
    ImGuiLayer() = default;
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    void Init(Window& window);
    void BeginFrame();
    void EndFrame();
    void Shutdown();

    bool IsInitialized() const { return m_initialized; }

private:
    bool m_initialized = false;
};

} // namespace engine