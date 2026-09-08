# Collision Analyzer

Open **Panels > Debug & Diagnostics > Collision Analyzer**.

## Edit mode

The **Collider Ownership** tab lists every collider in the open level. It shows the
owning object, shape, object channel, response mask, rigid-body type, sleep state,
and solver-island assignment. Edit mode is intended for checking authoring and
filter setup; live contact fields become available after Play starts.

## Play mode contacts

Start Play with the panel open. **Contacts & Overlaps** keeps the latest 512 events
and distinguishes Enter, Stay, and Exit from blocking contacts and trigger overlaps.
Blocking Enter/Stay rows include the average world contact point, contact normal,
pre-solve penetration depth, and solver impulse. Filter by object, phase, or event
kind. **Clear captured events** also clears the viewport event guides.

Enable **Scene contact guides** to draw the colliding-object relationship and the
contact normal in the viewport. The contact marker is yellow; longer normals indicate
deeper pre-solve penetration. The selected-object, trigger-only, and Enter/Exit-only
filters apply to these guides as well.

## Why a pair does not collide

In **Response Diagnostic**, choose two collider owners. A pair interacts only when:

1. Collider A's response mask contains Collider B's object channel.
2. Collider B's response mask contains Collider A's object channel.
3. The optional global channel matrix allows the two channels.

If either collider is a trigger, an allowed pair emits overlap events but receives no
blocking solver response. The matrix below the diagnosis is read-only and mirrors the
active Play physics world.

## Performance

Event history is captured only while the analyzer panel is open. Collider inspection
and ownership sorting also run only while visible, so normal editor and packaged-game
frames have no analyzer cost.
