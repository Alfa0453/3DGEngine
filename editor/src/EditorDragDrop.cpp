#include "EditorDragDrop.h"

void EditorDragDrop::BeginAssetDrag(const std::string &path, const std::string &typeName)
{
    m_payload.type = PayloadType::Asset;
    m_payload.path = path;
    m_payload.typeName = typeName;
}

void EditorDragDrop::Clear()
{
    m_payload = Payload{};
}
