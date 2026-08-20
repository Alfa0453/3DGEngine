#include "EditorDirtyDocument.h"

#include <iostream>

int main() {
    int calls=0;
    std::vector<DirtyDocument> documents;
    documents.push_back({DirtyDocumentType::Asset,"Asset","asset",true,
        [&](std::string*){++calls;return true;}});
    documents.push_back({DirtyDocumentType::Script,"Script","script",true,
        [&](std::string* error){++calls;if(error)*error="disk full";return false;}});
    documents.push_back({DirtyDocumentType::Scene,"Scene","scene",true,
        [&](std::string*){++calls;return true;}});
    std::string failed,error;
    if (SaveDirtyDocuments(documents,&failed,&error)) return 1;
    if (calls!=2 || failed!="Script" || error!="disk full") return 2;
    documents[1].save=[&](std::string*){++calls;return true;};
    calls=0;failed.clear();error.clear();
    if (!SaveDirtyDocuments(documents,&failed,&error) || calls!=3) return 3;
    std::cout << "editor dirty-document save ordering passed\n";
    return 0;
}
