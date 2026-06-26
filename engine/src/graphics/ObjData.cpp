#include "engine/graphics/ObjData.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace engine::obj {
namespace {

// Directory part of a path (so we can resolve sibling .mtl / texture files).
std::string DirOf(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

// OBJ indices are 1-based and may be negative (relative to the end). Resolve to
// a 0-based index; 0 means "absent".
int Resolve(int raw, std::size_t count) {
    if (raw > 0) return raw - 1;
    if (raw < 0) return static_cast<int>(count) + raw;  // -1 => last
    return -1;                                          // absent
}

void ParseMTL(const std::string& path, std::vector<MatDef>& out) {
    std::ifstream in(path);
    if (!in) return;    // a missing .mtl is not fatal — materials just default

    const std::string dir = DirOf(path);
    MatDef* cur = nullptr;
    std::string line;
    while (std::getline(in, line)){
        std::istringstream s(line);
        std::string tag;
        s >> tag;
        if (tag == "newmtl") {
            MatDef m;
            s >> m.name;
            out.push_back(m);
            cur = &out.back();
        } else if (!cur) {
            continue;
        } else if (tag == "Kd") {
            s >> cur->diffuse.x >> cur->diffuse.y >> cur->diffuse.z;
        } else if (tag == "Ks") {
            s >> cur->specular.x >> cur->specular.y >> cur->specular.z;
        } else if (tag == "Ns") {
            s >> cur->shininess;
        } else if (tag == "map_Kd") {
            // The filename is the last whitespace-separated token (ignoring any
            // -options that may precede it).
            std::string tok, file;
            while (s >> tok) file = tok;
            if (!file.empty()) cur->diffuseMapPath = dir + file;
        }
    }
}

} // namespace

ObjData ParseOBJ(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Model: cannot open OBJ: " + path);

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;

    ObjData data;

    // Dedup unique (pos, uv, normal) corners into output vertices.
    std::map<std::tuple<int, int, int>, std::uint32_t> uniqueMap;
    std::vector<bool> hadNormal;    // per output vertex: was a normal supplied?

    // Active group (faces accumulate into the current material's group).
    auto groupFor = [&](int material) -> Group& {
        for (Group& g : data.groups)
            if (g.material == material) return g;
        data.groups.push_back(Group{material, {}});
        return data.groups.back();
    };
    int curMaterial = -1;

    auto matIndexByName = [&](const std::string& name) -> int {
        for (std::size_t i = 0; i < data.materials.size(); ++i)
            if (data.materials[i].name == name) return static_cast<int>(i);
        // Referenced before defined (or .mtl missing): create a placeholder.
        data.materials.push_back(MatDef{name, glm::vec3(0.8f), glm::vec3(0.2f), 32.0f, ""});
        return static_cast<int>(data.materials.size()) - 1;
    };

    // Turn a "v/vt/vn" corner into an output vertex index (creating it once).
    auto corner = [&](int vi, int ti, int ni) -> std::uint32_t {
        const auto key = std::make_tuple(vi, ti, ni);
        auto it = uniqueMap.find(key);
        if (it != uniqueMap.end()) return it->second;

        const std::uint32_t index = static_cast<std::uint32_t>(data.VertexCount());
        // Bounds-guard every index: a malformed OBJ must never read out of range.
        const glm::vec3 p = (vi >= 0 && vi < static_cast<int>(positions.size()))
                          ? positions[static_cast<std::size_t>(vi)] : glm::vec3(0.0f);
        const glm::vec3 n = (ni >= 0 && ni < static_cast<int>(normals.size()))
                          ? normals[static_cast<std::size_t>(ni)] : glm::vec3(0.0f);
        const glm::vec2 t = (ti >= 0 && ti < static_cast<int>(uvs.size()))
                          ? uvs[static_cast<std::size_t>(ti)] : glm::vec2(0.0f);
        // A corner with no valid normal index must generate one later.
        const bool normalValid = (ni >= 0 && ni < static_cast<int>(normals.size()));
        data.vertices.insert(data.vertices.end(),
            {p.x, p.y, p.z, n.x, n.y, n.z, t.x, t.y});
        hadNormal.push_back(normalValid);
        uniqueMap.emplace(key, index);
        return index;
    };

    std::string line;
    while (std::getline(in, line)) {
        std::istringstream s(line);
        std::string tag;
        s >> tag;
        if (tag == "v") {
            glm::vec3 p; s >> p.x >> p.y >> p.z; positions.push_back(p);
        } else if (tag == "vt") {
            glm::vec2 t; s >> t.x >> t.y; uvs.push_back(t);
        } else if (tag == "vn") {
            glm::vec3 n; s >> n.x >> n.y >> n.z; normals.push_back(n);
        } else if (tag == "mtllib") {
            std::string file; s >> file;
            ParseMTL(DirOf(path) + file, data.materials);
        } else if (tag == "usemtl") {
            std::string name; s >> name; curMaterial = matIndexByName(name);
        } else if (tag == "f") {
            // Collect the face's corners (any of v, v/vt, v//vn, v/vt/vn).
            std::vector<std::uint32_t> face;
            std::string vert;
            while (s >> vert) {
                int vi = 0, ti = 0, ni = 0;
                // Replace '/' with spaces but remember empties (v//vn).
                std::replace(vert.begin(), vert.end(), '/', ' ');
                std::istringstream vs(vert);
                vs >> vi;                   // position always present
                if (!(vs >> ti)) ti = 0;    // texcoord optional
                if (!(vs >> ni)) ni = 0;    // normal optional
                face.push_back(corner(Resolve(vi, positions.size()),
                                      Resolve(ti, uvs.size()),
                                      Resolve(ni, normals.size())));
            }
            // Fan-triangulate (works for triangles and convex n-gons).
            Group& g = groupFor(curMaterial);
            for (std::size_t k = 1; k + 1 < face.size(); ++k) {
                g.indices.push_back(face[0]);
                g.indices.push_back(face[k]);
                g.indices.push_back(face[k + 1]);
            }
        }
    }

    // Generate smooth normals for any vertex that wasn't given one.
    bool anyMissing = false;
    for (bool h : hadNormal) if (!h) { anyMissing = true; break; }
    if (anyMissing) {
        const std::size_t vn = data.VertexCount();
        std::vector<glm::vec3> accum(vn, glm::vec3(0.0f));
        auto pos = [&](std::uint32_t i) {
            const std::size_t b = i * ObjData::kFloatsPerVertex;
            return glm::vec3(data.vertices[b], data.vertices[b + 1], data.vertices[b + 2]);
        };
        for (const Group& g : data.groups)
            for (std::size_t i = 0; i + 2 < g.indices.size(); i += 3) {
                const std::uint32_t a = g.indices[i], b =  g.indices[i + 1], c = g.indices[i + 2];
                const glm::vec3 fn = glm::cross(pos(b) - pos(a), pos(c) - pos(a));
                accum[a] += fn; accum[b] += fn; accum[c] += fn;
            }
        for (std::size_t i = 0; i < vn; ++i) {
            if (hadNormal[i]) continue;
            glm::vec3 n = accum[i];
            const float len = glm::length(n);
            n = (len > 1e-8f) ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
            const std::size_t b = i * ObjData::kFloatsPerVertex;
            data.vertices[b + 3] = n.x; data.vertices[b + 4] = n.y; data.vertices[b + 5] = n.z;
        }
    }

    return data;
}

} // namespace engine::obj