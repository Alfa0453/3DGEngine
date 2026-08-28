# Production Lighting Pipeline

This document describes the production-ready lighting path introduced by Lighting Improvement Pass 5. It complements the authoring-oriented Lighting Analysis guide.

## Quality profiles

World Settings uses one lighting quality value for the complete pipeline:

| Profile | Intended use | Main behavior |
|---|---|---|
| Low | minimum supported GPU | sparse shadow updates, one local shadow, dynamic GI disabled, low SSGI and fog cost |
| Medium | integrated/entry GPU | limited dynamic GI, two local shadows, moderate reflection budget |
| High | desktop default | balanced GI, four local shadows, two reflection probes, higher fog quality |
| Ultra | high-end/cinematic | highest sampling, larger reflection budget and more frequent updates |

The profile controls shadow blocker/filter samples, cascade update intervals, shadowed local-light count, dynamic-GI ray budget, SSGI steps, reflection residency, volumetric resolution/light count, clouds, bloom and exposure sampling. This prevents contradictory per-system presets. Authored feature switches still decide whether a feature is enabled.

## Runtime scheduling

- Directional cascades cache their matrices and caster revision. Near cascades update most often; distant cascades are staggered. Moving skinned casters keep the nearest result responsive.
- Point and spot shadow maps are regenerated only when their light or a caster changes. Decorative local lights can opt out of shadows, and quality profiles cap the number rendered.
- Dynamic GI uses its explicit ray budget, prioritized probes, sleeping/classification, hysteresis and relocation. It never exceeds `maxGiRaysPerFrame`.
- Reflection probes are looked up through a spatial grid, limited to two blends, and streamed around the camera. The profiler warns when resident captures exceed their profile budget.
- Volumetric lights are distance culled and priority sorted. Lights can disable `Affect Volumetric Fog` or set a higher volumetric priority. Fog history is discarded after relevant light/volume changes, camera cuts and teleports.
- Bloom performs no extraction or blur passes when disabled. Auto-exposure uses a percentile histogram, throttled readback, separate adaptation rates and an EV dead zone.

## Authoring workflow

1. Choose the target lighting quality in World Settings.
2. Keep shadow distance as short as gameplay allows. Disable shadows and volumetric contribution on decorative lights.
3. Place reflection probes around rooms and hero spaces, then capture them. Prefer 128 or 256 resolution.
4. Configure dynamic GI with a finite volume and use the profile ray budget as the upper bound.
5. Use local fog volumes for bounded effects instead of raising global fog density.
6. Open **Lighting Analysis**, run a scan and inspect light overlap, shadow coverage, exposure and unlit areas.
7. Open **Optimization Auditor**, run **Scan Level**, repair critical findings, and export the report for the build record.
8. Build lighting and save the level. Stale or legacy derived lighting data must be rebuilt rather than silently reused.

## Profiler and budgets

The Profiler lighting section reports GTAO, SSGI trace/denoise, dynamic-GI update/upload and ray counts, active/sleeping probes, irradiance/reflection memory, reflection residency/candidates, shadow cache renders/hits, shadow memory, screen-lighting/post memory, and the latest capture/build cost.

Do not treat a profile as a guarantee. Record measurements in the target scene on the target hardware. A useful baseline includes editor/game mode, resolution, profile, CPU scene time, GPU scene/post time, shadow renders/cache hits, GI rays, draw calls and lighting memory. Compare the same camera path before and after a change.

## Validation expectations

The Optimization Auditor flags excessive shadow lights, uncaptured or oversized reflection probes, reflection-memory pressure, too many fog volumes, dynamic-GI requests above the ray budget, excessive SSGI steps, missing assets, high texture/geometry cost and other scene risks. Lighting Analysis remains the visual correctness validator. Neither tool silently changes scene quality.

## Shipping defaults

- High is the desktop default; Medium is the fallback for constrained GPUs.
- All optional effects have a real off path.
- Reflection and GI resources remain bounded by explicit budgets.
- Old lighting files produce an explicit rebuild request.
- Runtime builds load authored settings and derived captures; editor-only overlays, analysis grids and debug modes are not required for gameplay rendering.

## Known limits

- Runtime GPU timings depend on the driver and representative scene; they cannot be inferred from source code.
- Reflection residency currently uses distance relevance and reports budget overflow rather than silently lowering authored capture resolution.
- SSGI is a screen-space supplement, so off-screen emitters and geometry require dynamic or baked GI.
- Volumetric fog uses a camera-aligned reduced-resolution integration rather than a full volumetric world texture.
