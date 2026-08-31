#pragma once

// Save / load for the .3dgphysmat physics-material asset. Header-only (inline free functions) so
// it needs no new .cpp / CMake reconfigure -- matching the engine's other lightweight asset I/O.
//
// Format: a leading "3DG_PHYSMAT <version>" line, then one named field per line so the file stays
// human-diffable and forward-compatible (unknown trailing lines are ignored, missing lines keep
// their struct default). Follows the engine's MAGIC+version asset convention; bump kVersion when
// adding fields and read them behind a version gate.

#include "engine/physics/PhysicsMaterial.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace engine {

struct PhysicsMaterialAsset {
    static constexpr const char* kMagic   = "3DG_PHYSMAT";
    static constexpr int         kVersion = 1;

    // Serialize a material to a .3dgphysmat file. Returns false (and sets *error) on I/O failure.
    static bool Save(const std::string& path, const PhysicsMaterial& m, std::string* error = nullptr) {
        std::ofstream out(path, std::ios::binary);
        if (!out) { if (error) *error = "Could not open physics material file for writing: " + path; return false; }
        out << kMagic << ' ' << kVersion << '\n';
        out << "name "                << std::quoted(m.name.empty() ? std::string("-") : m.name) << '\n';
        out << "staticFriction "      << m.staticFriction   << '\n';
        out << "dynamicFriction "     << m.dynamicFriction  << '\n';
        out << "restitution "         << m.restitution      << '\n';
        out << "density "             << m.density          << '\n';
        out << "frictionCombine "     << static_cast<int>(m.frictionCombine)    << '\n';
        out << "restitutionCombine "  << static_cast<int>(m.restitutionCombine) << '\n';
        return static_cast<bool>(out);
    }

    // Load a .3dgphysmat file into `out`. Fields absent from the file keep their struct defaults, so
    // a newer material read by older code (or vice-versa) degrades gracefully. Returns false only on
    // a missing file or a bad magic; unknown keys are skipped.
    static bool Load(const std::string& path, PhysicsMaterial* out, std::string* error = nullptr) {
        if (!out) return false;
        std::ifstream in(path, std::ios::binary);
        if (!in) { if (error) *error = "Could not open physics material file: " + path; return false; }

        std::string magic; int version = 0;
        in >> magic >> version;
        if (magic != kMagic || version < 1 || version > kVersion) {
            if (error) *error = "Unrecognized physics material format in " + path;
            return false;
        }

        PhysicsMaterial m;                       // start from defaults
        std::string line;
        std::getline(in, line);                  // consume the rest of the magic line
        while (std::getline(in, line)) {
            std::istringstream ls(line);
            std::string key;
            if (!(ls >> key) || key.empty()) continue;
            int i = 0;
            if      (key == "name")               { std::string v; ls >> std::quoted(v); m.name = (v == "-") ? std::string() : v; }
            else if (key == "staticFriction")     ls >> m.staticFriction;
            else if (key == "dynamicFriction")    ls >> m.dynamicFriction;
            else if (key == "restitution")        ls >> m.restitution;
            else if (key == "density")            ls >> m.density;
            else if (key == "frictionCombine")    { ls >> i; m.frictionCombine    = static_cast<ecs::MaterialCombine>(i); }
            else if (key == "restitutionCombine") { ls >> i; m.restitutionCombine = static_cast<ecs::MaterialCombine>(i); }
            // unknown key: ignore (forward compatibility)
        }
        *out = m;
        return true;
    }

    // Convenience: load a .3dgphysmat and stamp it straight onto a collider. Returns false if the
    // path is empty (no material assigned -> leave the collider's inline values) or on load failure.
    static bool Apply(const std::string& path, ecs::Collider& c, std::string* error = nullptr) {
        if (path.empty()) return false;
        PhysicsMaterial m;
        if (!Load(path, &m, error)) return false;
        m.Stamp(c);
        return true;
    }
};

} // namespace engine
