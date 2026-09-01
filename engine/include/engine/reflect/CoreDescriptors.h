#pragma once

// ECS Pass 3 -- descriptors for the core components (Transform, RigidBody, Collider, Light,
// CharacterController). Registering these is entirely additive: nothing here changes the component
// structs or the existing serializers/inspector. PropertyIds are hand-assigned and STABLE -- append,
// never renumber.
//
// Exposure policy (spec: "DO NOT AUTO-EXPOSE EVERYTHING"): solver-managed internals (sleep timers,
// inertia tensors, accumulated force/torque, mass-dirty flags) are simply NOT described. Runtime
// outputs that are useful to read but must not be authored (grounded, groundEntity) are described
// read-only and are NOT visual-script-visible. visualScriptVisible is opt-in per property.

#include "engine/reflect/Reflection.h"
#include "engine/ecs/Components.h"
#include "engine/physics/PhysicsComponents.h"
#include "engine/physics/CharacterController.h"

namespace engine {
namespace reflect {

// uint32 member exposed as Int (variant has no uint32 alternative). Tracked set via Patch<T>.
template <class T>
inline PropertyDescriptor MakeU32Prop(PropertyId id, const char* name, std::uint32_t T::* member,
                                      PropertyFlags flags = {}, PropertyMeta meta = {}) {
    PropertyDescriptor p;
    p.id = id; p.name = name; p.type = PropertyType::Int; p.flags = flags; p.meta = meta;
    p.get = [member](const void* c) -> PropertyValue {
        return static_cast<int>(static_cast<const T*>(c)->*member);
    };
    if (flags.writable) {
        p.set = [member](ecs::Registry& r, ecs::Entity e, const PropertyValue& v) -> bool {
            const int* val = std::get_if<int>(&v);
            if (!val) return false;
            r.Patch<T>(e, [&](T& c) { c.*member = static_cast<std::uint32_t>(*val); });
            return true;
        };
    }
    return p;
}

// Flag presets.
inline PropertyFlags PF_Full() { return PropertyFlags{}; }                             // rw+serialize+editor, vs off
inline PropertyFlags PF_VS()   { PropertyFlags f; f.visualScriptVisible = true; return f; } // + visual-script
inline PropertyFlags PF_ReadOnly() {                                                    // runtime output
    PropertyFlags f; f.writable = false; f.serializable = false; f.visualScriptVisible = false; return f;
}

inline void RegisterCoreComponents(TypeRegistry& tr = TypeRegistry::Get()) {
    using namespace engine::ecs;

    // ---- Transform (id 1) -----------------------------------------------------------------
    {
        ComponentDescriptor d;
        d.id = ComponentIds::Transform; d.name = "Transform"; d.category = "Core";
        FillComponentOps<Transform>(d);
        d.properties.push_back(MakeProp<Transform>(1, "position", &Transform::position, PropertyType::Vec3, PF_VS()));
        d.properties.push_back(MakeProp<Transform>(2, "rotation", &Transform::rotation, PropertyType::Quat, PF_VS()));
        d.properties.push_back(MakeProp<Transform>(3, "scale",    &Transform::scale,    PropertyType::Vec3, PF_VS()));
        tr.Register(std::move(d));
    }

    // ---- RigidBody (id 2) -----------------------------------------------------------------
    {
        ComponentDescriptor d;
        d.id = ComponentIds::RigidBody; d.name = "RigidBody"; d.category = "Physics";
        FillComponentOps<RigidBody>(d);
        d.properties.push_back(MakeProp<RigidBody>(1, "velocity",       &RigidBody::velocity,       PropertyType::Vec3, PF_VS()));
        d.properties.push_back(MakeProp<RigidBody>(2, "invMass",        &RigidBody::invMass,        PropertyType::Float, PF_Full(),
            PropertyMeta{true, 0.0f, 1000.0f, "Inverse mass; 0 = infinite (static)", nullptr, false}));
        d.properties.push_back(MakeProp<RigidBody>(3, "useGravity",     &RigidBody::useGravity,     PropertyType::Bool, PF_VS()));
        d.properties.push_back(MakeProp<RigidBody>(4, "kinematic",      &RigidBody::kinematic,      PropertyType::Bool, PF_Full()));
        d.properties.push_back(MakeProp<RigidBody>(5, "allowSleep",     &RigidBody::allowSleep,     PropertyType::Bool, PF_Full()));
        d.properties.push_back(MakeProp<RigidBody>(6, "linearDamping",  &RigidBody::linearDamping,  PropertyType::Float, PF_Full()));
        d.properties.push_back(MakeProp<RigidBody>(7, "angularDamping", &RigidBody::angularDamping, PropertyType::Float, PF_Full()));
        d.properties.push_back(MakeProp<RigidBody>(8, "ccd",            &RigidBody::ccd,            PropertyType::Bool, PF_Full()));
        d.properties.push_back(MakeProp<RigidBody>(9, "freezeRotation", &RigidBody::freezeRotation, PropertyType::Bool, PF_Full()));
        d.properties.push_back(MakeProp<RigidBody>(10,"angularVelocity",&RigidBody::angularVelocity,PropertyType::Vec3, PF_VS()));
        // NOT exposed: accumForce/accumTorque, sleeping, sleepTimer, invInertiaLocal,
        // massPropertiesDirty, centerOfMassLocal internals -- solver-managed / unsafe to author.
        tr.Register(std::move(d));
    }

    // ---- Collider (id 3) ------------------------------------------------------------------
    {
        ComponentDescriptor d;
        d.id = ComponentIds::Collider; d.name = "Collider"; d.category = "Physics";
        FillComponentOps<Collider>(d);
        d.properties.push_back(MakeEnumProp<Collider>(1, "shape", &Collider::shape, PF_Full()));
        d.properties.push_back(MakeProp<Collider>(2, "radius",       &Collider::radius,       PropertyType::Float, PF_Full()));
        d.properties.push_back(MakeProp<Collider>(3, "halfExtents",  &Collider::halfExtents,  PropertyType::Vec3, PF_Full()));
        d.properties.push_back(MakeProp<Collider>(4, "halfHeight",   &Collider::halfHeight,   PropertyType::Float, PF_Full()));
        d.properties.push_back(MakeProp<Collider>(5, "restitution",  &Collider::restitution,  PropertyType::Float, PF_Full(),
            PropertyMeta{true, 0.0f, 1.0f, "Bounciness", nullptr, false}));
        d.properties.push_back(MakeProp<Collider>(6, "friction",     &Collider::friction,     PropertyType::Float, PF_Full(),
            PropertyMeta{true, 0.0f, 2.0f, nullptr, nullptr, false}));
        d.properties.push_back(MakeProp<Collider>(7, "isTrigger",    &Collider::isTrigger,    PropertyType::Bool, PF_VS()));
        d.properties.push_back(MakeProp<Collider>(8, "density",      &Collider::density,      PropertyType::Float, PF_Full()));
        d.properties.push_back(MakeProp<Collider>(9, "localPosition",&Collider::localPosition,PropertyType::Vec3, PF_Full()));
        d.properties.push_back(MakeU32Prop<Collider>(10, "layer",    &Collider::layer,        PF_Full()));
        d.properties.push_back(MakeU32Prop<Collider>(11, "mask",     &Collider::mask,         PF_Full()));
        d.properties.push_back(MakeProp<Collider>(12, "physicsMaterialPath", &Collider::physicsMaterialPath, PropertyType::String, PF_Full(),
            PropertyMeta{false, 0, 0, "PhysicsMaterial asset", ".3dgphysmat", false}));
        tr.Register(std::move(d));
    }

    // ---- Light (id 4) ---------------------------------------------------------------------
    {
        ComponentDescriptor d;
        d.id = ComponentIds::Light; d.name = "Light"; d.category = "Rendering";
        FillComponentOps<Light>(d);
        d.properties.push_back(MakeEnumProp<Light>(1, "type", &Light::type, PF_Full()));
        d.properties.push_back(MakeProp<Light>(2, "color",     &Light::color,     PropertyType::Color, PF_VS()));
        d.properties.push_back(MakeProp<Light>(3, "intensity", &Light::intensity, PropertyType::Float, PF_VS(),
            PropertyMeta{true, 0.0f, 100.0f, nullptr, nullptr, false}));
        d.properties.push_back(MakeProp<Light>(4, "direction", &Light::direction, PropertyType::Vec3, PF_Full()));
        d.properties.push_back(MakeProp<Light>(5, "innerAngle",&Light::innerAngle,PropertyType::Float, PF_Full()));
        d.properties.push_back(MakeProp<Light>(6, "outerAngle",&Light::outerAngle,PropertyType::Float, PF_Full()));
        d.properties.push_back(MakeProp<Light>(7, "range",     &Light::range,     PropertyType::Float, PF_Full()));
        d.properties.push_back(MakeProp<Light>(8, "sourceRadius",&Light::sourceRadius,PropertyType::Float, PF_Full()));
        d.properties.push_back(MakeEnumProp<Light>(9, "areaShape", &Light::areaShape, PF_Full()));
        d.properties.push_back(MakeProp<Light>(10,"areaWidth", &Light::areaWidth, PropertyType::Float, PF_Full()));
        d.properties.push_back(MakeProp<Light>(11,"areaHeight",&Light::areaHeight,PropertyType::Float, PF_Full()));
        d.properties.push_back(MakeProp<Light>(12,"areaTwoSided",&Light::areaTwoSided,PropertyType::Bool, PF_Full()));
        d.properties.push_back(MakeProp<Light>(13,"affectDynamicGi",&Light::affectDynamicGi,PropertyType::Bool, PF_Full()));
        d.properties.push_back(MakeProp<Light>(14,"affectVolumetricFog",&Light::affectVolumetricFog,PropertyType::Bool, PF_Full()));
        tr.Register(std::move(d));
    }

    // ---- CharacterController (id 5) -------------------------------------------------------
    {
        ComponentDescriptor d;
        d.id = ComponentIds::CharacterController; d.name = "CharacterController"; d.category = "Physics";
        FillComponentOps<CharacterController>(d);
        d.properties.push_back(MakeProp<CharacterController>(1, "position", &CharacterController::position, PropertyType::Vec3, PF_VS()));
        d.properties.push_back(MakeProp<CharacterController>(2, "radius",   &CharacterController::radius,   PropertyType::Float, PF_Full()));
        d.properties.push_back(MakeProp<CharacterController>(3, "height",   &CharacterController::height,   PropertyType::Float, PF_Full()));
        d.properties.push_back(MakeProp<CharacterController>(4, "velocity", &CharacterController::velocity, PropertyType::Vec3, PF_VS()));
        d.properties.push_back(MakeProp<CharacterController>(5, "gravity",  &CharacterController::gravity,  PropertyType::Vec3, PF_Full()));
        d.properties.push_back(MakeProp<CharacterController>(6, "stepHeight",&CharacterController::stepHeight,PropertyType::Float, PF_Full()));
        d.properties.push_back(MakeProp<CharacterController>(7, "contactOffset",&CharacterController::contactOffset,PropertyType::Float, PF_Full()));
        d.properties.push_back(MakeProp<CharacterController>(8, "pushDynamicBodies",&CharacterController::pushDynamicBodies,PropertyType::Bool, PF_Full()));
        d.properties.push_back(MakeProp<CharacterController>(9, "pushStrength",&CharacterController::pushStrength,PropertyType::Float, PF_Full()));
        d.properties.push_back(MakeProp<CharacterController>(10,"maxPushImpulse",&CharacterController::maxPushImpulse,PropertyType::Float, PF_Full()));
        d.properties.push_back(MakeU32Prop<CharacterController>(11, "collisionMask", &CharacterController::collisionMask, PF_Full()));
        // Runtime outputs: readable, never authored, never visual-script-exposed.
        d.properties.push_back(MakeProp<CharacterController>(12, "grounded", &CharacterController::grounded, PropertyType::Bool, PF_ReadOnly()));
        d.properties.push_back(MakeEntityRefProp<CharacterController>(13, "groundEntity", &CharacterController::groundEntity, PF_ReadOnly()));
        tr.Register(std::move(d));
    }
}

} // namespace reflect
} // namespace engine
