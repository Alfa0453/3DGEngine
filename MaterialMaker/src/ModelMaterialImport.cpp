#include "MaterialMaker/ModelMaterialImport.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>

#include <filesystem>
#include <system_error>

namespace material_maker {
namespace {

// Resolve one texture of `type` on `mat` to an absolute file path against the model
// directory. Returns "" if absent, embedded ("*0"), or the file cannot be found.
std::string TexturePath(const aiMaterial* mat, aiTextureType type, const std::string& modelDir) {
    if (mat->GetTextureCount(type) == 0) {
        return std::string();
    }
    aiString tpath;
    if (mat->GetTexture(type, 0, &tpath) != AI_SUCCESS) {
        return std::string();
    }
    const std::string raw = tpath.C_Str();
    if (raw.empty() || raw[0] == '*') {   // embedded texture — skip
        return std::string();
    }
    std::error_code ec;
    const std::filesystem::path abs = std::filesystem::path(modelDir) / raw;
    if (std::filesystem::exists(abs, ec)) {
        return abs.string();
    }
    if (std::filesystem::exists(raw, ec)) {   // already absolute / valid as-is
        return raw;
    }
    return std::string();
}

const aiScene* Read(Assimp::Importer& importer, const std::string& path, std::string* error) {
    const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate);
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        if (error) *error = importer.GetErrorString();
        return nullptr;
    }
    return scene;
}

} // namespace

int CountModelMaterials(const std::string& modelPath, std::string* error) {
    Assimp::Importer importer;
    const aiScene* scene = Read(importer, modelPath, error);
    if (!scene) {
        return 0;
    }
    if (error) error->clear();
    return static_cast<int>(scene->mNumMaterials);
}

ModelImportResult ImportMaterialFromModel(const std::string& modelPath, int materialIndex,
                                          MaterialDocument* out) {
    ModelImportResult result;
    if (!out) {
        result.error = "output material pointer was null.";
        return result;
    }

    Assimp::Importer importer;
    const aiScene* scene = Read(importer, modelPath, &result.error);
    if (!scene) {
        return result;
    }

    result.materialCount = static_cast<int>(scene->mNumMaterials);
    if (materialIndex < 0 || materialIndex >= result.materialCount) {
        result.error = "material index out of range.";
        return result;
    }

    const aiMaterial* am = scene->mMaterials[static_cast<unsigned>(materialIndex)];
    const std::string modelDir = std::filesystem::path(modelPath).parent_path().string();

    MaterialDocument doc;   // start from defaults, then overwrite what the model provides

    aiString name;
    if (am->Get(AI_MATKEY_NAME, name) == AI_SUCCESS && name.length > 0) {
        doc.name = name.C_Str();
    }

    // Base colour (glTF PBR) with a diffuse fallback (Phong / OBJ).
    aiColor4D base;
    aiColor3D diffuse;
    if (am->Get(AI_MATKEY_BASE_COLOR, base) == AI_SUCCESS) {
        doc.albedo = {base.r, base.g, base.b};
    } else if (am->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS) {
        doc.albedo = {diffuse.r, diffuse.g, diffuse.b};
    }

    float metallic = 0.0f;
    if (am->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS) {
        doc.metallic = metallic;
    }
    float roughness = 0.5f;
    if (am->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) {
        doc.roughness = roughness;
    }

    aiColor3D emissive;
    if (am->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS) {
        doc.emissive = {emissive.r, emissive.g, emissive.b};
    }

    // Texture maps (external only; embedded are skipped).
    doc.albedoMap = TexturePath(am, aiTextureType_BASE_COLOR, modelDir);
    if (doc.albedoMap.empty()) {
        doc.albedoMap = TexturePath(am, aiTextureType_DIFFUSE, modelDir);
    }
    doc.normalMap = TexturePath(am, aiTextureType_NORMALS, modelDir);
    if (doc.normalMap.empty()) {
        doc.normalMap = TexturePath(am, aiTextureType_HEIGHT, modelDir);   // OBJ bump
    }
    doc.metalRoughMap = TexturePath(am, aiTextureType_METALNESS, modelDir);
    if (doc.metalRoughMap.empty()) {
        doc.metalRoughMap = TexturePath(am, aiTextureType_DIFFUSE_ROUGHNESS, modelDir);
    }
    if (doc.metalRoughMap.empty()) {
        doc.metalRoughMap = TexturePath(am, aiTextureType_UNKNOWN, modelDir);   // glTF packs ORM here
    }

    *out = doc;
    result.ok = true;
    return result;
}

} // namespace material_maker
