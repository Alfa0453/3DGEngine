#include "RenderDebuggerPanel.h"
#include "EditorPanels.h"

#include <glad/glad.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iterator>

namespace {
constexpr const char* kVertex = R"GLSL(#version 330 core
out vec2 vUv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})GLSL";

constexpr const char* kFragment = R"GLSL(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTexture2D;
uniform sampler2DArray uTextureArray;
uniform int uArray;
uniform int uLayer;
uniform int uMode;
uniform int uFlipY;
uniform float uExposure;
uniform float uRangeMin;
uniform float uRangeMax;
void main() {
    vec2 uv = vec2(vUv.x, uFlipY == 1 ? 1.0 - vUv.y : vUv.y);
    vec4 sampleValue = uArray == 1
        ? texture(uTextureArray, vec3(uv, float(uLayer)))
        : texture(uTexture2D, uv);
    vec3 value;
    if (uMode == 1) {
        float v = clamp((sampleValue.r-uRangeMin)/max(uRangeMax-uRangeMin, 0.000001), 0.0, 1.0);
        value = vec3(v);
    } else if (uMode == 2) {
        value = normalize(sampleValue.xyz) * 0.5 + 0.5;
    } else if (uMode == 3) {
        value = vec3(sampleValue.rg * 0.5 + 0.5, 0.5);
    } else if (uMode == 4) {
        float d = clamp((sampleValue.r-uRangeMin)/max(uRangeMax-uRangeMin, 0.000001), 0.0, 1.0);
        value = vec3(1.0-d);
    } else if (uMode == 5) {
        value = abs(sampleValue.xyz) / (abs(sampleValue.xyz) + vec3(max(uRangeMax, 0.001)));
    } else {
        value = sampleValue.rgb * uExposure;
        value = value / (value + vec3(1.0));
        value = pow(max(value, vec3(0.0)), vec3(1.0/2.2));
    }
    fragColor = vec4(value, 1.0);
})GLSL";

const char* InterpretationName(RenderDebuggerPanel::Interpretation value) {
    using I = RenderDebuggerPanel::Interpretation;
    switch (value) {
    case I::Color: return "Color / HDR";
    case I::Scalar: return "Scalar";
    case I::Normal: return "Normal";
    case I::Velocity: return "Velocity";
    case I::Depth: return "Depth";
    case I::Position: return "View-space position";
    }
    return "Unknown";
}

constexpr const char* kLightingViews[] = {
    "Lit", "Direct Lighting", "Diffuse Indirect", "Specular Indirect",
    "Probe Irradiance", "Sky Visibility", "AO Combined", "Specular Occlusion",
    "Probe Validity", "Indirect Normal", "Raw GTAO", "Filtered GTAO",
    "Reflection Weight", "GI Direct Environment", "GI Bounce", "GI Emissive",
    "Probe Visibility", "Dynamic GI Probes", "SSGI", "Total Indirect",
    "Directional Shadow", "Shadow Cascades", "Global IBL", "GI Higher Bounces",
    "Material Base Color", "Geometric Normal", "Shading Normal",
    "Imported Material Slot", "PCSS Filter Radius"
};
}

RenderDebuggerPanel::~RenderDebuggerPanel() { ReleaseResources(); }

void RenderDebuggerPanel::ReleaseResources() {
    m_capture.reset();
    m_preview.reset();
    m_shader.reset();
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    m_vao = 0;
}

bool RenderDebuggerPanel::EnsureResources(int width, int height) {
    width = std::clamp(width, 32, 1024);
    height = std::clamp(height, 32, 1024);
    try {
        if (!m_shader) m_shader = std::make_unique<engine::Shader>(kVertex, kFragment);
        if (!m_preview) m_preview = std::make_unique<engine::Framebuffer>(width, height, GL_RGBA8, false);
        else m_preview->Resize(width, height);
        if (!m_capture) m_capture = std::make_unique<engine::Framebuffer>(width, height, GL_RGBA8, false);
        else if (!m_frozen) m_capture->Resize(width, height);
        if (!m_vao) glGenVertexArrays(1, &m_vao);
        return true;
    } catch (const std::exception& exception) {
        m_status = exception.what();
        return false;
    }
}

void RenderDebuggerPanel::RenderTexture(const TextureView& view, int layer) {
    if (!m_preview || !m_shader || !view.texture) return;
    GLint oldFbo = 0, oldProgram = 0, oldVao = 0, oldViewport[4]{};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldFbo);
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
    glGetIntegerv(GL_VIEWPORT, oldViewport);
    const GLboolean depth = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blend = glIsEnabled(GL_BLEND);
    m_preview->Bind();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(0.02f, 0.025f, 0.035f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    m_shader->Bind();
    m_shader->SetInt("uTexture2D", 0);
    m_shader->SetInt("uTextureArray", 1);
    m_shader->SetInt("uArray", view.target == TextureTarget::Texture2DArray ? 1 : 0);
    m_shader->SetInt("uLayer", layer);
    m_shader->SetInt("uMode", static_cast<int>(view.interpretation));
    m_shader->SetInt("uFlipY", m_flipY ? 1 : 0);
    m_shader->SetFloat("uExposure", m_exposure);
    m_shader->SetFloat("uRangeMin", m_rangeMin);
    m_shader->SetFloat("uRangeMax", m_rangeMax);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, view.target == TextureTarget::Texture2D ? view.texture : 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D_ARRAY, view.target == TextureTarget::Texture2DArray ? view.texture : 0);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(static_cast<GLuint>(oldVao));
    glUseProgram(static_cast<GLuint>(oldProgram));
    if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(oldFbo));
    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
}

void RenderDebuggerPanel::CapturePreview() {
    if (!m_preview || !m_capture) return;
    GLint read = 0, draw = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_preview->FboId());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_capture->FboId());
    glBlitFramebuffer(0, 0, m_preview->Width(), m_preview->Height(),
                      0, 0, m_capture->Width(), m_capture->Height(),
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw));
    m_frozen = true;
    m_status = "Captured the selected pass.";
}

void RenderDebuggerPanel::ReadPixel(bool captured, int x, int y) {
    engine::Framebuffer* source = captured ? m_capture.get() : m_preview.get();
    if (!source) return;
    GLint old = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, source->FboId());
    glReadPixels(std::clamp(x, 0, source->Width()-1),
                 std::clamp(y, 0, source->Height()-1), 1, 1, GL_RGBA, GL_FLOAT, m_pixel);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(old));
    m_hasPixel = true;
}

RenderDebuggerPanel::Result RenderDebuggerPanel::Draw(const FrameData& frame, bool* open) {
    Result result;
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::RenderDebugger), open,
                      ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return result;
    }

    ImGui::SeparatorText("Live Viewport Override");
    int debugMode = std::clamp(frame.lightingDebugMode, 0,
        static_cast<int>(std::size(kLightingViews))-1);
    ImGui::SetNextItemWidth(230.0f);
    if (ImGui::Combo("Material / Lighting View", &debugMode, kLightingViews,
                     static_cast<int>(std::size(kLightingViews))))
        result.lightingDebugMode = debugMode;
    ImGui::SameLine();
    if (ImGui::Button("Lit")) result.lightingDebugMode = 0;
    ImGui::SameLine();
    if (ImGui::Button("Use World Setting")) result.lightingDebugMode = -2;
    if (ImGui::Button("Refresh Shadow Maps")) result.refreshShadows = true;

    ImGui::SeparatorText("Render Passes");
    if (frame.textures.empty()) {
        ImGui::TextDisabled("No retained render-pass textures are available yet.");
    } else {
        m_selected = std::clamp(m_selected, 0, static_cast<int>(frame.textures.size())-1);
        const char* selectedName = frame.textures[static_cast<std::size_t>(m_selected)].name.c_str();
        ImGui::SetNextItemWidth(250.0f);
        if (ImGui::BeginCombo("Pass", selectedName)) {
            for (int index = 0; index < static_cast<int>(frame.textures.size()); ++index) {
                ImGui::PushID(index);
                if (ImGui::Selectable(frame.textures[static_cast<std::size_t>(index)].name.c_str(),
                                      index == m_selected)) {
                    m_selected = index;
                    m_layer = 0;
                    m_frozen = false;
                    m_hasPixel = false;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        const TextureView& view = frame.textures[static_cast<std::size_t>(m_selected)];
        if (view.layers > 1) ImGui::SliderInt("Layer", &m_layer, 0, view.layers-1);
        else m_layer = 0;
        ImGui::Checkbox("Flip vertically", &m_flipY);
        if (view.interpretation == Interpretation::Color)
            ImGui::DragFloat("Preview exposure", &m_exposure, 0.02f, 0.01f, 32.0f, "%.2fx");
        else if (view.interpretation == Interpretation::Scalar
                 || view.interpretation == Interpretation::Depth
                 || view.interpretation == Interpretation::Position) {
            ImGui::DragFloatRange2("Display range", &m_rangeMin, &m_rangeMax,
                0.002f, -10000.0f, 10000.0f, "Min %.3f", "Max %.3f");
            if (m_rangeMax <= m_rangeMin) m_rangeMax = m_rangeMin + 0.0001f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Capture")) CapturePreview();
        if (m_frozen) {
            ImGui::SameLine();
            if (ImGui::Button("Resume Live")) m_frozen = false;
        }
        ImGui::TextDisabled("%s | %dx%d | owner: %s", InterpretationName(view.interpretation),
                            view.width, view.height, view.owner.c_str());
        if (!view.description.empty()) ImGui::TextWrapped("%s", view.description.c_str());

        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float aspect = static_cast<float>(std::max(view.width, 1))
            / static_cast<float>(std::max(view.height, 1));
        float imageWidth = std::max(128.0f, available.x);
        float imageHeight = imageWidth / std::max(aspect, 0.01f);
        imageHeight = std::clamp(imageHeight, 128.0f, 520.0f);
        imageWidth = imageHeight * aspect;
        if (EnsureResources(std::min(view.width, 1024), std::min(view.height, 1024))) {
            if (!m_frozen) RenderTexture(view, m_layer);
            engine::Framebuffer* shown = m_frozen ? m_capture.get() : m_preview.get();
            const ImVec2 start = ImGui::GetCursorScreenPos();
            ImGui::Image((ImTextureID)(std::intptr_t)shown->ColorTexture(),
                         {imageWidth, imageHeight}, {0, 1}, {1, 0});
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Click to inspect the displayed pixel.");
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    const ImVec2 mouse = ImGui::GetIO().MousePos;
                    const float u = std::clamp((mouse.x-start.x)/imageWidth, 0.0f, 1.0f);
                    const float v = std::clamp((mouse.y-start.y)/imageHeight, 0.0f, 1.0f);
                    ReadPixel(m_frozen, static_cast<int>(u*shown->Width()),
                              static_cast<int>((1.0f-v)*shown->Height()));
                }
            }
            if (m_hasPixel) ImGui::Text("Displayed pixel RGBA: %.4f  %.4f  %.4f  %.4f",
                m_pixel[0], m_pixel[1], m_pixel[2], m_pixel[3]);
        }
    }

    ImGui::SeparatorText("Draw-call Ownership");
    ImGui::Text("Total draw calls: %d", frame.totalDrawCalls);
    if (ImGui::BeginTable("##renderDrawOwners", 2,
            ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Pass");
        ImGui::TableSetupColumn("Draws", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableHeadersRow();
        for (const auto& owner : frame.drawCalls) {
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(owner.first.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%d", owner.second);
        }
        ImGui::EndTable();
    }
    ImGui::Text("Shadow casters: %d one-sided / %d two-sided",
                frame.oneSidedShadowDraws, frame.twoSidedShadowDraws);
    ImGui::Text("Shadow memory: %.2f MB",
                static_cast<double>(frame.shadowMemoryBytes)/(1024.0*1024.0));

    ImGui::SeparatorText("GPU Pass Timings");
    double total = 0.0;
    for (const auto& timing : frame.gpuTimings) {
        ImGui::Text("%-18s %7.3f ms", timing.first.c_str(), timing.second);
        total += timing.second;
    }
    if (frame.gpuTimings.empty()) ImGui::TextDisabled("GPU queries are warming up...");
    else ImGui::Text("Tracked GPU total: %.3f ms", total);
    if (!m_status.empty()) ImGui::TextWrapped("%s", m_status.c_str());
    ImGui::End();
    return result;
}
