# 3DGEngine Visual Scripting — Remaining Milestones

Last reviewed: 2026-09-08

This roadmap covers the work remaining after the visual-scripting runtime, graph editor,
reflection bridge, gameplay nodes, cooking validation, debugger/profiler, and per-object exposed
variable overrides were implemented. Milestones are ordered so each one leaves the system usable
and testable before the next begins.

## Completed baseline

The following capabilities already exist and should be preserved while completing this roadmap:

- Versioned `.3dgvs` graph assets with stable graph, node, pin, link, and variable IDs.
- Typed execution and data pins, explicit conversions, variables, comments, copy/paste, undo/redo,
  searchable node creation, connection validation, and save-time validation.
- Begin Play, Update, Fixed Update, script events, collision/trigger events, latent execution,
  engine reflection nodes, gameplay nodes, physics queries, input, structural changes, and hot reload.
- Runtime instruction/depth safeguards, recoverable errors, cook validation, dependency discovery,
  content hashing, and generated node documentation.
- Runtime profiling, execution highlighting, breakpoints, pause/continue, node stepping, call stacks,
  live values, and visual-script error reporting.
- Graph variables marked **Exposed on Component**, with typed per-object Inspector overrides that are
  saved in editor scenes and packaged runtime scenes.

## Remaining milestone summary

| Order | Milestone | Primary result | Status |
|---:|---|---|---|
| 1 | Complete typed value authoring | Friendly editors and project pickers for every supported type | Implemented (pending build verify) |
| 2 | Graph navigation and layout tools | Large graphs become quick to organize and navigate | Implemented (pending build verify) |
| 3 | Functions and reusable subgraphs | Logic can be reused without copy/paste | Implemented (pending build verify) |
| 4 | Collections and structured data | Arrays, maps, structs, and typed enums | Implemented (pending build verify) |
| 5 | Advanced flow and time nodes | Timelines, gates, loops, timers, and controlled parallel flow | Implemented (pending build verify) |
| 6 | Event dispatchers and graph interfaces | Decoupled, typed communication between objects and graphs | Implemented (pending build verify) |
| 7 | Visual state-machine graphs | Author high-level gameplay states visually | Implemented (pending build verify) |
| 8 | Async task model and cancellation | Safe long-running navigation, streaming, and gameplay operations | Implemented (pending build verify) |
| 9 | Compiled graph execution | Faster runtime execution with a derived bytecode/IR cache | Implemented (pending build verify) |
| 10 | Automated graph testing | Repeatable tests and simulation without entering the game manually | Implemented (pending build verify) |
| 11 | Asset lifecycle and migration hardening | Safe rename, delete, duplication, upgrade, and reference repair | Implemented (pending build verify) |
| 12 | Templates, documentation, and release hardening | Production-ready onboarding and packaging | Implemented (pending build verify) |

## Milestone 1 — Complete typed value authoring

Replace raw IDs and incomplete value controls with type-aware editors everywhere a pin default,
variable default, node property, or component override is shown.

### Deliverables

- Add editors for every current `ValueType`, including Entity, Asset, Quaternion, Color, and the
  reserved Script Handle type when its final representation is available.
- Use searchable Content Browser pickers for Asset values instead of manually entering handles.
- Use a scene-object picker for Entity values, with **Self**, **None**, and eyedropper choices.
- Show enum names through reflection rather than exposing only integer values.
- Add numeric limits, units, tooltips, reset-to-default, and mixed-value handling where applicable.
- Reuse one shared value-widget implementation in the graph panel and Inspector.
- Keep every ImGui control ID unique when the same value type appears more than once.

### Completion gate

A user can author every supported value without copying an asset handle or entity number, values
round-trip through `.3dgvs`, editor scenes, and runtime scenes, and the editor produces no ImGui ID
warnings.

## Milestone 2 — Graph navigation and layout tools

Improve daily graph editing without changing runtime behavior.

### Deliverables

- Reroute/knot nodes for organizing wires.
- Box selection, duplicate-by-drag, frame selection, frame all, and focus-node navigation.
- Align, distribute, straighten links, and automatic layout commands.
- Minimap, bookmarks, breadcrumb/path display, and searchable jump-to-node results.
- Collapsible comments with color, title, and optional movement of enclosed nodes.
- Configurable grid snapping and zoom limits.
- Clear wire direction, compatible-pin highlighting, and data-type colors.

### Completion gate

A large sample graph can be organized and navigated without manually positioning every node or
searching the canvas by panning.

## Milestone 3 — Functions and reusable subgraphs

Allow repeated logic to live in one authored definition.

### Deliverables

- Function entry/return nodes with typed input and output parameters.
- Function-local variables separated from component instance variables.
- Callable functions within the same graph.
- Reusable subgraph/function-library assets callable from other graphs.
- Dependency tracking and cycle/recursion validation for cross-graph calls.
- Double-click navigation into a called function and breadcrumb navigation back to the caller.
- Debugger call-stack frames that include graph and function names.

### Completion gate

Two different graphs can call one shared function asset, changes propagate through hot reload, and
recursive or missing calls fail validation rather than hanging or crashing.

## Milestone 4 — Collections and structured data

Expand the value system beyond scalar values while keeping types explicit and serializable.

### Deliverables

- Typed arrays with get, set, add, insert, remove, contains, find, clear, length, and iteration nodes.
- Typed maps with find, add/update, remove, contains, keys, and values nodes.
- User-authored structs with split/make/break nodes.
- Reflected engine structs available without maintaining a second schema.
- Typed enums with named options in pins and Inspector controls.
- Versioned serialization and migration rules for collection and struct values.
- Execution-budget-aware loop nodes with useful failure messages.

### Completion gate

A graph can store and iterate an inventory-like array of structs, save/load it, expose it where
appropriate, and reject incompatible element or field types during authoring.

## Milestone 5 — Advanced flow and time nodes

Provide common gameplay control patterns without requiring custom native nodes.

### Deliverables

- For Loop, For Each, While, Do Once, Do N, Gate, Flip-Flop, Multi-Gate, and Select nodes.
- Timer-by-event and timer-by-function-name nodes backed by the engine timer system.
- Timeline assets or embedded timeline tracks for float, vector, color, and event output.
- Retriggerable Delay, cooldown, debounce, throttle, and latent sequence nodes.
- Explicit cancellation and reset inputs for latent flow nodes.
- Clear behavior under pause and global/custom time dilation.

### Completion gate

A graph can implement a cooldown ability, animated door, and repeated timed event without native or
Lua glue, and every latent operation stops safely when its entity or scene is destroyed.

## Milestone 6 — Event dispatchers and graph interfaces

Add typed, decoupled communication between scene objects.

### Deliverables

- User-defined events with typed parameters.
- Event dispatcher creation, bind, unbind, broadcast, and lifetime cleanup.
- Graph interfaces that define required functions/events without providing implementation.
- Interface-call nodes that safely handle unsupported targets.
- Direct event delivery to a selected entity in addition to current shared-bus publication.
- Validation for duplicate signatures, missing implementations, and incompatible bindings.
- Debugger visibility for event sender, receiver, payload, and dispatch path.

### Completion gate

A switch graph can broadcast a typed event to multiple listeners and call an interface on an unknown
object type without hard-coded class names or dangling bindings.

## Milestone 7 — Visual state-machine graphs

Add a reusable high-level state model for gameplay logic. This complements the animation graph and
does not replace it.

### Deliverables

- State, transition, entry, exit, and state-update graph sections.
- Typed transition conditions and priorities.
- Optional hierarchical states and reusable state subgraphs.
- State history, transition cooldowns, and controlled interruption.
- Runtime state highlighting and transition history in the debugger.
- Script API access for querying and requesting state changes.

### Completion gate

An enemy can be authored with Idle, Patrol, Chase, Attack, Stunned, and Dead states, with current
state and transitions visible during Play.

## Milestone 8 — Async task model and cancellation

Generalize latent execution for operations that finish outside the current frame.

### Deliverables

- A typed task handle and explicit Completed, Failed, Cancelled, and Timed Out paths.
- Async wrappers for navigation requests, level streaming, asset loading, dialogue, and other engine
  services that already complete asynchronously.
- Ownership by graph instance/entity so unloading or destruction cancels outstanding work.
- Timeouts, cancellation tokens, and safe result delivery on the main thread.
- Debugger list of active tasks, elapsed time, owner, and current status.

### Completion gate

A graph can request a streamed level or navigation path, continue on success/failure, and be deleted
mid-request without leaking, resuming a dead instance, or mutating the registry from a worker thread.

## Milestone 9 — Compiled graph execution

Keep `.3dgvs` as the source asset while generating a faster runtime representation.

### Deliverables

- Lower validated graphs into a compact instruction or intermediate representation.
- Resolve node descriptors, pins, variables, and property/function bindings during cooking.
- Store derived compiled data using the existing graph and node-registry content hash.
- Skip unchanged graph compilation and invalidate caches when graph data or reflected APIs change.
- Preserve source node IDs for errors, breakpoints, profiling, and execution highlights.
- Fall back cleanly to validation errors when compiled data is stale or unavailable.
- Add performance comparisons for interpreted and compiled execution.

### Completion gate

Packaged games execute cooked graph data without repeated node/pin lookup, debugger source mapping
still identifies authored nodes, and benchmark graphs show a measurable reduction in script time.

## Milestone 10 — Automated graph testing

Make graph behavior verifiable without repeatedly entering Play mode.

### Deliverables

- A graph test asset or test section with setup, input events, simulated ticks, and assertions.
- Assertions for values, emitted events, component properties, runtime errors, and completion time.
- Deterministic clock and seeded random input for test runs.
- Headless test execution suitable for the existing Automated Test panel and command line.
- Regression tests for serialization, migration, hot reload, latent cancellation, and instruction limits.
- Test results that link directly to the failing graph and node.

### Completion gate

Core node libraries and representative gameplay graphs run in an automated build, and failures name
the graph, test, node, expected value, and actual value.

## Milestone 11 — Asset lifecycle and migration hardening

Ensure visual-script assets remain healthy as a project evolves.

### Deliverables

- Content Browser creation, duplication, rename, move, delete, and reference-repair integration.
- Dependency Viewer coverage for graph calls, exposed overrides, asset defaults, and node properties.
- Clear warnings before deleting an asset referenced by a graph.
- Migration steps for every future `.3dgvs` version and scene/runtime override format.
- Missing-node placeholders that retain pins and authored data until the node type returns.
- Cleanup/reporting for overrides whose variables were removed or changed type.
- Project-wide graph validation and repair command.

### Completion gate

Renaming or moving graphs and their dependencies does not break references, deletion reports every
dependent asset, and older test assets upgrade without losing recoverable authored data.

## Milestone 12 — Templates, documentation, and release hardening

Finish the workflow for designers and packaged games.

### Deliverables

- Templates for interaction, trigger, pickup, moving platform, door, ability, projectile, UI event,
  AI helper, and level-streaming graphs.
- Generated node reference integrated into engine documentation.
- A visual-scripting tutorial that builds one complete Wizard's Trial gameplay feature.
- Context-sensitive node/pin tooltips with examples and common failure explanations.
- Packaging audit showing included graphs, dependencies, compiled-cache status, and validation errors.
- Shipping-build verification that editor-only graph data and disabled diagnostics are excluded.
- Performance and memory budgets with profiler counters for graph instances, tasks, and instructions.

### Completion gate

A new user can create a graph from a template, connect it to a scene object, debug it, package it,
and run it in the standalone player by following the documentation alone.

## Implementation rules for every milestone

- Complete and verify one milestone before starting the next.
- Preserve backward compatibility or provide an explicit versioned migration.
- Keep runtime code independent of ImGui and editor-only objects.
- Use stable IDs for every persisted reference; never make runtime correctness depend on display names.
- Reuse engine reflection, asset registry, event bus, timers, physics, and gameplay services instead of
  building duplicate visual-scripting-only systems.
- Apply structural ECS changes only through the existing deferred safe point.
- Add bounded memory/work limits to loops, latent tasks, diagnostics, and editor caches.
- Build and test both `3DGEditor` and `player` before marking a milestone complete.
- Update this file by changing the milestone status and recording the completion date and verification.

