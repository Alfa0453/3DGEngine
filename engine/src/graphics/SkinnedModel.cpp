// Rigged-model loader. Like Model.cpp, the Assimp path can't be compiled in the
// headless sandbox (Assimp is fetched + built by CMake), so this is written
// against the documented Assimp 5.x API and verified by the user's build. The
// animation *maths* it feeds (engine/animation/Animator) is separately unit-tested.
#include "engine/graphics/SkinnedModel.h"

#include "engine/graphics/ImageDecode.h"
#include "engine/graphics/VertexLayout.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>

namespace engine {
namespace {

glm::mat4 ToGlm(const aiMatrix4x4& a) {
    // aiMatrix4x4 is row-major; glm is column-major.
    return glm::mat4(a.a1, a.b1, a.c1, a.d1,
                     a.a2, a.b2, a.c2, a.d2,
                     a.a3, a.b3, a.c3, a.d3,
                     a.a4, a.b4, a.c4, a.d4);
}
std::string DirOf(const std::string& p) {
    const std::size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? std::string() : p.substr(0, s + 1);
}
void FlipRows(image::Image& im) {
    const std::size_t row = static_cast<std::size_t>(im.width) * 4;
    for (int y = 0; y < im.height / 2; ++y)
        std::swap_ranges(im.rgba.begin() + static_cast<std::ptrdiff_t>(y * row),
                         im.rgba.begin() + static_cast<std::ptrdiff_t>((y + 1) * row),
                         im.rgba.begin() + static_cast<std::ptrdiff_t>((im.height - 1 - y) * row));
}

// Build one Animation from an aiAnimation, remapping channels onto `boneIndex`
// (bone name -> index in `skel`). If stripRootMotion, the skeleton root bone's
// translation is frozen to its first key so a locomotion clip stays in place.
Animation BuildClip(const aiAnimation* a, const Skeleton& skel,
                    const std::map<std::string, int>& boneIndex,
                    bool stripRootMotion, const std::string& nameOverride) {
    Animation anim;
    anim.name           = nameOverride.empty() ? std::string(a->mName.C_Str()) : nameOverride;
    anim.duration       = static_cast<float>(a->mDuration);
    anim.ticksPerSecond = (a->mTicksPerSecond > 0.0) ? static_cast<float>(a->mTicksPerSecond) : 25.0f;
    anim.channels.resize(skel.bones.size());
    for (unsigned c = 0; c < a->mNumChannels; ++c) {
        const aiNodeAnim* ch = a->mChannels[c];
        const auto it = boneIndex.find(ch->mNodeName.C_Str());
        if (it == boneIndex.end()) continue;
        BoneChannel& bc = anim.channels[static_cast<std::size_t>(it->second)];
        for (unsigned k = 0; k < ch->mNumPositionKeys; ++k) {
            const aiVectorKey& key = ch->mPositionKeys[k];
            bc.positions.push_back({ static_cast<float>(key.mTime), { key.mValue.x, key.mValue.y, key.mValue.z } });
        }
        for (unsigned k = 0; k < ch->mNumRotationKeys; ++k) {
            const aiQuatKey& key = ch->mRotationKeys[k];
            bc.rotations.push_back({ static_cast<float>(key.mTime), glm::quat(key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z) });
        }
        for (unsigned k = 0; k < ch->mNumScalingKeys; ++k) {
            const aiVectorKey& key = ch->mScalingKeys[k];
            bc.scales.push_back({ static_cast<float>(key.mTime), { key.mValue.x, key.mValue.y, key.mValue.z } });
        }
    }
    if (stripRootMotion) {
        // Freeze translation on the skeleton root (a bone named "root" if present,
        // else the first parentless bone) -> removes locomotion drift.
        int rootIdx = -1;
        const auto rit = boneIndex.find("root");
        if (rit != boneIndex.end()) rootIdx = rit->second;
        else for (std::size_t i = 0; i < skel.bones.size(); ++i)
            if (skel.bones[i].parent < 0) { rootIdx = static_cast<int>(i); break; }
        if (rootIdx >= 0 && rootIdx < static_cast<int>(anim.channels.size())) {
            auto& pos = anim.channels[static_cast<std::size_t>(rootIdx)].positions;
            if (!pos.empty()) { const glm::vec3 first = pos.front().value; for (auto& kf : pos) kf.value = first; }
        }
    }
    return anim;
}

} // namespace

SkinnedModel SkinnedModel::FromFile(const std::string& path) {
    Assimp::Importer importer;
    // NB: no aiProcess_PreTransformVertices -- it removes bones. LimitBoneWeights
    // caps each vertex at 4 influences (matching our vertex format).
    const unsigned flags = aiProcess_Triangulate
                         | aiProcess_GenSmoothNormals
                         | aiProcess_CalcTangentSpace
                         | aiProcess_LimitBoneWeights
                         | aiProcess_JoinIdenticalVertices
                         | aiProcess_GenUVCoords
                         | aiProcess_SortByPType;
    const aiScene* scene = importer.ReadFile(path, flags);
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
        throw std::runtime_error("SkinnedModel: Assimp failed to load '" + path + "': " + importer.GetErrorString());

    SkinnedModel model;
    const std::string dir = DirOf(path);
    std::map<std::string, int> texCache;

    auto loadTexture = [&](const aiMaterial* mat, aiTextureType type) -> int {
        if (mat->GetTextureCount(type) == 0) return -1;
        aiString tpath; mat->GetTexture(type, 0, &tpath);
        const std::string key = tpath.C_Str();
        const auto it = texCache.find(key);
        if (it != texCache.end()) return it->second;
        int index = -1;
        try {
            if (const aiTexture* emb = scene->GetEmbeddedTexture(tpath.C_Str())) {
                image::Image im;
                if (emb->mHeight == 0) {
                    const auto* d = reinterpret_cast<const unsigned char*>(emb->pcData);
                    const std::size_t n = emb->mWidth;
                    const std::string hint = emb->achFormatHint;
                    if (hint == "png")                            im = image::DecodePNGFromMemeory(d, n);
                    else if (hint == "jpg" || hint == "jpeg")     im = image::DecodeJPEGFromMemory(d, n);
                    else if (n > 8 && d[0] == 0x89 && d[1] == 'P') im = image::DecodePNGFromMemeory(d, n);
                    else if (n > 2 && d[0] == 0xFF && d[1] == 0xD8) im = image::DecodeJPEGFromMemory(d, n);
                    else throw std::runtime_error("embedded texture format unsupported");
                } else {
                    im.width = static_cast<int>(emb->mWidth);
                    im.height = static_cast<int>(emb->mHeight);
                    im.rgba.resize(static_cast<std::size_t>(im.width) * im.height * 4);
                    for (std::size_t i = 0; i < static_cast<std::size_t>(im.width) * im.height; ++i) {
                        const aiTexel& t = emb->pcData[i];
                        im.rgba[i * 4 + 0] = t.r; im.rgba[i * 4 + 1] = t.g;
                        im.rgba[i * 4 + 2] = t.b; im.rgba[i * 4 + 3] = t.a;
                    }
                }
                FlipRows(im);
                index = static_cast<int>(model.m_textures.size());
                model.m_textures.push_back(std::make_unique<Texture>(im.rgba.data(), im.width, im.height));
            } else {
                const std::string full = dir + key;
                std::error_code ec;
                if (std::filesystem::exists(full, ec)) {
                    index = static_cast<int>(model.m_textures.size());
                    model.m_textures.push_back(std::make_unique<Texture>(full));
                }
            }
        } catch (const std::exception&) { index = -1; }
        texCache.emplace(key, index);
        return index;
    };

    // Materials (same as the static loader).
    model.m_materials.reserve(scene->mNumMaterials);
    for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
        const aiMaterial* am = scene->mMaterials[i];
        Material m; aiString nm;
        if (am->Get(AI_MATKEY_NAME, nm) == AI_SUCCESS) m.name = nm.C_Str();
        aiColor3D c;
        if (am->Get(AI_MATKEY_COLOR_DIFFUSE,  c) == AI_SUCCESS) m.diffuse  = {c.r, c.g, c.b};
        if (am->Get(AI_MATKEY_COLOR_SPECULAR, c) == AI_SUCCESS) m.specular = {c.r, c.g, c.b};
        if (am->Get(AI_MATKEY_COLOR_EMISSIVE, c) == AI_SUCCESS) m.emissive = {c.r, c.g, c.b};
        float sh = 0.0f;
        if (am->Get(AI_MATKEY_SHININESS, sh) == AI_SUCCESS && sh > 0.0f) m.shininess = sh;
        m.diffuseMap  = loadTexture(am, aiTextureType_DIFFUSE);
        m.specularMap = loadTexture(am, aiTextureType_SPECULAR);
        m.emissiveMap = loadTexture(am, aiTextureType_EMISSIVE);
        int nmap = loadTexture(am, aiTextureType_NORMALS);
        if (nmap < 0) nmap = loadTexture(am, aiTextureType_HEIGHT);
        m.normalMap = nmap;
        model.m_materials.push_back(std::move(m));
    }

    // Skeleton: every node becomes a bone, pre-order so parents precede children.
    std::map<std::string, int> boneIndex;
    std::function<void(const aiNode*, int)> buildNodes = [&](const aiNode* node, int parent) {
        const int idx = static_cast<int>(model.m_skeleton.bones.size());
        Bone b;
        b.name      = node->mName.C_Str();
        b.parent    = parent;
        b.localBind = ToGlm(node->mTransformation);
        b.offset    = glm::mat4(1.0f);
        model.m_skeleton.bones.push_back(b);
        boneIndex[b.name] = idx;
        for (unsigned c = 0; c < node->mNumChildren; ++c) buildNodes(node->mChildren[c], idx);
    };
    buildNodes(scene->mRootNode, -1);
    model.m_skeleton.globalInverse = glm::inverse(ToGlm(scene->mRootNode->mTransformation));

    // Meshes -> skinned vertices (bind space, not baked) + fill bone offsets.
    const VertexLayout layout{ {3}, {3}, {2}, {4}, {4} };
    glm::vec3 mn( std::numeric_limits<float>::max());
    glm::vec3 mx(-std::numeric_limits<float>::max());
    for (unsigned mi = 0; mi < scene->mNumMeshes; ++mi) {
        const aiMesh* mesh = scene->mMeshes[mi];
        const int numV = static_cast<int>(mesh->mNumVertices);

        std::vector<glm::ivec4> ids(static_cast<std::size_t>(numV), glm::ivec4(0));
        std::vector<glm::vec4>  wts(static_cast<std::size_t>(numV), glm::vec4(0.0f));
        std::vector<int>        slot(static_cast<std::size_t>(numV), 0);

        for (unsigned bi = 0; bi < mesh->mNumBones; ++bi) {
            const aiBone* bone = mesh->mBones[bi];
            const auto it = boneIndex.find(bone->mName.C_Str());
            if (it == boneIndex.end()) continue;
            const int bidx = it->second;
            model.m_skeleton.bones[static_cast<std::size_t>(bidx)].offset = ToGlm(bone->mOffsetMatrix);
            for (unsigned w = 0; w < bone->mNumWeights; ++w) {
                const unsigned vid = bone->mWeights[w].mVertexId;
                const float    wt  = bone->mWeights[w].mWeight;
                if (vid >= static_cast<unsigned>(numV)) continue;
                int& s = slot[vid];
                if (s < 4) { ids[vid][s] = bidx; wts[vid][s] = wt; ++s; }
            }
        }

        std::vector<float> verts;
        verts.reserve(static_cast<std::size_t>(numV) * 16);
        for (int v = 0; v < numV; ++v) {
            const aiVector3D& p = mesh->mVertices[v];
            verts.push_back(p.x); verts.push_back(p.y); verts.push_back(p.z);
            mn = glm::min(mn, glm::vec3(p.x, p.y, p.z));
            mx = glm::max(mx, glm::vec3(p.x, p.y, p.z));
            if (mesh->mNormals) { verts.push_back(mesh->mNormals[v].x); verts.push_back(mesh->mNormals[v].y); verts.push_back(mesh->mNormals[v].z); }
            else                { verts.push_back(0.0f); verts.push_back(1.0f); verts.push_back(0.0f); }
            if (mesh->mTextureCoords[0]) { verts.push_back(mesh->mTextureCoords[0][v].x); verts.push_back(mesh->mTextureCoords[0][v].y); }
            else                         { verts.push_back(0.0f); verts.push_back(0.0f); }
            for (int k = 0; k < 4; ++k) verts.push_back(static_cast<float>(ids[static_cast<std::size_t>(v)][k]));
            glm::vec4 w = wts[static_cast<std::size_t>(v)];
            const float sum = w.x + w.y + w.z + w.w;
            if (sum > 1e-5f) w /= sum; else w = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);  // unrigged vertex -> follows root
            for (int k = 0; k < 4; ++k) verts.push_back(w[k]);
        }

        std::vector<std::uint32_t> idx;
        for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            for (unsigned k = 0; k < face.mNumIndices; ++k) idx.push_back(face.mIndices[k]);
        }
        model.m_subMeshes.push_back(SubMesh{ Mesh(verts, idx, layout), static_cast<int>(mesh->mMaterialIndex) });
    }
    model.m_min = mn; model.m_max = mx;

    // Animation clips embedded in this file (if any).
    for (unsigned ai = 0; ai < scene->mNumAnimations; ++ai)
        model.m_animations.push_back(BuildClip(scene->mAnimations[ai], model.m_skeleton, boneIndex, false, ""));

    return model;
}

std::size_t SkinnedModel::AddAnimationsFromFile(const std::string& path,
                                                bool stripRootMotion,
                                                const std::string& nameOverride) {
    Assimp::Importer importer;
    // Clip-only files need no mesh post-processing; keep the node tree intact.
    const aiScene* scene = importer.ReadFile(path, aiProcess_LimitBoneWeights);
    if (!scene || !scene->mRootNode)
        throw std::runtime_error("SkinnedModel: failed to load animation '" + path + "': " + importer.GetErrorString());

    // Map this model's existing skeleton bone names -> indices.
    std::map<std::string, int> boneIndex;
    for (std::size_t i = 0; i < m_skeleton.bones.size(); ++i)
        boneIndex[m_skeleton.bones[i].name] = static_cast<int>(i);

    std::size_t added = 0;
    for (unsigned ai = 0; ai < scene->mNumAnimations; ++ai) {
        const std::string nm = (scene->mNumAnimations == 1) ? nameOverride : std::string();
        m_animations.push_back(BuildClip(scene->mAnimations[ai], m_skeleton, boneIndex, stripRootMotion, nm));
        ++added;
    }
    return added;
}

} // namespace engine
