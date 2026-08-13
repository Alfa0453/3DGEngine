#pragma once

#include "EditorAssets.h"
#include "EditorScene.h"
#include <engine/assets/DayNightTimelineAsset.h>

#include <string>

class DayNightTimelinePanel {
public:
    struct Result { bool saved = false; std::string message; };
    void QueueOpen(std::string path) { m_pendingOpen = std::move(path); }
    Result Draw(EditorScene& scene, EditorAssets& assets,
                const std::string& contentRoot, bool* open);
private:
    void NewTimeline(const std::string& root);
    void ApplySample(EditorScene& scene) const;
    void CaptureKey(const EditorScene::Environment& environment,
                    engine::DayNightKeyframe& key) const;
    void DrawPreview() const;
    engine::DayNightTimelineAssetData m_timeline;
    std::string m_path;
    std::string m_pendingOpen;
    std::string m_status;
    int m_selectedKey = 0;
    float m_time = 0.25f;
    bool m_playing = false;
    bool m_previewInLevel = true;
    bool m_dirty = false;
};
