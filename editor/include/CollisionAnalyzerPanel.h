#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

class CollisionAnalyzerPanel {
public:
    struct ColliderRow {
        std::uint32_t entity = 0;
        std::string name;
        std::string shape;
        std::uint32_t layer = 0;
        std::uint32_t mask = 0;
        bool trigger = false;
        bool rigidBody = false;
        bool kinematic = false;
        bool sleeping = false;
        int island = -1;
        int activeContacts = 0;
    };

    struct ContactEvent {
        std::uint64_t sequence = 0;
        std::uint32_t entityA = 0;
        std::uint32_t entityB = 0;
        std::string objectA;
        std::string objectB;
        int phase = 0;
        bool trigger = false;
        glm::vec3 point{0.0f};
        glm::vec3 normal{0.0f};
        float penetration = 0.0f;
        float impulse = 0.0f;
    };

    struct Snapshot {
        bool playMode = false;
        bool layerMatrixActive = false;
        bool broadphaseValid = false;
        int candidatePairs = 0;
        int manifolds = 0;
        int occupiedGridCells = 0;
        float deepestPenetration = 0.0f;
        std::vector<ColliderRow> colliders;
        bool layerMatrix[9][9]{};
    };

    struct Result {
        bool showSceneGuides = false;
        bool selectedOnly = false;
        bool triggersOnly = false;
        bool enterExitOnly = false;
        bool clearGuides = false;
    };

    void Record(ContactEvent event);
    void ClearEvents();
    Result Draw(const Snapshot& snapshot, bool* open);

private:
    static bool PairAllowed(const ColliderRow& a, const ColliderRow& b,
                            bool matrixEnabled, bool matrixAllows);

    std::vector<ContactEvent> m_events;
    std::uint64_t m_nextSequence = 1;
    char m_filter[128]{};
    int m_phaseFilter = 0;
    int m_kindFilter = 0;
    int m_pairA = -1;
    int m_pairB = -1;
    bool m_follow = true;
    bool m_showSceneGuides = true;
    bool m_selectedOnly = false;
    bool m_triggersOnly = false;
    bool m_enterExitOnly = false;
};
