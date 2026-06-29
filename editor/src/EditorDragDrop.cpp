#include "EditorDragDrop.h"

void EditorDragDrop::BeginAssetDrag(const std::string &path, const std::string &typeName)
{
    m_payload.type = PayloadType::Asset;
    m_payload.path = path;
    m_payload.typeName = typeName;
}

void EditorDragDrop::BeginAssetDragAt(const std::string &path, const std::string &typeName, float x, float y)
{
    m_payload.type = PayloadType::Asset;
    m_payload.path = path;
    m_payload.typeName = typeName;
    m_payload.startX = x;
    m_payload.startY = y;
    m_payload.cursorX = x;
    m_payload.cursorY = y;
    m_payload.mouseDriven = true;
}

void EditorDragDrop::UpdateCursor(float x, float y)
{
    if (!HasPayload()) {
        return;
    }

    m_payload.cursorX = x;
    m_payload.cursorY = y;
}

void EditorDragDrop::Clear()
{
    m_payload = Payload{};
}
