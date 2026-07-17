#include "MaterialMaker/ModelMaterialImport.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>

#include <filesystem>
#include <algorithm>
#include <cctype>
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

bool IsGltf(const std::string& path) {
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".gltf" || extension == ".glb";
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
        doc.opacity = base.a;
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
    float value = 0.0f;
    if (am->Get(AI_MATKEY_OPACITY, value) == AI_SUCCESS) doc.opacity *= value;
    if (am->Get(AI_MATKEY_REFRACTI, value) == AI_SUCCESS) doc.ior = value;
    if (am->Get(AI_MATKEY_CLEARCOAT_FACTOR, value) == AI_SUCCESS) doc.clearcoat = value;
    if (am->Get(AI_MATKEY_CLEARCOAT_ROUGHNESS_FACTOR, value) == AI_SUCCESS) doc.clearcoatRoughness = value;
    if (am->Get(AI_MATKEY_TRANSMISSION_FACTOR, value) == AI_SUCCESS) doc.transmission = value;
    if (am->Get(AI_MATKEY_VOLUME_THICKNESS_FACTOR, value) == AI_SUCCESS) doc.thickness = value;
    if (am->Get(AI_MATKEY_ANISOTROPY_FACTOR, value) == AI_SUCCESS) doc.anisotropy = value;
    if (am->Get(AI_MATKEY_SHEEN_ROUGHNESS_FACTOR, value) == AI_SUCCESS) doc.sheenRoughness = value;
    if (am->Get(AI_MATKEY_SPECULAR_FACTOR, value) == AI_SUCCESS) doc.specularLevel = value;
    aiColor3D sheen;
    if (am->Get(AI_MATKEY_SHEEN_COLOR_FACTOR, sheen) == AI_SUCCESS)
        doc.sheenColor = {sheen.r, sheen.g, sheen.b};
    if (doc.opacity < 0.999f || doc.transmission > 0.0f) doc.blendMode = 2;

    // Texture maps (external only; embedded are skipped).
    doc.albedoMap = TexturePath(am, aiTextureType_BASE_COLOR, modelDir);
    if (doc.albedoMap.empty()) {
        doc.albedoMap = TexturePath(am, aiTextureType_DIFFUSE, modelDir);
    }
    doc.normalMap = TexturePath(am, aiTextureType_NORMALS, modelDir);
    if (doc.normalMap.empty()) {
        doc.normalMap = TexturePath(am, aiTextureType_HEIGHT, modelDir);   // OBJ bump
    }
    doc.heightMap = TexturePath(am, aiTextureType_DISPLACEMENT, modelDir);
    result.metallicMap = TexturePath(am, aiTextureType_METALNESS, modelDir);
    result.roughnessMap = TexturePath(am, aiTextureType_DIFFUSE_ROUGHNESS, modelDir);
    result.aoMap = TexturePath(am, aiTextureType_AMBIENT_OCCLUSION, modelDir);
    if (result.aoMap.empty()) {
        result.aoMap = TexturePath(am, aiTextureType_LIGHTMAP, modelDir);
    }

    // Assimp reports a glTF metallic-roughness texture in both semantic slots.
    // Only pass it through directly when both slots resolve to the same file;
    // separate grayscale inputs must be channel-packed by the panel.
    if (!result.metallicMap.empty() && result.metallicMap == result.roughnessMap) {
        result.combinedMetalRoughMap = result.metallicMap;
        result.metallicMap.clear();
        result.roughnessMap.clear();
    } else if (result.metallicMap.empty() && result.roughnessMap.empty() && IsGltf(modelPath)) {
        result.combinedMetalRoughMap = TexturePath(am, aiTextureType_UNKNOWN, modelDir);
    }

    *out = doc;
    result.ok = true;
    return result;
}

} // namespace material_maker
