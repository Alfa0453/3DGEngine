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
    };

    void BeginAssetDrag(const std::string& path, const std::string& typeName);
    void Clear();

    bool HasPayload() const { return m_payload.type != PayloadType::None; }
    const Payload& CurrentPayload() const { return m_payload; }

private:
    Payload m_payload;
};