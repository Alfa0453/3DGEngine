# Animation Retargeting Tool

Open **Panels > Animation & Characters > Animation Retargeting**.

## Retarget an animation

1. Import the source and target skeletons as engine-owned `.3dgskel` assets.
2. Import the source animation as an engine-owned `.3dganim` asset. Animation-only
   import can reuse the source skeleton and does not need another mesh.
3. Choose the source skeleton, target skeleton, and source animation.
4. Select **Load / Auto Map**. Bone namespaces and common rig prefixes are ignored
   while matching, so names such as `mixamorig:Spine` can map to `Spine`.
5. Inspect the blue source and orange target previews. Play or scrub the timeline.
6. Select a mapping to change its source/target bone, rotation correction,
   translation scale, or translation transfer. Add manual mappings for unmatched bones.
7. Enable **Transfer Root Motion** when the output should preserve authored movement.
8. Save the `.3dgretarget` profile for reuse.
9. Enter an output name and retarget either the selected clip or every clip.

Retargeted animations are saved under
`Content/GameAssets/AnimationRetarget/Clips` as native `.3dganim` assets. They depend
on the target skeleton and can be selected by the Clip Editor, Animation Graph,
Character Editor, editor Play, and packaged games like any imported animation.

## Troubleshooting

- **Unmapped bones:** add a manual mapping or rename bones in the source DCC rig.
- **Twisted limbs:** correct that mapping's rotation offset while watching the preview.
- **Sliding or oversized movement:** adjust global scale or the root mapping's
  translation scale.
- **Unwanted movement:** turn off root-motion transfer or disable translation on the
  root mapping.
- **Missing animation:** ensure the source animation was imported against the selected
  source skeleton.
