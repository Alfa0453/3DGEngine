#pragma once

#include "EditorScene.h"

// Stable, generation-safe scene target shared by asset editors. Scene selection is
// only a convenience for explicitly choosing the target; it never retargets this
// handle implicitly. Resolve() naturally invalidates deleted/recycled entities.
class SceneApplyTarget {
public:
    engine::ecs::Entity Entity() const { return m_entity; }
    bool IsSet() const { return m_entity != engine::ecs::kNull; }
    void Set(engine::ecs::Entity entity) { m_entity = entity; }
    void Clear() { m_entity = engine::ecs::kNull; }

    const EditorScene::Object* Resolve(const EditorScene& scene) const {
        return scene.FindObject(m_entity);
    }

    template <class Predicate>
    bool SetIfCompatible(const EditorScene::Object* object, Predicate compatible) {
        if (!object || !compatible(*object)) return false;
        m_entity = object->entity;
        return true;
    }

private:
    engine::ecs::Entity m_entity = engine::ecs::kNull;
};
