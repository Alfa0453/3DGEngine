# AI, navigation, perception, and behavior trees

## Navigation sources

The engine supports two navigation representations.

`NavGrid` is a regular XZ grid of walkable cells. `AStar` finds 8-directional
paths, prevents diagonal corner cutting, and string-pulls the result to remove
unnecessary cell-by-cell turns.

`NavMesh` is a list of convex polygons with shared-edge portals. It locates or
snaps endpoints to polygons, searches the polygon graph, and applies the funnel
algorithm to produce taut corner waypoints.

`NavMeshBuilder` builds a lightweight Recast-style navmesh:

1. Rasterize configured bounds into cells.
2. Mark obstacle footprints.
3. Expand blocked cells by agent radius.
4. Merge walkable cells into rectangles.
5. Emit convex polygons and adjacency links.

## Nav Mesh Bounds Volume

The editor Nav Mesh Bounds Volume defines the world region included in the nav
build. It is editor-only and excluded from runtime rendering and collision.

The editor debug overlay shows walkable and blocked areas. Red cells/segments
represent non-walkable space or debug visualization; they are not physical
scene geometry.

The volume must cover both agent and destination and intersect the intended
walkable floor. Walls/obstacles must have colliders when the nav build derives
blockage from collision geometry.

## Steering

`Agent` stores position, velocity, speed, force, and wander state.

Available steering behaviors:

- Seek and Flee;
- Arrive;
- Pursue and Evade;
- Wander;
- obstacle avoidance;
- waypoint following.

`Integrate` clamps acceleration and velocity. For ECS agents, apply navigation
motion through the collision-aware AI movement system rather than copying the
steering position directly through walls.

## Ready-made AI agent

`AiAgent` combines steering, path planning, perception input, and a simple
Patrol/Chase/Search state machine. The host supplies target position and
whether the target is currently visible. The same brain can use a NavGrid or
NavMesh.

It remembers the last known target, investigates heard positions, repaths on a
timer, charges directly at close visible targets, and resumes patrol after its
search dwell expires.

Use this for simple agents. Use a Behavior Graph when designers need explicit
tasks, decorators, services, blackboard data, or script extensions.

## Perception

Vision uses:

- `VisionCone` range and half-angle;
- `InvisionCone` for pure range/angle testing;
- `CanSee` for cone plus physics line-of-sight.

The forward vector must come from the corrected entity/controller facing, not
the raw imported mesh’s visual forward axis. Otherwise the vision guide and
perception face backward.

Hearing uses `SoundStimulus` and `SoundField`. Noises have position, radius,
loudness, and lifetime. Agents query the loudest currently audible stimulus.
Hearing currently ignores occlusion.

## Ground movement

`AiMovementComponent` owns Grounded/Falling/Flying state, gravity, maximum
fall speed, ground probe, step height, maximum slope, vertical velocity, and
ground normal.

Navigation remains horizontal. `MoveAiAgent` adds floor contact, gravity,
small-step handling, slope limits, and collision sliding. It also supports a
terrain ground-height input.

`UpdateAiAnimationParameters` publishes engine-owned movement state to the
animation graph. Behavior trees drive intent; animation graphs choose
locomotion poses.

## Behavior Graph data

A `.btgraph` contains:

- a stable asset ID;
- an indexed node list and root;
- child ordering;
- node display names and canvas positions;
- decorators and services attached to nodes;
- a typed blackboard schema and initial values.

The runtime converts it into `BehaviorTree<AgentContext>`.
`AgentContext` contains steering state, target/perception input, nav sources,
ECS registry/self/target, blackboard, path scratch, facing/focus outputs, and
debug node statuses.

## Node library

Composites:

- Sequence
- Selector
- Parallel All
- Parallel One

Decorators:

- Inverter
- Succeeder
- Failer
- Repeat
- Retry
- Cooldown
- Time Limit
- Random Chance
- script decorator

Conditions and blackboard:

- Sees Target
- Target Within
- Heard Noise
- Health Below
- Target Dead
- set/check bool and float
- float below

Actions:

- Chase
- Patrol
- Move To Target
- Wait
- Idle
- Flee
- Wander
- Attack
- Focus Target
- Clear Focus
- Investigate
- Subtree
- script task

Services include repathing and script services.

New serialized node types must be appended to `BtNodeType`; reordering existing
values would break saved graphs.

## Blackboard

The Blackboard stores named Bool, Int, Float, Vec3, String, and Entity values.
Authored `BlackboardEntry` defaults seed the live store when an agent is built.

Use explicit keys for perception and decision state. Do not let missing health
silently mean “target dead.” `TargetDead` should succeed only when a valid
target has a Health component whose alive state is false.

## BT scripts

Subclass `ai::BtScript` to implement custom roles:

- `OnEnter`;
- `Tick` for tasks/services;
- `Check` for decorators;
- `OnExit`.

Register factories in `BtScriptRegistry`. The Behavior Graph editor lists
registered classes and integrates creation templates for tasks, decorators,
and services.

Script nodes receive `AgentContext`, including the ECS registry, self, target,
blackboard, steering, and navigation data.

## Focus and attack

`FocusTarget` enables persistent target facing while the relevant branch is
active. `ClearFocus` releases it when visibility or branch state changes.
Facing should update every tick so the enemy does not continue in an old
direction before eventually turning.

Chase should return success or stop steering when the target is within the
configured acceptance/attack radius. Attack or cast branches then run without
continually pushing the agent into the player.

Projectile attacks should spawn through a script task or animation event and
apply damage only on projectile collision.

## Editor workflow

- Double-click `.btgraph` to open Behavior Graph.
- Select saved graphs through the panel dropdown.
- **Save** overwrites the loaded graph rather than appending extensions.
- Rename composites for readable branches.
- Drag from a node pin to empty space to open the compatible-node prompt.
- Mouse wheel zooms the graph around the cursor.
- Create BT scripts from the graph’s task/decorator/service inspector.
- Assign a saved behavior tree through a dropdown on a Character Asset or AI
  Agent component.

## Diagnostic checklist

If an enemy stands still:

1. Confirm Play console says the behavior tree loaded and the agent is active.
2. Confirm target name or hostile auto-target/team configuration.
3. Confirm player and enemy teams differ and are non-neutral as intended.
4. Confirm the tree root and branches are valid.
5. Inspect live node status and blackboard.
6. Check `TargetDead`, visibility, and distance decorators.
7. Verify nav bounds cover both endpoints and a path exists.
8. Verify collision masks allow floor contact and block walls.
9. Confirm movement mode is Grounded rather than unintentionally Flying.
10. Confirm chase reach radius is not so large that it immediately succeeds.

