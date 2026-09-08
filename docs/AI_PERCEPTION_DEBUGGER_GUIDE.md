# AI Perception Debugger

The AI Perception Debugger explains why a live agent can or cannot detect its target.
Open **Panels > Debug & Diagnostics > AI Perception Debugger**, then enter Play mode.
The panel is intentionally empty outside Play mode because it observes runtime AI
decisions rather than authored estimates.

## Live agent table

Each row shows the agent, its team, current AI state, selected target, sight result,
hearing source, and the first reason sight failed. The reason progresses through the
same checks used by gameplay:

1. A valid target must be assigned or acquired.
2. The target must be inside the authored vision range.
3. The target must fall inside the vision half-angle.
4. The physics line of sight must reach the target without another collider blocking it.

Select a row for exact distance, angle, range, FOV, line-of-sight, hearing loudness,
team, brain type, and last-known world position. Use the text, team, and sensing filters
to isolate an encounter. **Selected only** applies to both the table history and scene
visualization.

## Scene guides

- Blue/yellow cone: authored vision cone; yellow means the target is currently visible.
- Green/red ray: clear or failed target visibility.
- Purple circle: hearing range; it brightens when a sound is heard.
- Orange cross: the last target or stimulus position remembered by the agent.
- State marker/path: the same live state and route already used by AI debug rendering.

The perception panel does not automatically draw the navigation mesh. Turn on the
general AI debug option when navigation cells, polygons, and routes also need diagnosis.

## History

History records target changes, sight acquisition/loss, audible stimuli, and squad
alerts while the panel is open. **Freeze history** pauses recording without stopping
gameplay. The buffer is bounded so leaving the debugger open cannot grow memory without
limit. **Clear history** also resets transition baselines, which prevents stale events
from a previous encounter.

## Common diagnoses

- **No target:** verify the configured chase target, hostile team numbers, or automatic
  hostile targeting.
- **Out of range:** increase the character's AI vision range or move the target closer.
- **Outside FOV:** check model forward orientation and the vision half-angle.
- **Occluded:** inspect the red ray and collision geometry between the eye and target.
- **Squad instead of sound:** the agent did not hear a sound; it received a teammate's
  remembered target position within the squad alert radius.
- **No hearing:** give the agent a non-zero hearing range and ensure gameplay emitted a
  sound stimulus with enough radius and remaining lifetime.
