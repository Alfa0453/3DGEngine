#pragma once

#include <engine/graphics/Framebuffer.h>
#include <engine/graphics/Shader.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class RenderDebuggerPanel {
public:
    enum class TextureTarget { Texture2D, Texture2DArray };
    enum class Interpretation { Color, Scalar, Normal, Velocity, Depth, Position };

    struct TextureView {
        std::string name;
        unsigned int texture = 0;
        TextureTarget target = TextureTarget::Texture2D;
        Interpretation interpretation = Interpretation::Color;
        int width = 1;
        int height = 1;
        int layers = 1;
        std::string owner;
        std::string description;
    };

    struct FrameData {
        std::vector<TextureView> textures;
        std::vector<std::pair<std::string, double>> gpuTimings;
        std::vector<std::pair<std::string, int>> drawCalls;
        int totalDrawCalls = 0;
        int oneSidedShadowDraws = 0;
        int twoSidedShadowDraws = 0;
        std::uint64_t shadowMemoryBytes = 0;
        int lightingDebugMode = 0;
    };

    struct Result {
        int lightingDebugMode = -1;
        bool refreshShadows = false;
    };

    RenderDebuggerPanel() = default;
    ~RenderDebuggerPanel();
    Result Draw(const FrameData& frame, bool* open);

private:
    bool EnsureResources(int width, int height);
    void RenderTexture(const TextureView& view, int layer);
    void CapturePreview();
    void ReadPixel(bool captured, int x, int y);
    void ReleaseResources();

    std::unique_ptr<engine::Shader> m_shader;
    std::unique_ptr<engine::Framebuffer> m_preview;
    std::unique_ptr<engine::Framebuffer> m_capture;
    unsigned int m_vao = 0;
    int m_selected = 0;
    int m_layer = 0;
    bool m_frozen = false;
    bool m_flipY = true;
    float m_exposure = 1.0f;
    float m_rangeMin = 0.0f;
    float m_rangeMax = 1.0f;
    float m_pixel[4]{0, 0, 0, 0};
    bool m_hasPixel = false;
    std::string m_status;
};
