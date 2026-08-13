# Lighting Analysis Guide

The Lighting Analysis panel helps find expensive, inconsistent, dark, or
overexposed lighting before packaging a level.

## Analyze a level

Open **Panels > Debug & Diagnostics > Lighting Analysis**, then press **Analyze
Level**. The panel derives analysis bounds from visible scene objects and samples
the authored lights and World Settings on a configurable grid.

Use **Grid Resolution** to balance detail and analysis cost. Adjust the unlit,
overexposed, overlap, and shadow warning thresholds to match the visual target of
the game. Reanalyze after moving lights or changing materials.

## Viewport modes

- **Light Complexity** shows how many lights influence each sampled area. Green is
  inexpensive, while yellow and red indicate increasingly expensive overlap.
- **Shadow Coverage** shows overlapping shadow-casting lights. Magenta warning
  tiles commonly indicate redundant shadow maps.
- **Exposure** maps sampled illumination from blue through red and flags values
  above the configured exposure threshold.
- **Unlit Areas** marks samples below the configured minimum in red.
- **Transparency Cost** marks objects using transparent, transmission-heavy, or
  heavily cut-out materials.

The overlay uses editor-only shader lines and is never included in the packaged
game. Disable **Viewport Overlay** when comparing the final scene image.

## Findings and reports

Findings cover oversized light ranges, inactive lights, local-light complexity,
shadow overlap, directional cascade pressure, shadow filtering, unlit coverage,
overexposure, and transparent-material cost. Select an object finding to frame it
in the viewport.

Press **Export Report** to write
`Content/Reports/LightingAnalysis.txt`. Use this report during lighting reviews and
repeat the analysis after optimization. Treat thresholds as guidance: intentional
darkness or a cinematic highlight can be valid when it supports the art direction.
