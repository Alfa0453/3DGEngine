#pragma once

#include "EditorScene.h"

#include <string>

class RuntimeSceneExporter {
public:
    static bool Export(const EditorScene& scene, const std::string& path, std::string* error);
};