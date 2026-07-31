# Skeletal animation, graphs, actions, sockets, and characters

## Runtime data model

`Skeleton` is a topologically ordered bone list with inverse-bind offsets,
local bind transforms, and the model’s global inverse transform.

`Animation` contains a duration, tick rate, and a `BoneChannel` per skeleton
bone. Each channel can contain position, quaternion rotation, and scale keys.

`SkinnedModel` owns weighted mesh data, a skeleton, animation clips, imported
materials/textures, and bounds.

`AnimatedModel` is the per-entity ECS component. It stores:

- a shared `SkinnedModel` pointer;
- an `AnimationController`;
- the current skinning pose;
- material overrides and render-only orientation offset;
- a one-shot action layer and animation events;
- socketed static-model attachments and named gameplay sockets.

## Pose evaluation

`Animator` is stateless and provides:

- bind pose;
- single-clip pose;
- cross-faded pose;
- local-pose sampling;
- local-pose blending;
- masked override layers;
- hierarchy composition into skinning matrices;
- root-translation sampling;
- bone-mask generation from a root bone and descendants.

`UpdateAnimations(registry, dt)` advances every `AnimatedModel`, evaluates its
controller, blends the previous/current state, evaluates blend-space samples,
layers an action if active, fires events, and rebuilds the pose.

## Animation controller

`AnimationController` is a model-independent state machine. A state defines:

- name, clip, loop, and speed;
- duration;
- optional legacy activation range;
- root-motion flag;
- optional blend clip;
- 1D or 2D blend-space samples;
- blend parameters and synchronization.

Parameters are Float, Bool, or Trigger. Transitions support:

- from/to state, including any-state transitions;
- greater/equal, less, equal, and not-equal comparisons;
- multiple conditions with all/any evaluation;
- fade duration;
- normalized exit time;
- priority;
- interruption during an existing blend.

`TransitionDebug()` reports why each transition was or was not eligible.

Crossfades use smoothstep interpolation. Blend-space parameters are
exponentially smoothed independently from raw transition parameters, keeping
logic responsive while visual pose changes remain gradual.

## Blend spaces

A state may have multiple `(value, valueY, clip)` samples:

- 1D blend spaces interpolate neighboring samples on one parameter, commonly
  `Speed`.
- 2D blend spaces weight samples across two parameters, commonly forward and
  strafe speed.
- Synchronization maps samples to a common normalized phase so walk/run cycles
  do not foot-pop while blending.

Locomotion should publish raw movement state into graph parameters. The
behavior tree does not choose animation clips; it drives movement intent and
the animation graph reads locomotion flags such as speed, grounded, and
falling.

## Animation graph assets

A `.3dggraph` stores reusable clips, states, parameters, transitions, blend
spaces, action profiles, and events. Clips come from `.3dgclip` assets and use
unique graph-facing aliases.

`AnimationGraphDesc` is the engine-side canonical runtime description.
`BuildAnimationController` resolves clip names with fallback indices and maps
the description into a controller. The editor adapter only copies authoring
fields into this descriptor.

Double-clicking a `.3dggraph` opens the Graph Editor. The editor previews the
graph on its configured preview model rather than whichever scene object is
selected.

## Clip and action assets

A `.3dgclip` identifies one named animation inside a native `.3dgskmesh` or
`.3dganim` source. It stores:

- display name and source clip;
- strip-root-motion, loop, and speed;
- whether it is a standalone action;
- mask root bone;
- fade-in and fade-out;
- timestamped gameplay events.

Action clips are authored in the Clip Editor but invoked from gameplay scripts.
They are not automatically selected by the state graph.

Full-body action:

```cpp
if (!Anim().IsActionPlaying()) {
    Anim().PlayActionClip("StaffAttack");
}
```

A full-body action has an empty mask and `AnimatedModel::BlocksMovement()`
returns true until it completes. This provides montage-like behavior: normal
locomotion cannot move the character during the action.

A masked action affects a bone subtree, such as the upper body, and does not
block movement:

```cpp
Anim().PlayMaskedAction("Cast", "spine_01", 0.08f, 0.15f);
```

Check `IsActionPlaying()` or use gameplay cooldown state to prevent input spam.

## Animation events

Events are timestamped names authored on clips or profiles. As playback crosses
an event, the runtime dispatches it through `AnimatedModel::onEvent` and the
script input event list.

Scripts query:

```cpp
if (WasAnimationEvent("SpawnFireball")) {
    // Spawn from a named socket here.
}
```

Events can also drive audio actions through `RuntimeAudioSystem`.

## Root motion and model offsets

Strip root motion for in-place locomotion clips. Enable root motion only for
states whose authored translation should intentionally drive the character.

Model offsets are render-only:

```text
world Transform
  * model position offset
  * model orientation offset
  * model scale offset
  * recenter around imported bounds
```

They correct Z-up/down-facing or backwards imports without rotating the
entity’s collider, movement axes, navigation, or scene gizmo.

## Sockets and attachments

A character socket stores a name, bone, and local position/rotation/scale.
`AnimatedModel::SocketWorldTransform` resolves it every frame from the current
pose and character transform.

Attachments mount a static model and optional material on a named socket.
Gameplay can use sockets without visible attachments—for example
`StaffTip` as a projectile spawn point.

The Character Editor lets the developer select sockets/attachments and edit
their socket transform with the preview gizmo.

## Character Asset

A `.3dgcharacter` bundles:

- skeletal mesh and material;
- animation graph and standalone action clips;
- model render offsets;
- collider;
- player-controller and camera settings;
- health and ragdoll;
- AI/nav settings, team, behavior tree, and perception;
- multiple scripts;
- sockets and attachments.

The Character Editor owns the preview model, animation graph preview,
collider guide, socket gizmo, material and asset pickers, camera configuration,
AI settings, and script list.

Character-editor model transforms are asset defaults and render offsets. Once
placed in a scene, the scene’s world transform is edited independently.

## Recommended update order

1. Update player or AI movement and publish graph parameters.
2. Run gameplay scripts that may start actions or set triggers.
3. Call `UpdateAnimations`.
4. Process animation events.
5. Apply ragdoll pose override after physics if active.
6. Draw with `SkinnedRenderer`.
7. Draw socketed static attachments.

## Troubleshooting

- **Clip name error:** use the graph alias or exact imported clip name and
  refresh saved clip choices after import.
- **Missing blend space:** add at least two samples and choose the controlling
  parameter; for 2D select the Y parameter too.
- **Snappy locomotion:** increase crossfade and blend-space smoothing; ensure
  sample thresholds are ordered and synchronized.
- **Model faces down/backward:** adjust render-only orientation in Character
  Editor, not the scene entity rotation.
- **Attack does not play:** verify the action asset is listed on the Character
  Asset, the script name matches it, scripts are compiled/enabled, and spam
  prevention is not permanently latched.
- **Socket spawn direction reversed:** derive forward from the socket world
  matrix or corrected character facing, not the raw imported model axis.

