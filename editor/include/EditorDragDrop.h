#pragma once

#include <string>

class EditorDragDrop {
public:
    enum class PayloadType {
        None,
        Asset
    };

    struct Payload {
        PayloadType type = PayloadType::None;
        std::string path;
        std::string typeName;
        float startX = 0.0f;
        float startY = 0.0f;
        float cursorX = 0.0f;
        float cursorY = 0.0f;
        bool mouseDriven = false;
    };

    void BeginAssetDrag(const std::string& path, const std::string& typeName);
    void BeginAssetDragAt(const std::string& path, const std::string& typeName, float x, float y);
    void UpdateCursor(float x, float y);
    void Clear();

    bool HasPayload() const { return m_payload.type != PayloadType::None; }
    bool IsMouseDriven() const { return HasPayload() && m_payload.mouseDriven; }
    const Payload& CurrentPayload() const { return m_payload; }

private:
    Payload m_payload;
};