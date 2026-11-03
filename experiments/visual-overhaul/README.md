# Visual pass: November 1, 2026

See [component documentation](../procedural-parts/README.md) for scope, launch,
compatibility checks and authoring. Production renderer captures are `game-baseline`
and `game-enhanced`, each with `portrait.png` and `body.png` under identical lights.
These are the real game renderer with a standalone camera, not screenshots of the
live world. Other Blender images are UV diagnostics and do not represent game lighting.

Accepted image asset: `../procedural-parts/pack/materials/skin-height.png`.
Exact generation prompts and rejected UV atlas experiment: `material-provenance.json`.
The accepted map is used for subtle material microdetail; it does not replace skin
albedo or facial artwork. Final pack intentionally excludes remeshed face pieces.

A complete visual overhaul remains broader than this pass. Priorities are new
procedural face/hand/hair modules, compatible clothing artwork, then environment
art and lighting with explicit world and crowd performance budgets.