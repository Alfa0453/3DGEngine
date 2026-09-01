# Render Debugger

The Render Debugger exposes the engine's live render targets, material diagnostic
views, GPU pass timings, shadow information, and draw-call ownership in one panel.
Open it from **Panels > Debug & Diagnostics > Render Debugger**.

## Live material and lighting views

Use **Material / Lighting View** to replace the lit viewport temporarily with a
diagnostic result. Important views include:

- Material Base Color, Geometric Normal, and Shading Normal
- Imported Material Slot
- Raw and filtered GTAO
- Direct, indirect, probe, SSGI, and reflection contributions
- Directional Shadow, shadow cascade assignment, and PCSS filter radius

These controls are a non-destructive editor override. **Use World Setting** returns
control to the Lighting Debug View saved in World Settings. **Lit** forces the normal
lit result without changing the scene asset.

## Inspect retained render passes

Choose a target from **Pass**. The list adapts to the features that have created
render resources during the session and can include:

- presented viewport color and depth;
- linear HDR scene color, bloom, LDR composite, TAA history, and volumetrics;
- GTAO view position, normal, velocity, raw/filtered AO, and bent normals;
- raw and filtered SSGI and the SSR composite;
- the four directional-shadow cascade layers.

Color targets have a preview-exposure control. Scalar, depth, and position targets
have a display range. These controls affect only the diagnostic image.

Use **Capture** to freeze the currently displayed pass while the game or editor keeps
running. Use **Resume Live** to return to the active GPU texture. Click the image to
read its displayed RGBA pixel. **Refresh Shadow Maps** invalidates directional,
point, and spot shadow caches so the next render rebuilds them.

## Performance ownership

**Draw-call Ownership** groups render submissions by the GPU scope that owned them,
such as Scene, Post, or UI, and also reports one-sided versus two-sided shadow caster
draws. **GPU Pass Timings** uses delayed GPU queries, so values can say “warming up”
for the first few frames and do not stall the render thread.

The engine uses a forward PBR renderer, so there is no persistent deferred material
G-buffer. Material properties are inspected through the live material debug views;
screen-space targets that genuinely remain available are inspected directly.
