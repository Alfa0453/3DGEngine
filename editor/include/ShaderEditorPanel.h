#pragma once

#include <engine/assets/RuntimeShaderManager.h>
#include <engine/graphics/Framebuffer.h>
#include <engine/graphics/Mesh.h>
#include <engine/graphics/Model.h>

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class EditorAssets;

// Isolated shader authoring document and live preview. All GL resources are
// lazy-created while Draw() has a current editor context.
class ShaderEditorPanel {
public:
    ~ShaderEditorPanel();
    void Draw(EditorAssets& assets, bool* open);
    void QueueOpen(const std::string& path);
    bool WantsKeyboard() const { return m_keyboardFocused; }

private:
    enum class PreviewShape { Sphere, Cube, Plane, ImportedModel };

    void NewDocument();
    bool OpenDocument(const std::string& path);
    bool SaveDocument(EditorAssets& assets, bool saveAs);
    bool DuplicateDocument(EditorAssets& assets);
    void RequestNew();
    void GenerateSources();
    void DrawGraphCanvas();
    void DrawGraphInspector();
    void AddGraphNode(const std::string& type, float x, float y);
    void DeleteSelectedNodes();
    void DuplicateSelectedNodes();
    void CopySelectedNodes();
    void PasteNodes();
    void PushUndo();
    void UndoGraph();
    void RedoGraph();
    void Compile(bool force = false);
    void RequestOpen(const std::string& path);
    unsigned int RenderPreview(int width, int height);
    void EnsurePreviewResources();
    std::string NumberedSource(const std::string& source) const;

    engine::ShaderAsset m_asset;
    engine::ShaderAsset m_savedAsset;
    engine::RuntimeShaderManager m_runtime;
    std::string m_path;
    std::string m_vertexSource;
    std::string m_fragmentSource;
    std::string m_lastValidVertex;
    std::string m_lastValidFragment;
    std::string m_status{"Create or open a shader asset."};
    std::string m_error;
    std::string m_pendingOpenPath;
    std::string m_externalOpenPath;
    std::array<char, 160> m_name{};
    std::array<char, 96> m_sourceSearch{};
    bool m_dirty = false;
    bool m_pendingNew = false;
    bool m_closeAfterPrompt = false;
    bool m_promptQueued = false;
    bool m_autoCompile = true;
    bool m_compilePending = true;
    bool m_applied = false;
    double m_compileMilliseconds = 0.0;
    int m_selectedDiagnostic = -1;
    int m_sourceTab = 0;
    std::unordered_set<std::uint64_t> m_selectedNodes;
    std::uint64_t m_linkPin = 0;
    std::uint64_t m_hoveredPin = 0;
    std::uint64_t m_nodePromptPin = 0;
    std::uint64_t m_contextNode = 0;
    std::array<char, 96> m_nodeSearch{};
    float m_graphPanX = 0.0f;
    float m_graphPanY = 0.0f;
    float m_graphZoom = 1.0f;
    float m_nodePopupX = 0.0f;
    float m_nodePopupY = 0.0f;
    std::vector<engine::ShaderAsset> m_undo;
    std::vector<engine::ShaderAsset> m_redo;
    std::unordered_map<int, std::uint64_t> m_fragmentLineNodes;
    engine::ShaderAsset m_graphClipboard;

    PreviewShape m_shape = PreviewShape::Sphere;
    float m_yaw = 35.0f;
    float m_pitch = 18.0f;
    float m_distance = 3.2f;
    float m_timeOfDay = 0.42f;
    float m_environmentYaw = 0.0f;
    float m_lightIntensity = 1.0f;
    float m_previewObjectColor[4]{0.68f, 0.32f, 0.12f, 1.0f};
    float m_background[3]{0.05f, 0.06f, 0.08f};
    bool m_ground = true;
    bool m_bloom = true;
    bool m_keyboardFocused = false;

    std::optional<engine::Framebuffer> m_framebuffer;
    std::optional<engine::Framebuffer> m_postInput;
    std::optional<engine::Mesh> m_sphere;
    std::optional<engine::Mesh> m_cube;
    std::optional<engine::Mesh> m_plane;
    std::optional<engine::Mesh> m_groundMesh;
    std::optional<engine::Mesh> m_fullscreenQuad;
    std::unique_ptr<engine::Shader> m_previewSurface;
    std::unique_ptr<engine::Model> m_importedModel;
    std::string m_importedModelPath;
    unsigned int m_previewNormalTexture = 0;
    unsigned int m_previewVelocityTexture = 0;
};
