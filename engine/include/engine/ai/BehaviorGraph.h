#pragma once

#include "engine/ai/BehaviorTree.h"
#include "engine/ai/Blackboard.h"
#include "engine/ai/Steering.h"
#include "engine/ecs/Entity.h"

#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <vector>

namespace engine {
namespace ecs { class Registry; }
namespace ai {

struct NavGrid;
class  NavMesh;

// -----------------------------------------------------------------------------
// A DATA-DRIVEN behaviour tree.
//
// The engine's BehaviorTree<Ctx> is built from C++ lambdas, which a visual editor
// can't author or serialize. This layer closes that gap: a BehaviorGraph is a plain
// list of typed nodes (a fixed vocabulary the editor exposes) that can be saved to a
// file, and BuildBehaviorTree() reconstructs a runnable BehaviorTree<AgentContext>
// by binding each data node to a built-in behaviour. AgentContext is the blackboard
// those built-ins read and write each tick.
// -----------------------------------------------------------------------------

// The blackboard every built-in node reads/writes. The host (editor play loop)
// seeds the inputs each tick, ticks the tree, then applies 'steer'/'facing'.
struct AgentContext {
    Agent     agent;                        // steering body (position/velocity/limits)
    glm::vec3 targetPos{0.0f};              // pursued point (e.g. the player)
    bool      seesTarget = false;           // perception result for this tick
    float     reachRadius = 0.6f;
    float     chargeRadius = 4.0f;          // within this + visible -> seek straight in
    float     repathInterval = 0.3f;
    float     dt = 0.0f;                     // set each tick before Tick()

    const NavGrid* grid = nullptr;          // nav source for path actions (one of...)
    const NavMesh* mesh = nullptr;
    std::vector<glm::vec3> patrol;          // patrol loop (world points)

    // ECS access for script nodes (populated by the host each tick). Lets a BtScript
    // read/write components on itself or its target beyond the steering blackboard.
    ecs::Registry* registry = nullptr;
    ecs::Entity    self = ecs::kNull;
    ecs::Entity    targetEntity = ecs::kNull;

    // Shared named key-value store. Persists across ticks for the whole play session;
    // seeded from the graph's authored initial entries when the agent is built.
    Blackboard blackboard;

    // Per-node debug status for the current tick, indexed by top-level graph node index
    // (0 = not ticked, 1 = Running, 2 = Success, 3 = Failure). Sized + cleared by the
    // host each frame when debugging; empty disables instrumentation.
    std::vector<int> nodeStatus;

    // Outputs, read by the host after Tick().
    glm::vec3 steer{0.0f};                   // desired steering acceleration this tick
    glm::vec3 facing{0.0f, 0.0f, 1.0f};
    bool      focusTarget = false;            // persistent override controlled by focus tasks

    // Runtime path scratch used by Chase/MoveTo actions.
    std::vector<glm::vec3> path;
    std::size_t pathIndex = 0;
    glm::vec3   pathGoal{0.0f};
    bool        pathGoalValid = false;
    std::size_t patrolIndex = 0;
    float       repathTimer = 0.0f;

    // Plan a path from 'from' to 'to' on whichever nav source is set (mesh preferred).
    std::vector<glm::vec3> Plan(const glm::vec3& from, const glm::vec3& to) const;
};

// The fixed node vocabulary the editor offers. Order is serialized as an int, so
// only ever APPEND new types here (never reorder) to keep old .btgraph files valid.
enum class BtNodeType {
    // composites
    Sequence = 0,   // run children in order; fail/stop on first non-Success
    Selector,       // run children in order; succeed on first non-Failure
    // decorators (one child)
    Inverter,       // flip child Success<->Failure
    Succeeder,      // force Success once the child finishes
    Repeat,         // repeat child `param` times (param < 1 => forever)
    // conditions (leaf)
    SeesTarget,     // Success if the agent currently sees its target
    TargetWithin,   // Success if the target is within `param` metres
    // actions (leaf)
    Chase,          // path toward the target, re-planning; Success when reached
    Patrol,         // follow the patrol loop forever (Running)
    MoveToTarget,   // path to the target once; Success when reached
    Wait,           // idle for `param` seconds, then Success
    Idle,           // stop moving (Running)
    // services (attached to a node; never placed standalone)
    Repath,         // every `param` seconds, re-plan the path toward the target
    // script-backed roles (bind to a BtScript by name via BtScriptRegistry)
    ScriptTask,       // leaf: runs a user BtScript's Tick() -> status
    ScriptDecorator,  // attachment: gates the node on a user BtScript's Check()
    ScriptService,    // attachment: runs a user BtScript's Tick() every `param` seconds
    // blackboard access (no-code): 'key' names the blackboard entry, 'param' the value
    BbSetBool,        // task: blackboard[key] = (param != 0); Success
    BbSetFloat,       // task: blackboard[key] = param; Success
    BbCheckBool,      // condition/decorator: Success if blackboard[key] == (param != 0)
    BbCheckFloat,     // condition/decorator: Success if blackboard[key] >= param
    // richer built-in library (reuse param/key, no new fields)
    Flee,             // task: steer directly away from the target (Running)
    Wander,           // task: steering wander (Running)
    Cooldown,         // decorator: block the node for `param` seconds after it succeeds
    TimeLimit,        // decorator: fail the node if it runs longer than `param` seconds
    RandomChance,     // decorator: enter the node with probability `param` (0..1)
    BbFloatBelow,     // condition/decorator: Success if blackboard[key] < param
    Subtree,          // leaf: runs another .btgraph (its path is stored in `script`)
    // health / combat integration (operate on the ECS via c.registry/self/targetEntity)
    HealthBelow,      // condition/decorator: Success if self's HP fraction < param
    TargetDead,       // condition/decorator: Success if the chase target's Health is dead
    Attack,           // task: deal `param` damage to the target if within reach radius
    FocusTarget,      // task: face a visible target until ClearFocus runs
    ClearFocus,       // task: release the target-facing override

    Count
};

const char* BtNodeTypeName(BtNodeType type);   // UI label
bool        BtNodeTypeIsComposite(BtNodeType type);
bool        BtNodeTypeIsDecorator(BtNodeType type);   // exactly one child
bool        BtNodeTypeIsLeaf(BtNodeType type);
bool        BtNodeTypeUsesParam(BtNodeType type);
const char* BtNodeTypeParamLabel(BtNodeType type);    // "" if unused
bool        BtNodeTypeUsesScript(BtNodeType type);    // binds to a BtScript by name
bool        BtNodeTypeUsesKey(BtNodeType type);       // references a blackboard key
bool        BtNodeTypeIsSubtree(BtNodeType type);     // runs another .btgraph (path in 'script')

// A decorator/service attached to a node (Unreal-style): compiled into the runtime
// as a wrapper around the node. Decorators gate/modify the node's result before it
// runs; services tick in the background while the node's branch is active.
struct BtAttachment {
    BtNodeType  type  = BtNodeType::SeesTarget;
    float       param = 0.0f;    // TargetWithin range / Repeat count / Repath|Service interval
    std::string script;          // BtScript class name (ScriptDecorator / ScriptService only)
    std::string key;             // blackboard key (BbCheck* decorators only)
};

// One authored node. Children index into BehaviorGraph::nodes.
struct BtGraphNode {
    BtNodeType       type = BtNodeType::Sequence;
    std::vector<int> children;          // ordered child indices
    float            param = 0.0f;      // Repeat count / TargetWithin range / Wait seconds
    std::string      script;            // BtScript class name (ScriptTask nodes only)
    std::string      key;               // blackboard key (Bb* nodes only)
    glm::vec2        canvasPos{0.0f};   // editor canvas position (persisted, ignored at runtime)
    // Attachments, evaluated top-to-bottom (top = outermost). Decorators gate entry;
    // services run each tick while this node's branch is active.
    std::vector<BtAttachment> decorators;
    std::vector<BtAttachment> services;
};

// One authored blackboard key + its initial value (only the field matching 'type'
// is used). Copied into a live Blackboard when the agent is built.
struct BlackboardEntry {
    enum class Type { Bool = 0, Int, Float, Vec3, String };
    std::string key;
    Type        type = Type::Float;
    bool        b = false;
    int         i = 0;
    float       f = 0.0f;
    glm::vec3   v{0.0f};
    std::string s;
};

const char* BlackboardTypeName(BlackboardEntry::Type type);

// The authored graph: a node list plus the root index, and the blackboard schema.
struct BehaviorGraph {
    std::vector<BtGraphNode>      nodes;
    int                          root = -1;
    std::vector<BlackboardEntry> blackboard;   // authored initial keys/values

    int  AddNode(BtNodeType type, const glm::vec2& canvasPos);
    void RemoveNode(int index);          // also unlinks it from every parent, fixes root
    bool IsValid() const;                // root in range and no obvious cycles
};

// Copy a graph's authored initial entries into a live Blackboard (call once when the
// agent is built, before the first tick).
void SeedBlackboard(const std::vector<BlackboardEntry>& entries, Blackboard& out);

// Resolves a Subtree node's asset path to a loaded BehaviorGraph (owned elsewhere,
// e.g. an editor cache). Return nullptr if it can't be loaded.
using SubtreeResolver = std::function<const BehaviorGraph*(const std::string& assetPath)>;

// Reconstruct a runnable tree from the graph. Unknown/broken nodes bind to a node
// that fails safely, so a malformed graph never crashes the agent. Subtree nodes are
// expanded through 'resolveSubtree' (recursion is depth-capped to break cycles).
BehaviorTree<AgentContext> BuildBehaviorTree(const BehaviorGraph& graph,
                                             const SubtreeResolver& resolveSubtree = {});

// Text-file persistence (the editor references graphs by path, like materials).
bool SaveBehaviorGraph(const std::string& path, const BehaviorGraph& graph, std::string* error = nullptr);
bool LoadBehaviorGraph(const std::string& path, BehaviorGraph& outGraph, std::string* error = nullptr);

} // namespace ai
} // namespace engine
