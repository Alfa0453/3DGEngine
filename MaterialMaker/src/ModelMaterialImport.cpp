#include "MaterialMaker/ModelMaterialImport.h"

#include <engine/assets/SkeletalAsset.h>
#include <engine/assets/StaticMeshAsset.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>

#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <system_error>
#include <vector>

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

std::string Extension(const std::string& path) {
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

// Write BGRA bottom-up pixels as an uncompressed 32-bit TGA (the engine loads TGA
// natively — same format the ORM packer emits).
bool WriteTga32(const std::string& path, int w, int h,
                const std::vector<unsigned char>& bgraBottomUp) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    unsigned char header[18] = {0};
    header[2]  = 2;                                        // uncompressed true-colour
    header[12] = static_cast<unsigned char>(w & 0xFF);
    header[13] = static_cast<unsigned char>((w >> 8) & 0xFF);
    header[14] = static_cast<unsigned char>(h & 0xFF);
    header[15] = static_cast<unsigned char>((h >> 8) & 0xFF);
    header[16] = 32;                                       // bits per pixel
    header[17] = 8;                                        // 8 alpha bits, bottom-up origin
    out.write(reinterpret_cast<const char*>(header), sizeof(header));
    out.write(reinterpret_cast<const char*>(bgraBottomUp.data()),
              static_cast<std::streamsize>(bgraBottomUp.size()));
    return static_cast<bool>(out);
}

// Extract one embedded engine-mesh texture (bottom-up RGBA8) to a TGA in `outputDir`
// and return its absolute path, or "" when the slot is empty / cannot be written.
// This is why importing from a .3dgmesh/.3dgskmesh used to lose all textures: the
// maps live *inside* the asset rather than as external files, so they must be
// unpacked to disk before the material can reference them.
std::string ExtractEmbeddedTexture(
    const std::vector<engine::StaticMeshTextureData>& textures,
    int index, const std::string& outputDir, const std::string& stem) {
    if (index < 0 || outputDir.empty()
        || index >= static_cast<int>(textures.size())) {
        return std::string();
    }
    const engine::StaticMeshTextureData& tex = textures[static_cast<std::size_t>(index)];
    const int w = static_cast<int>(tex.width);
    const int h = static_cast<int>(tex.height);
    if (w <= 0 || h <= 0 || w > 65535 || h > 65535
        || tex.rgba.size() < static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u) {
        return std::string();
    }
    // Engine textures are bottom-up RGBA8; TGA true-colour expects BGRA — swap R/B.
    std::vector<unsigned char> bgra(tex.rgba.size());
    for (std::size_t p = 0; p + 3 < tex.rgba.size(); p += 4) {
        bgra[p + 0] = tex.rgba[p + 2];
        bgra[p + 1] = tex.rgba[p + 1];
        bgra[p + 2] = tex.rgba[p + 0];
        bgra[p + 3] = tex.rgba[p + 3];
    }
    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);
    const std::string safeStem = stem.empty() ? std::string("texture") : stem;
    const std::string path =
        (std::filesystem::path(outputDir) / (safeStem + ".tga")).string();
    if (!WriteTga32(path, w, h, bgra)) return std::string();
    return path;
}

MaterialDocument NativeMaterial(
    const engine::StaticMeshMaterialData& source) {
    MaterialDocument document;
    document.name = source.name.empty() ? "ImportedMaterial" : source.name;
    document.albedo = source.diffuse;
    document.emissive = source.emissive;
    // Convert legacy/native Phong gloss to a useful PBR roughness estimate.
    document.roughness = std::clamp(
        std::sqrt(2.0f / std::max(source.shininess + 2.0f, 2.0f)),
        0.02f, 1.0f);
    return document;
}

} // namespace

int CountModelMaterials(const std::string& modelPath, std::string* error) {
    const std::string extension = Extension(modelPath);
    if (extension == ".3dgmesh") {
        engine::StaticMeshAssetData asset;
        if (!engine::LoadStaticMeshAsset(modelPath, &asset, error)) return 0;
        if (error) error->clear();
        return static_cast<int>(asset.materials.size());
    }
    if (extension == ".3dgskmesh") {
        engine::SkeletalMeshAssetData asset;
        if (!engine::LoadSkeletalMeshAsset(modelPath, &asset, error)) return 0;
        if (error) error->clear();
        return static_cast<int>(asset.materials.size());
    }
    Assimp::Importer importer;
    const aiScene* scene = Read(importer, modelPath, error);
    if (!scene) {
        return 0;
    }
    if (error) error->clear();
    return static_cast<int>(scene->mNumMaterials);
}

ModelImportResult ImportMaterialFromModel(const std::string& modelPath, int materialIndex,
                                          MaterialDocument* out,
                                          const std::string& outputDir) {
    ModelImportResult result;
    if (!out) {
        result.error = "output material pointer was null.";
        return result;
    }

    const std::string extension = Extension(modelPath);
    if (extension == ".3dgmesh" || extension == ".3dgskmesh") {
        std::vector<engine::StaticMeshMaterialData>* materials = nullptr;
        std::vector<engine::StaticMeshTextureData>* textures = nullptr;
        engine::StaticMeshAssetData staticAsset;
        engine::SkeletalMeshAssetData skeletalAsset;
        if (extension == ".3dgmesh") {
            if (!engine::LoadStaticMeshAsset(modelPath, &staticAsset, &result.error))
                return result;
            materials = &staticAsset.materials;
            textures = &staticAsset.textures;
        } else {
            if (!engine::LoadSkeletalMeshAsset(modelPath, &skeletalAsset, &result.error))
                return result;
            materials = &skeletalAsset.materials;
            textures = &skeletalAsset.textures;
        }
        result.materialCount = static_cast<int>(materials->size());
        if (materialIndex < 0 || materialIndex >= result.materialCount) {
            result.error = "material index out of range.";
            return result;
        }
        const engine::StaticMeshMaterialData& src =
            (*materials)[static_cast<std::size_t>(materialIndex)];
        MaterialDocument doc = NativeMaterial(src);
        // Unpack the material's embedded texture maps to disk so the imported
        // material actually references them (previously they were dropped).
        const std::string stem = SanitizeFileStem(doc.name);
        doc.albedoMap = ExtractEmbeddedTexture(*textures, src.diffuseMap, outputDir,
                                               stem + "_albedo");
        doc.normalMap = ExtractEmbeddedTexture(*textures, src.normalMap, outputDir,
                                               stem + "_normal");
        *out = doc;
        result.ok = true;
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
