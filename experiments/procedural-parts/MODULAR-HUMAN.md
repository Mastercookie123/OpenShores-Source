# Modular human revision and shading repair

Current revision: **385 components**, including the actual human nose type 2,
mouth type 6 and eye type 2 in all four sizes. The earlier pack targeted the wrong
nose/mouth families. Upper arms are less exaggerated and the jaw is broader.
Hand/foot bounding extrema are now preserved because gameplay derives grip and
stance measurements from mesh bounds as well as explicit markers.

The revised pack passes 13/13 isolated-process native RTX 5080 cases, with zero
grip/camera delta, plus repeated walking/strike frames at a fixed preview time.
The fixed clock is guarded by render-audit variables and is not used by ordinary
clients. Four asset contract tests also check exact human hand/foot extents.
Missing-pack and bad-checksum negative controls are correctly rejected.

Native captures: workspace `Artifacts/visual-tests/face-pass/index.html`.
Previous candidate captures: `Artifacts/visual-tests/pre-face-pass/index.html`.
Launch remains `Hazeron.ps1 ModularClient`; restart it to load changed assets.
Original palette and hair artwork remain in use; this is an incremental model
revision, not a complete new character-art library.

**Previous revision verification correction:** the native visual lab in `../../tools/visual_test`
runs A/B in separate processes. It found approximately 0.0012 units of hand-grip
drift in the previous candidate. The older 80-case comparison below shared global
hand/foot measurement caches and masked that difference. Its zero-drift result
did not establish that candidate's live attachment compatibility. Its captures
and failures are in workspace `Artifacts/visual-tests/pre-face-pass/index.html`.
That earlier candidate was not visually accepted.

## Run

From the Windows workspace: `./Hazeron.ps1 ModularClient`.
`OriginalModularClient` uses the same executable with the original resources.
Installed binary: `Client/AuSceneClient-modular.exe`. Assets are separate in
`Client/body-parts-modular`; the previous visual and procedural packages remain
available. The launcher sets OPENSHORES_BODY_PARTS for the child process only.

## What changed

The 385-component pack includes reauthored dry-skin human skull, four nose and
mouth sizes, torso connectors, necks, arms, legs, hands and feet. It includes the
actual default human hand type 3 and foot type 2, plus alternate type 0. Other
skin families retain the compatible component treatment from the earlier pack.

`tools/body_parts/human_shapes.py` edits anatomical cages in their source frames:
rounded limb cross sections and taper, cheek/jaw/brow shaping, nose/lip relief,
waist/sternum shaping, palm and instep adjustments. These are reworked original
components, not a new scanned or artist-sculpted human. Original palette artwork
and hair remain a substantial limit on appearance.

Texture duplicates are welded for geometric editing while preserving per-corner
UV islands. True geometric openings and TopConnect/BottomConnect rings retain
source coordinates; scale, eye and attachment markers remain unchanged. Geometry
is subdivided only along same-palette edges. This matters because the engine's
skin-region attribute is binary: inserting vertices across a region transition
would create false palette edges. Near-identical connection samples are snapped
back to original floats after Blender operations; larger changes fail validation.

The shading repair recalculates angle-weighted normals after DNA scaling when
the enhanced component path is enabled. UV duplicates share shading without
changing their positions or weights. Geometry normals follow the generated
surface instead of retaining stale unscaled normals. Skin normal-map amplitude
is reduced from 0.9 to 0.12 to remove exaggerated distant lumpiness. Legacy
refinement remains disabled by default; this normal repair adds no runtime
subdivision. Armor material detail still bypasses the character-detail shader.

## Rebuild and edit

```bash
blender -b --python-exit-code 1 --python tools/body_parts/build_pack.py -- --human-set
# Export an artist edit from the existing file:
blender -b experiments/procedural-parts/limb-components.blend \
  --python-exit-code 1 --python tools/body_parts/build_pack.py -- --human-set --export-current
python3 tools/body_parts/test_contract.py
python3 tools/body_parts/package.py --client-dir build/windows-debug/bin --folder body-parts-modular
```

The saved `limb-components.blend` contains tagged editable components. Unsupported
layouts, failed seam checks and parts without editable interior detail fall back
to their original resources. `pack/manifest.json` is authoritative; stale files
not listed in it are not loaded or packaged.

## Verification

- Four Python tests: lossless containers, all published seam/marker contracts,
  moved seam/marker rejection, and distinguishing UV copies from actual openings.
- Five C++ geometry/shading tests, 814 assertions: DNA-scaled normals, preserved
  coordinates, UV islands and bone weights, palette edges and refinement seams.
- 48 production body/hair fragment shader variants compile.
- 80 DNA/sex cases in four poses compare grips, camera/connection anchors and
  bone matrices. Includes unusual generated anatomies and missing/checksum fallback.
- Actual production DpBody render checks include cascaded 2048px sun self-shadows,
  production depth rendering and OpenGL error checks. `modular-shadow/portrait.png`
  and `body.png` in ../visual-overhaul are renderer captures, not live-world shots.

The material height map and exact generation prompt remain documented in
`../visual-overhaul/material-provenance.json`. No rejected atlas repaint is used.
The old fixed whole-body human prototype remains retired. DNA generation,
variable body-part counts, ordinary clothing, tools, collisions and server data
continue through the original systems.

Live-world appearance, attacks and crowd performance still need acceptance for
this revision. This does not complete terrain/building art, new clothing artwork,
new hair artwork or a modern renderer migration.
