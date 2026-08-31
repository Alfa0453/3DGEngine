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
    // True when this represents real content: a scene, or an asset already written to the Content
    // folder (it has a file path). False for a still-open editor panel whose asset was never saved
    // to Content ("New Material", etc.) -- those should not force a save prompt on their own.
    bool existsInContent = false;
    // Per-item choice in the save prompt (checkbox). Defaults to on for content, off for unsaved
    // editor panels, so closing doesn't nag you to save a panel you only opened to look at.
    bool selectedToSave = true;
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
