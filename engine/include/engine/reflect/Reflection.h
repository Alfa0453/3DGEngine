#pragma once

// ECS Pass 3 -- Reflection, stable IDs & visual-scripting foundation.
//
// This is an ADDITIONAL metadata layer. It does NOT replace the Registry template API, the plain
// C++ component structs, the existing serializers, or the Inspector. Components stay plain data and
// inherit from nothing. Descriptors are registered once (RegisterCoreComponents) and let editor /
// serializer / (future) visual-script code walk components generically WITHOUT hard-coding each
// field -- while any existing custom path keeps working untouched.
//
// Guarantees demanded by the pass spec:
//   * Stable IDs: ComponentTypeId and PropertyId are explicit small integers assigned by hand here.
//     We NEVER persist typeid().hash_code(), std::type_index, addresses, or offsetof. The integers
//     are the on-disk identity and stay fixed across rebuilds / compilers / launches.
//   * Safe writes: reflected SetProperty routes through Registry::Patch<T>, so a reflected edit
//     participates in Pass-1 change tracking exactly like a hand-written Patch (revision bump +
//     change record). It can NOT silently bypass tracking.
//   * Opt-in exposure: visualScriptVisible defaults to false on any property that isn't explicitly
//     opted in. Raw pointers, proxy IDs, solver internals, and editor-only internals are never
//     exposed as visual-script properties.
//   * Entity references: an entity-valued property is flagged entityRef; ResolveEntityRef validates
//     generation via Registry::Valid before anyone dereferences it. Dead handles resolve to kNull.
//
// GL-free and header-only so it links in tools/tests without the renderer.

#include "engine/ecs/Registry.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <variant>
#include <vector>
#include <sstream>
#include <iomanip>

namespace engine {
namespace reflect {

// ---- stable identity -------------------------------------------------------------------------
using ComponentTypeId = std::uint32_t;   // persisted; assigned by hand, never derived from the type
using PropertyId      = std::uint16_t;   // persisted; unique WITHIN one component descriptor

// Explicit, hand-assigned component IDs. Append new ids; never renumber an existing one.
namespace ComponentIds {
    constexpr ComponentTypeId Transform           = 1;
    constexpr ComponentTypeId RigidBody           = 2;
    constexpr ComponentTypeId Collider            = 3;
    constexpr ComponentTypeId Light               = 4;
    constexpr ComponentTypeId CharacterController = 5;
    // 6..99 reserved for further core components
}

// ---- value model -----------------------------------------------------------------------------
enum class PropertyType : std::uint8_t {
    Bool, Int, Float, Vec2, Vec3, Vec4, Quat, Color, String, EntityRef, Enum
};

// A dynamically-typed property value. Enum values ride inside Int; Color inside Vec3.
using PropertyValue = std::variant<
    std::monostate, bool, int, float,
    glm::vec2, glm::vec3, glm::vec4, glm::quat,
    std::string, ecs::Entity>;

// Validate an entity-valued property's generation before anyone uses it. A stale handle -> kNull.
inline ecs::Entity ResolveEntityRef(const ecs::Registry& reg, const PropertyValue& v) {
    if (auto p = std::get_if<ecs::Entity>(&v))
        return reg.Valid(*p) ? *p : ecs::kNull;
    return ecs::kNull;
}

// ---- property descriptor ---------------------------------------------------------------------
struct PropertyFlags {
    bool readable            = true;
    bool writable            = true;
    bool serializable        = true;
    bool editorVisible       = true;
    bool visualScriptVisible = false;   // OPT-IN: default hidden from visual scripting
};

struct PropertyMeta {
    bool        hasRange = false;
    float       minValue = 0.0f;
    float       maxValue = 0.0f;
    const char* tooltip  = nullptr;
    const char* assetType = nullptr;    // e.g. ".3dgphysmat" for an asset-path string
    bool        entityRef = false;      // this property is an Entity handle (validate generation)
};

struct PropertyDescriptor {
    PropertyId    id = 0;
    const char*   name = "";
    PropertyType  type = PropertyType::Float;
    PropertyFlags flags;
    PropertyMeta  meta;

    // get reads from a component instance (raw const pointer from ComponentDescriptor::getRaw).
    std::function<PropertyValue(const void*)> get;
    // set routes a new value onto entity `e` via Registry::Patch<T> (tracked). Returns false when
    // the property is not writable or the entity is dead.
    std::function<bool(ecs::Registry&, ecs::Entity, const PropertyValue&)> set;
};

// ---- component descriptor --------------------------------------------------------------------
struct ComponentDescriptor {
    ComponentTypeId id = 0;
    const char*     name = "";
    const char*     category = "";
    bool            serializable = true;   // whole-component opt-out (per-property flags still apply)

    std::function<void(ecs::Registry&, ecs::Entity)>              construct;  // add default-constructed
    std::function<void(ecs::Registry&, ecs::Entity)>              remove;     // remove if present
    std::function<void(ecs::Registry&, ecs::Entity /*src*/, ecs::Entity /*dst*/)> clone; // copy src->dst
    std::function<bool(const ecs::Registry&, ecs::Entity)>        has;
    std::function<const void*(ecs::Registry&, ecs::Entity)>       getRaw;     // component ptr or nullptr

    std::vector<PropertyDescriptor> properties;

    const PropertyDescriptor* FindProperty(PropertyId pid) const {
        for (const auto& p : properties) if (p.id == pid) return &p;
        return nullptr;
    }
};

// ---- type registry (singleton) ---------------------------------------------------------------
class TypeRegistry {
public:
    static TypeRegistry& Get() { static TypeRegistry r; return r; }

    void Register(ComponentDescriptor d) {
        for (auto& e : m_descs) if (e.id == d.id) { e = std::move(d); return; }  // idempotent
        m_descs.push_back(std::move(d));
    }
    const ComponentDescriptor* Find(ComponentTypeId id) const {
        for (const auto& d : m_descs) if (d.id == id) return &d;
        return nullptr;
    }
    const ComponentDescriptor* FindByName(const std::string& n) const {
        for (const auto& d : m_descs) if (n == d.name) return &d;
        return nullptr;
    }
    const std::vector<ComponentDescriptor>& All() const { return m_descs; }
    void Clear() { m_descs.clear(); }

private:
    std::vector<ComponentDescriptor> m_descs;
};

// ---- reflected access (top-level, entity-safe) -----------------------------------------------
// Returns monostate if the entity is dead, the component is absent, or the property isn't readable.
inline PropertyValue GetProperty(ecs::Registry& reg, ecs::Entity e,
                                 ComponentTypeId cid, PropertyId pid) {
    if (!reg.Valid(e)) return {};
    const ComponentDescriptor* cd = TypeRegistry::Get().Find(cid);
    if (!cd) return {};
    const PropertyDescriptor* pd = cd->FindProperty(pid);
    if (!pd || !pd->flags.readable || !pd->get) return {};
    const void* raw = cd->getRaw(reg, e);
    if (!raw) return {};
    return pd->get(raw);
}

// Writes via Patch<T> (tracked). Returns false on dead entity, absent component, non-writable
// property, or type mismatch.
inline bool SetProperty(ecs::Registry& reg, ecs::Entity e,
                        ComponentTypeId cid, PropertyId pid, const PropertyValue& value) {
    if (!reg.Valid(e)) return false;
    const ComponentDescriptor* cd = TypeRegistry::Get().Find(cid);
    if (!cd) return false;
    const PropertyDescriptor* pd = cd->FindProperty(pid);
    if (!pd || !pd->flags.writable || !pd->set) return false;
    if (!cd->has(reg, e)) return false;
    return pd->set(reg, e, value);
}

// ---- builder helpers -------------------------------------------------------------------------
// Scalar/vector member: get reads member; set assigns member inside a tracked Patch<T>.
template <class T, class M>
PropertyDescriptor MakeProp(PropertyId id, const char* name, M T::* member,
                            PropertyType type, PropertyFlags flags = {}, PropertyMeta meta = {}) {
    PropertyDescriptor p;
    p.id = id; p.name = name; p.type = type; p.flags = flags; p.meta = meta;
    p.get = [member](const void* c) -> PropertyValue {
        return static_cast<const T*>(c)->*member;
    };
    if (flags.writable) {
        p.set = [member](ecs::Registry& r, ecs::Entity e, const PropertyValue& v) -> bool {
            const M* val = std::get_if<M>(&v);
            if (!val) return false;                      // type mismatch -> reject, don't corrupt
            r.Patch<T>(e, [&](T& c) { c.*member = *val; });   // TRACKED write
            return true;
        };
    }
    return p;
}

// Enum member exposed as Int (stable numeric value, editor renders a combo from it).
template <class T, class E>
PropertyDescriptor MakeEnumProp(PropertyId id, const char* name, E T::* member,
                                PropertyFlags flags = {}, PropertyMeta meta = {}) {
    PropertyDescriptor p;
    p.id = id; p.name = name; p.type = PropertyType::Enum; p.flags = flags; p.meta = meta;
    p.get = [member](const void* c) -> PropertyValue {
        return static_cast<int>(static_cast<const T*>(c)->*member);
    };
    if (flags.writable) {
        p.set = [member](ecs::Registry& r, ecs::Entity e, const PropertyValue& v) -> bool {
            const int* val = std::get_if<int>(&v);
            if (!val) return false;
            r.Patch<T>(e, [&](T& c) { c.*member = static_cast<E>(*val); });
            return true;
        };
    }
    return p;
}

// Entity-handle member: flagged entityRef; consumers must ResolveEntityRef before dereferencing.
template <class T>
PropertyDescriptor MakeEntityRefProp(PropertyId id, const char* name, ecs::Entity T::* member,
                                     PropertyFlags flags = {}, PropertyMeta meta = {}) {
    meta.entityRef = true;
    PropertyDescriptor p;
    p.id = id; p.name = name; p.type = PropertyType::EntityRef; p.flags = flags; p.meta = meta;
    p.get = [member](const void* c) -> PropertyValue { return static_cast<const T*>(c)->*member; };
    if (flags.writable) {
        p.set = [member](ecs::Registry& r, ecs::Entity e, const PropertyValue& v) -> bool {
            const ecs::Entity* val = std::get_if<ecs::Entity>(&v);
            if (!val) return false;
            // Accept kNull or a live handle only; reject stale handles rather than storing them.
            if (*val != ecs::kNull && !r.Valid(*val)) return false;
            r.Patch<T>(e, [&](T& c) { c.*member = *val; });
            return true;
        };
    }
    return p;
}

// Fill the shared construct/remove/clone/has/getRaw lambdas for a plain-data component T.
template <class T>
void FillComponentOps(ComponentDescriptor& d) {
    d.construct = [](ecs::Registry& r, ecs::Entity e) { if (!r.Has<T>(e)) r.Add(e, T{}); };
    d.remove    = [](ecs::Registry& r, ecs::Entity e) { if (r.Has<T>(e)) r.Remove<T>(e); };
    d.clone     = [](ecs::Registry& r, ecs::Entity src, ecs::Entity dst) {
        if (const T* c = r.TryGet<T>(src)) { T copy = *c; if (r.Has<T>(dst)) r.Get<T>(dst) = copy; else r.Add(dst, copy); }
    };
    d.has       = [](const ecs::Registry& r, ecs::Entity e) { return r.Has<T>(e); };
    d.getRaw    = [](ecs::Registry& r, ecs::Entity e) -> const void* { return r.TryGet<T>(e); };
}

// ---- generic reflected text serialization (opt-in, additive) ---------------------------------
// Walks a component's serializable properties and writes `pid <value>` lines. This is offered as a
// duplication-reducing helper for SIMPLE components; complex components/assets keep their bespoke
// serializers. Round-trips through SerializeComponent / DeserializeComponent.
namespace detail {
    inline void WriteValue(std::ostream& os, const PropertyValue& v) {
        std::visit([&](auto&& x) {
            using X = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<X, std::monostate>) { os << "_"; }
            else if constexpr (std::is_same_v<X, bool>)   { os << (x ? 1 : 0); }
            else if constexpr (std::is_same_v<X, int>)    { os << x; }
            else if constexpr (std::is_same_v<X, float>)  { os << std::setprecision(9) << x; }
            else if constexpr (std::is_same_v<X, glm::vec2>) { os << x.x << ' ' << x.y; }
            else if constexpr (std::is_same_v<X, glm::vec3>) { os << x.x << ' ' << x.y << ' ' << x.z; }
            else if constexpr (std::is_same_v<X, glm::vec4>) { os << x.x << ' ' << x.y << ' ' << x.z << ' ' << x.w; }
            else if constexpr (std::is_same_v<X, glm::quat>) { os << x.w << ' ' << x.x << ' ' << x.y << ' ' << x.z; }
            else if constexpr (std::is_same_v<X, std::string>) { os << std::quoted(x.empty() ? std::string("-") : x); }
            else if constexpr (std::is_same_v<X, ecs::Entity>) { os << static_cast<std::uint32_t>(x); }
        }, v);
    }

    // Read a value of the same alternative currently held by `proto` (the property's declared type).
    inline PropertyValue ReadValue(std::istream& is, const PropertyValue& proto) {
        PropertyValue out;
        std::visit([&](auto&& x) {
            using X = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<X, std::monostate>) { std::string s; is >> s; out = std::monostate{}; }
            else if constexpr (std::is_same_v<X, bool>)   { int t; is >> t; out = (t != 0); }
            else if constexpr (std::is_same_v<X, int>)    { int t; is >> t; out = t; }
            else if constexpr (std::is_same_v<X, float>)  { float t; is >> t; out = t; }
            else if constexpr (std::is_same_v<X, glm::vec2>) { glm::vec2 t; is >> t.x >> t.y; out = t; }
            else if constexpr (std::is_same_v<X, glm::vec3>) { glm::vec3 t; is >> t.x >> t.y >> t.z; out = t; }
            else if constexpr (std::is_same_v<X, glm::vec4>) { glm::vec4 t; is >> t.x >> t.y >> t.z >> t.w; out = t; }
            else if constexpr (std::is_same_v<X, glm::quat>) { glm::quat t; is >> t.w >> t.x >> t.y >> t.z; out = t; }
            else if constexpr (std::is_same_v<X, std::string>) { std::string s; is >> std::quoted(s); out = (s == "-") ? std::string() : s; }
            else if constexpr (std::is_same_v<X, ecs::Entity>) { std::uint32_t t; is >> t; out = static_cast<ecs::Entity>(t); }
        }, proto);
        return out;
    }
}

// Serialize one component instance's serializable properties to a single text line:
//   <componentId> <count> <pid> <value> <pid> <value> ...
inline std::string SerializeComponent(ecs::Registry& reg, ecs::Entity e, const ComponentDescriptor& cd) {
    std::ostringstream os;
    const void* raw = cd.getRaw(reg, e);
    os << cd.id << ' ';
    if (!raw || !cd.serializable) { os << 0; return os.str(); }
    std::ostringstream body; int n = 0;
    for (const auto& p : cd.properties) {
        if (!p.flags.serializable || !p.get) continue;
        body << ' ' << p.id << ' ';
        detail::WriteValue(body, p.get(raw));
        ++n;
    }
    os << n << body.str();
    return os.str();
}

// Inverse: reads the line, applies each property via the tracked SetProperty path. Unknown pids are
// skipped (forward/backward compatible). Component must already exist on the entity.
inline bool DeserializeComponent(ecs::Registry& reg, ecs::Entity e, std::istream& is) {
    ComponentTypeId cid = 0; int n = 0;
    if (!(is >> cid >> n)) return false;
    const ComponentDescriptor* cd = TypeRegistry::Get().Find(cid);
    for (int i = 0; i < n; ++i) {
        PropertyId pid = 0; is >> pid;
        const PropertyDescriptor* pd = cd ? cd->FindProperty(pid) : nullptr;
        if (!pd || !pd->get) {                 // unknown property: consume a token and skip
            std::string skip; is >> skip; continue;
        }
        PropertyValue proto = pd->get(cd->getRaw(reg, e) ? cd->getRaw(reg, e) : nullptr);
        PropertyValue v = detail::ReadValue(is, proto);
        SetProperty(reg, e, cid, pid, v);      // tracked write
    }
    return true;
}

} // namespace reflect
} // namespace engine
