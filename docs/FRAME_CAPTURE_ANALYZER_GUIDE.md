# Frame Capture Analyzer

The Frame Capture Analyzer records one editor frame and presents its work as a
cross-system timeline. Open it from **Panels > Debug & Diagnostics > Frame Capture
Analyzer**.

## Capturing a frame

1. Enter Play when diagnosing gameplay, or remain in Edit mode for editor/render work.
2. Open the Frame Capture Analyzer.
3. Select **Capture Next Frame**.
4. Reproduce the expensive action. The completed frame appears on the next UI update.

Enable **Continuous** only while looking for an intermittent spike. It replaces the
displayed capture every frame. Disable it when the spike is visible so the result stays
frozen for inspection. Capture bookkeeping and CPU timing are inactive when neither a
one-shot nor continuous capture is requested.

## Reading the timeline

Each row owns one kind of work:

- **Frame** shows the editor update and the complete captured-frame span.
- **Scripts** separates per-frame script updates from fixed gameplay updates.
- **AI** covers behavior-tree, perception, steering, and navigation work.
- **Physics** includes each fixed simulation step and ragdoll preparation.
- **Animation** includes graph evaluation and foot IK.
- **Rendering** separates scene submission, post processing, and the game HUD.
- **Particles**, **Audio**, and **UI** expose their respective update/build costs.
- **GPU** shows the latest asynchronously resolved GPU scopes.

Hover a colored event for its start time and exact duration. Select it to retain the
event index while inspecting the capture. Use **Zoom** and the time scrollbar to inspect
short events inside a long frame.

The table below the timeline sorts the twelve longest CPU events. Start there when a
frame spikes, then locate the event on its lane to see what ran before and after it.

## Interpreting fixed updates

More than one Scripts, AI, Animation, or Physics block means the engine performed
multiple fixed simulation steps during that rendered frame. The header reports the
step count. Repeated blocks commonly indicate recovery after a slow frame; if the
count frequently reaches the engine's fixed-step limit, optimize the longest fixed
lane or reduce the amount of simulation active at once.

GPU query results are delayed by design so reading them does not stall rendering.
They are the latest available GPU frame and should be used for pass-cost comparison,
not exact CPU/GPU event alignment. The draw-call count follows that same GPU sample.

## Recommended workflow

Capture once with the Profiler visible to identify a spike, then use this analyzer to
find its owning system. Use the Render Debugger afterward when the Rendering or GPU
lane is dominant; use the gameplay and physics debuggers when AI or Physics dominates.
