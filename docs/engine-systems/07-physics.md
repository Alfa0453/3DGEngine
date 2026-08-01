# Physics, collision, controllers, and ragdolls

## Physics world

`PhysicsWorld::Step(registry, dt)` gathers entities with `Transform + Collider`
and optional `RigidBody`, integrates motion, detects contacts, solves
constraints, updates sleeping, and emits collision events.

Call it from the fixed-step loop. The main settings are gravity, solver
iterations, broad-phase enable/cell size, restitution threshold, and sleeping
thresholds.

## Rigid bodies

`ecs::RigidBody` contains:

- linear velocity and accumulated force;
- inverse mass (`0` means immovable);
- gravity toggle;
- kinematic mode;
- linear and angular damping;
- sleep state;
- CCD;
- angular velocity, torque, inverse inertia, and freeze rotation.

Use:

- no rigid body for a static collider;
- `RigidBody::Static()` for an explicitly immovable body;
- `RigidBody::Dynamic(mass)` for simulated props;
- kinematic for doors, lifts, and moving platforms driven by authored velocity
  or scripts.

CCD sweeps fast bodies as a sphere and should be enabled on projectiles or
other small fast objects that could tunnel through thin geometry.

## Collider shapes

Supported shapes:

- Sphere
- Plane
- Box
- Capsule
- Cylinder
- Cone
- Pyramid
- Torus
- Staircase

Each collider stores restitution, friction, trigger state, object channel, and
response mask. Shape-specific fields include radius, half extents, plane,
capsule/cylinder half height, torus radii, and staircase count.

Cylinder is a flat-ended cylinder and is distinct from capsule. Capsule total
height includes its two hemispherical ends.

## Collision channels

Named object channels are bits:

- Default
- WorldStatic
- WorldDynamic
- Player
- Enemy
- Collectible
- Projectile
- CameraBlocker
- Trigger

Two colliders interact only if both masks accept the other object’s channel:

```text
(A.mask & B.layer) != 0
and
(B.mask & A.layer) != 0
```

The built-in CharacterBlockers mask ignores Collectible and Trigger objects.
The CameraBlockers mask includes only world and camera-blocking channels.
This allows coins to overlap without stopping the player and prevents camera
spring arms from clipping against collectibles.

`isTrigger` keeps overlap detection and Enter/Stay/Exit events but disables
physical response.

## Solver

The solver uses:

- a uniform spatial-hash broad phase;
- shape-pair narrow-phase contact generation;
- contact manifolds with up to four points;
- sequential normal and two-axis friction impulses;
- warm-started cached impulses;
- positional correction;
- restitution slop;
- dynamic-body sleeping.

Contact caches improve stable stacks. Increasing solver iterations improves
constraint quality but increases fixed-step CPU cost.

## Collision events

`CollisionEvent` reports:

- entities A and B;
- Enter, Stay, or Exit;
- trigger status;
- contact point and normal;
- total normal impulse for solid contacts.

Events become available after `Step`. Gameplay uses them for triggers, audio,
particle actions, hit logic, and scripts.

Do not apply projectile damage merely because the enemy fired. Damage is
applied only after the projectile’s swept collision hits a living target.

## Queries

`PhysicsWorld` supports:

- raycast;
- sphere cast;
- sphere overlap;
- radial impulse.

Queries accept collision masks. Camera collision should use
`CollisionLayer::CameraBlockers`; perception rays should ignore the observer
and allow the intended target; projectiles should ignore their owner.

## Joints

`Joint` supports:

- Distance constraint;
- Rope behavior on distance joints;
- Spring with stiffness and damping;
- Ball joint;
- Hinge with local axes.

Joints may connect two entities or one entity to a fixed world anchor.
Authored joints are stored in the scene and rebuilt when Play begins.

## Character controller

`CharacterController` is a kinematic upright capsule. It does not participate
as a normal dynamic body. `Move`:

- applies gravity and jump velocity;
- sweeps and slides against blockers;
- depenetrates overlaps;
- walks shallow slopes;
- steps over ledges within `stepHeight`;
- maintains grounded state and ground normal.

`PlayerController` wraps it with camera-relative intent, facing, and camera
rigs. Physical stair stepping can be immediate while visual/camera position is
smoothed using `stairSmoothingSpeed`.

## AI movement and collision

Navigation supplies horizontal desired motion. `MoveAiAgent` combines it with
wall collision, gravity, floor probes, slope limits, and step-up behavior.
Movement modes are Grounded, Falling, and Flying.

`MoveAgentWithCollision` performs swept horizontal motion and wall sliding.
This prevents nav agents from passing through scene walls even when the
navigation path itself is valid.

The collision capsule and render-only model offset must remain separate. A
down-facing imported mesh should be visually corrected without rotating the
AI’s capsule or navigation frame.

## Ragdolls

`Ragdoll` stores authored activation settings and transient physics-body
mapping. The system creates a limited set of physics bodies for important
bones, links them, disables conflicting root collision response, and can apply
a death impulse.

Update order:

1. Apply damage.
2. `UpdateHealth` sets `justDied`.
3. `ActivateRagdolls` creates bodies before physics.
4. `PhysicsWorld::Step`.
5. `UpdateRagdollPoses` converts body transforms into skinning matrices.

Ragdoll runtime vectors are intentionally not serialized. They are rebuilt
each Play session.

## Editor physics tools

The Inspector edits collider shape, dimensions, material, trigger status,
collision preset, object channel, and per-channel responses. It also exposes
rigid-body, dynamic/static presets, joints, and matching collider size to the
object.

Physics Status and debug controls can show collider outlines, contact events,
joints, and solver statistics. Debug lines have an explicit Play-mode toggle
and should not be treated as game geometry.

## Troubleshooting

- **Enemy passes through walls:** confirm it has a collider, the Enemy/World
  masks block each other, and AI movement uses the collision-aware move path.
- **Character clips into ground:** check capsule center versus half-height,
  model render offset, floor channel, ground probe, and starting penetration.
- **Stair snapping:** tune step height and visual stair smoothing; ensure the
  staircase collider dimensions match visible steps.
- **Coin blocks player:** make it a trigger/Collectible and use a response mask
  that does not block Player.
- **Camera clips on pickups:** remove Collectible from camera blockers.
- **Fast projectile misses:** enable CCD or use the gameplay projectile sweep.

