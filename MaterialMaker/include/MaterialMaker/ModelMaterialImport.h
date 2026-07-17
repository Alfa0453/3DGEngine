#pragma once

#include "MaterialMaker/MaterialDocument.h"

#include <string>

namespace material_maker {

struct ModelImportResult {
    bool        ok = false;
    int         materialCount = 0;
    std::string metallicMap;
    std::string roughnessMap;
    std::string combinedMetalRoughMap;
    std::string aoMap;
    std::string error;
};

// Read the material at `materialIndex` from a model file (glTF / OBJ / FBX / ...)
// via Assimp and populate `out`: base colour -> albedo, metallic/roughness factors,
// emissive colour, and external texture maps (base-colour, normal, metal-rough)
// resolved to absolute paths against the model's directory. Embedded textures are
// skipped (their slots are left empty). Returns ok=false with an error on failure.
ModelImportResult ImportMaterialFromModel(const std::string& modelPath,
                                          int materialIndex,
                                          MaterialDocument* out);

// Number of materials in a model (for a selector); 0 with `error` set on failure.
int CountModelMaterials(const std::string& modelPath, std::string* error);

} // namespace material_maker
