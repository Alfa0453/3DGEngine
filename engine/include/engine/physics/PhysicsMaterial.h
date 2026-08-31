#pragma once

#include "engine/physics/PhysicsComponents.h"

#include <string>

namespace engine {

// A named surface preset (.3dgphysmat asset). It is authored once and referenced by many
// colliders via Collider::physicsMaterialPath; on load/apply its values are stamped onto the
// collider's inline material fields (staticFriction / dynamicFriction / restitution / density /
// combine modes). Keeping the runtime data on the Collider means the solver never has to chase an
// asset pointer per contact -- the material is a design-time convenience, resolved to plain floats.
//
// Header-only + trivially copyable so it needs no new .cpp / CMake reconfigure. The editor owns the
// file format; Stamp()/Capture() below are the single source of truth for which fields it carries,
// so the serializer and the inspector stay in lockstep with the runtime.
struct PhysicsMaterial {
    std::string     name;                                   // display name (asset stem by default)
    float           staticFriction   = 0.6f;                // resists onset of sliding
    float           dynamicFriction  = 0.4f;                // once sliding
    float           restitution      = 0.0f;                // bounciness (0 = dead)
    float           density          = 1000.0f;             // kg/m^3 (~water), for Density mass mode
    ecs::MaterialCombine frictionCombine    = ecs::MaterialCombine::GeometricMean;
    ecs::MaterialCombine restitutionCombine = ecs::MaterialCombine::GeometricMean;

    // Write this material onto a collider (design-time -> runtime). Leaves geometry/filtering alone.
    void Stamp(ecs::Collider& c) const {
        c.staticFriction     = staticFriction;
        c.dynamicFriction    = dynamicFriction;
        c.restitution        = restitution;
        c.density            = density;
        c.frictionCombine    = frictionCombine;
        c.restitutionCombine = restitutionCombine;
        // Keep the legacy single `friction` roughly consistent for any code still reading it.
        c.friction           = staticFriction;
    }

    // Read a material back out of a collider (for "save current collider as a material").
    static PhysicsMaterial Capture(const ecs::Collider& c) {
        PhysicsMaterial m;
        m.staticFriction     = c.ResolvedStaticFriction();
        m.dynamicFriction    = c.ResolvedDynamicFriction();
        m.restitution        = c.restitution;
        m.density            = c.density;
        m.frictionCombine    = c.frictionCombine;
        m.restitutionCombine = c.restitutionCombine;
        return m;
    }

    // A few sensible built-ins the editor can offer as a starting palette.
    static PhysicsMaterial Wood()   { PhysicsMaterial m; m.name="Wood";   m.staticFriction=0.5f;  m.dynamicFriction=0.4f;  m.restitution=0.1f;  m.density=700.0f;  return m; }
    static PhysicsMaterial Metal()  { PhysicsMaterial m; m.name="Metal";  m.staticFriction=0.4f;  m.dynamicFriction=0.35f; m.restitution=0.05f; m.density=7800.0f; return m; }
    static PhysicsMaterial Ice()    { PhysicsMaterial m; m.name="Ice";    m.staticFriction=0.1f;  m.dynamicFriction=0.05f; m.restitution=0.05f; m.density=917.0f;  return m; }
    static PhysicsMaterial Rubber() { PhysicsMaterial m; m.name="Rubber"; m.staticFriction=1.1f;  m.dynamicFriction=0.9f;  m.restitution=0.7f;  m.density=1100.0f; return m; }
    static PhysicsMaterial Stone()  { PhysicsMaterial m; m.name="Stone";  m.staticFriction=0.7f;  m.dynamicFriction=0.6f;  m.restitution=0.05f; m.density=2600.0f; return m; }
};

} // namespace engine
