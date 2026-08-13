#pragma once

#include "EditorAssets.h"
#include "EditorScene.h"
#include <engine/assets/WeatherAsset.h>

#include <string>

class WeatherEditorPanel {
public:
    struct Result { bool saved=false; bool applied=false; std::string message; };
    void QueueOpen(const std::string& path){m_pendingOpen=path;}
    Result Draw(EditorScene& scene,EditorAssets& assets,const std::string& contentRoot,bool* open);
private:
    void Capture(const EditorScene::Environment& environment);
    void Apply(EditorScene& scene)const;
    void DrawPreview(const engine::WeatherAssetData& weather);
    void Preset(engine::PrecipitationType type);
    engine::WeatherAssetData m_weather;
    engine::WeatherAssetData m_transitionFrom;
    std::string m_path,m_pendingOpen,m_status;
    bool m_dirty=false,m_playTransition=false;
    float m_transitionAlpha=1.f;
};
