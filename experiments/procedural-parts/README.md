Current revision: see [modular human and shading repair](MODULAR-HUMAN.md). The following describes the previous visual package.

# Procedural visual development

Players and creatures retain the original DNA assembly, skeleton, clothing palette,
scale/connect markers, camera and carried-item attachments. Fixed MakeHuman bodies
are not used. The previous 16-arm client remains available for comparison.

The visual pack contains 299 compatible components across eight skin types:
arms, legs, torso connectors, selected necks, ears and crests. Interior cage
relaxation, simple subdivision and small anatomy-aware contour adjustments preserve
original connection positions. Authored corner normals are interpolated; exported
vertices are indexed by position, UV and deform memberships. Original skull, nose
and mouth components remain in use: experimental remeshing created visible palette
edges, so those assets were excluded from the published manifest. Unsupported
containers and failed seam checks also retain original resources.

## Run and compare

Run `Hazeron.ps1 VisualClient` from the workspace for `Client/AuSceneClient-visual.exe`
with `-visual-overhaul`. `OriginalVisualClient` launches that binary with original
components and materials. Pack location: `Client/body-parts-visual`. The older
`ProceduralClient` uses its separate 16-arm package in `Client/body-parts`.

The visual material pass adds filtered skin microdetail, subtle cloth weave,
reduced dry-skin gloss and coarse skin mottling, and existing hair fiber shading.
It uses the legacy renderer; this is not physically based rendering or an engine
migration. Original palette artwork, clothing UVs and alpha remain authoritative.
Power armor bypasses the new body material treatment.

## Authoring

`limb-components.blend` contains editable component objects tagged with source
filename, object identity and SHA-256. Preserve all group names and scale/connect
markers. Original boundary positions must remain exact; UV validation permits at
most 0.0001 rounding error (0.21 texel at 2048). Marker records remain byte-identical.
The custom .3dsN exporter supports multiple material assignments; unsupported
layouts are recorded in `unsupported-components.json`.

```bash
blender -b --python-exit-code 1 --python tools/body_parts/build_pack.py
# To export edits from the saved cage instead:
blender -b experiments/procedural-parts/limb-components.blend \
  --python-exit-code 1 --python tools/body_parts/build_pack.py -- --export-current
python3 tools/body_parts/test_contract.py
python3 tools/body_parts/package.py --client-dir build/linux-debug/bin --folder body-parts-visual
```

Loader verifies original and replacement component hashes and falls back to
original resources on mismatch. Packaging verifies material hashes. The detail map
is an external image loaded through the renderer texture cache.

## Validation and limits

`ValidateProceduralParts` compares 80 DNA/sex combinations, eight skin types,
five anatomical configurations and four poses at frozen animation phase. It checks
hand grips, camera/connection anchors, all bone matrices, finite geometry, arm
weights, missing pack/checksum fallback and retired fixed-human behavior. Every
configuration now loads upgraded geometry. `test_contract.py` tests lossless
containers and rejected marker/seam edits. Shader validator compiles 48 variants.

`RenderProceduralAvatar` uses actual DpBody rendering, production shaders and
OpenGL framebuffer readback. See `../visual-overhaul/game-baseline` and
`../visual-overhaul/game-enhanced`; Blender comparison images are UV inspection
only. Live motion and crowd performance still need acceptance on the final pack.
The user verified held-tool movement on the previous 16-arm package.

This is an incremental character/creature visual pass, not a complete modern art
replacement. Hands, faces, most hair and clothing artwork remain legacy assets.
Terrain, buildings, water, effects and UI have not been overhauled in this pass.
The next substantial improvement requires artist-authored modular cages and UV
artwork tested across generated anatomies, rather than subdivision alone.

Assets derived from game resources retain the project's existing license. The
accepted generated height texture and exact prompts are documented in
`../visual-overhaul/material-provenance.json`; the rejected atlas candidate is
not shipped.