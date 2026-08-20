#pragma once

#include <functional>
#include <string>
#include <vector>

enum class DirtyDocumentType {
    Scene,
    Asset,
    Script,
    ProjectSettings,
    EditorDocument
};

struct DirtyDocument {
    DirtyDocumentType type = DirtyDocumentType::EditorDocument;
    std::string displayName;
    std::string identifier;
    bool canSave = true;
    std::function<bool(std::string*)> save;
};

inline bool SaveDirtyDocuments(std::vector<DirtyDocument>& documents,
                               std::string* failedDocument,
                               std::string* error) {
    for (DirtyDocument& document : documents) {
        if (!document.canSave || !document.save) {
            if (failedDocument) *failedDocument = document.displayName;
            if (error) *error = "No save route is available";
            return false;
        }
        std::string saveError;
        if (!document.save(&saveError)) {
            if (failedDocument) *failedDocument = document.displayName;
            if (error) *error = saveError;
            return false;
        }
    }
    return true;
}
