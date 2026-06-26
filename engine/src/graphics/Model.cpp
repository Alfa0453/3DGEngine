#include "engine/graphics/Model.h"

#include "engine/graphics/ImageDecode.h"
#include "engine/graphics/VertexLayout.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glm/glm.hpp>

#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <stdexcept>

namespace engine {
namespace {

glm::mat4 ToGlm(const aiMatrix4x4& a) {
    // aiMatrix4x4 is row-major (a1..a4 = first row); glm is column-major.
    return glm::mat4(a.a1, a.b1, a.c1, a.d1,
                     a.a2, a.b2, a.c2, a.d2,
                     a.a3, a.b3, a.c3, a.d3,
                     a.a4, a.b4, a.c4, a.d4);
}

std::string DirOf(const std::string& p) {
    const std::size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? std::string() : p.substr(0, s + 1);
}

// Decoders return the top row first; GL wants bottom-first.
void FlipRows(image::Image& im) {
    const std::size_t row = static_cast<std::size_t>(im.width) * 4;
    for (int y = 0; y < im.height / 2; ++y)
        std::swap_ranges(im.rgba.begin() + static_cast<std::ptrdiff_t>(y * row),
                        im.rgba.begin() + static_cast<std::ptrdiff_t>((y + 1) * row),
                        im.rgba.begin() + static_cast<std::ptrdiff_t>((im.height - 1 - y) * row));
}

}// namespace

Model Model::FromFile(const std::string& path) {
    Assimp::Importer importer;
    const unsigned flags = aiProcess_Triangulate
                         | aiProcess_GenSmoothNormals
                         | aiProcess_CalcTangentSpace
                         | aiProcess_JoinIdenticalVertices
                         | aiProcess_GenUVCoords
                         | aiProcess_SortByPType;
    const aiScene* scene = importer.ReadFile(path, flags);
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
        throw std::runtime_error("Model: Assimp failed to load '" + path + "': " + importer.GetErrorString());

    Model model;
    const std::string dir = DirOf(path);
    std::map<std::string, int> texCache;

    // Load a material's first texture of `type` -> index into m_textures (or -1).
    auto loadTexture = [&](const aiMaterial* mat, aiTextureType type) -> int {
        if (mat->GetTextureCount(type) == 0) return -1;
        aiString tpath;
        mat->GetTexture(type, 0, &tpath);
        const std::string key = tpath.C_Str();
        const auto it = texCache.find(key);
        if (it != texCache.end()) return it->second;

        int index = -1;
        try {
            if (const aiTexture* emb = scene->GetEmbeddedTexture(tpath.C_Str())) {
                image::Image im;
                if (emb->mHeight == 0) {                         // compressed bytes
                    const auto* d = reinterpret_cast<const unsigned char*>(emb->pcData);
                    const std::size_t n = emb->mWidth;
                    const std::string hint = emb->achFormatHint;
                    if (hint == "png")                          im = image::DecodePNGFromMemeory(d, n);
                    else if (hint == "jpg" || hint == "jpeg")   im = image::DecodeJPEGFromMemory(d, n);
                    else if (n > 8 && d[0] == 0x89 && d[1] == 'p') im = image::DecodePNGFromMemeory(d, n);
                    else if (n > 2 && d[0] == 0xFF && d[1] == 0xD8) im = image::DecodeJPEGFromMemory(d, n);
                    else throw std::runtime_error("embeded texture format '" + hint + "' unsupported");
                } else {                                        // raw BGRA texels
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
            } else {                                            // external file
                const std::string full = dir + key;
                std::error_code ec;
                if (std::filesystem::exists(full, ec)) {
                    index = static_cast<int>(model.m_textures.size());
                    model.m_textures.push_back(std::make_unique<Texture>(full));
                }
            }
        } catch (const std::exception&) {
            index = -1;                                         // skip a bad texture, keep the model
        }
        texCache.emplace(key, index);
        return index;
    };

    // Materials.
    model.m_materials.reserve(scene->mNumMaterials);
    for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
        const aiMaterial* am = scene->mMaterials[i];
        Material m;
        aiString nm;
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
        if (nmap < 0) nmap = loadTexture(am, aiTextureType_HEIGHT);  // OBJ keeps normals in bump/height
        m.normalMap = nmap;
        
        model.m_materials.push_back(std::move(m));
    }

    // Meshes: walk the node tree, baking each node's transform into the vertices.
    const VertexLayout layout{ {3}, {3}, {2}, {3} };
    glm::vec3 lo(1e30f), hi(-1e30f);
    std::function<void(const aiNode*, const glm::mat4&)> visit =
        [&](const aiNode* node, const glm::mat4& parent) {
            const glm::mat4 xform = parent * ToGlm(node->mTransformation);
            const glm::mat3 nmat  = glm::mat3(glm::transpose(glm::inverse(xform)));
            for (unsigned i = 0; i < node->mNumMeshes; ++i) {
                const aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
                std::vector<float> verts;
                verts.reserve(static_cast<std::size_t>(mesh->mNumVertices) * 11);
                for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
                    const glm::vec4 p = xform * glm::vec4(mesh->mVertices[v].x,
                                                          mesh->mVertices[v].y,
                                                          mesh->mVertices[v].z, 1.0f);
                    glm::vec3 n(0, 1, 0), t(1, 0, 0);
                    glm::vec2 uv(0.0f);
                    if (mesh->HasNormals())
                        n = glm::normalize(nmat * glm::vec3(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z));
                    if (mesh->HasTangentsAndBitangents())
                        t = glm::normalize(nmat * glm::vec3(mesh->mTangents[v].x, mesh->mTangents[v].y, mesh->mTangents[v].z));
                    if (mesh->HasTextureCoords(0))
                        uv = {mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y};
                    verts.insert(verts.end(), {p.x, p.y, p.z, n.x, n.y, n.z, uv.x, uv.y, t.x, t.y, t.z});
                    lo = glm::min(lo, glm::vec3(p));
                    hi = glm::max(hi, glm::vec3(p));
                }
                std::vector<std::uint32_t> idx;
                idx.reserve(static_cast<std::size_t>(mesh->mNumFaces) * 3);
                for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
                    const aiFace& face = mesh->mFaces[f];
                    for (unsigned k = 0; k < face.mNumIndices; ++k) idx.push_back(face.mIndices[k]);
                }
                if (!idx.empty())
                    model.m_subMeshes.push_back(SubMesh{ Mesh(verts, idx, layout),
                                                         static_cast<int>(mesh->mMaterialIndex) });
            }
            for (unsigned c = 0; c < node->mNumChildren; ++c) visit(node->mChildren[c], xform);
        };
    visit(scene->mRootNode, glm::mat4(1.0f));

    if (!model.m_subMeshes.empty()) { model.m_min = lo; model.m_max = hi; }
    return model;
}

} // namespace engine