# Human asset replacement prototype

**Retired from the live character path.** This fixed-body experiment failed the
procedural-generation requirement. Its files remain an offline art reference.
Use `../procedural-parts/README.md` and the `ProceduralClient` launcher instead.
The former `HumanClient` launcher now opens the procedural client.

This is an actual replacement mesh, skin/eye atlas, hairstyle, work suit and
shoes. It is a local, opt-in experiment in the existing OpenGL client.

The editable Blender source and generated game files are under `output/`.
`human-preview.png` and `face-preview.png` use Blender lighting. The
`game-pose-*.png` images use the production mesh loader, real game bone
transforms and a simple OpenGL preview shader. Neither preview is a screenshot
of the live world.

## Status

This asset is an offline reference. Its old launch flag no longer enables a
live fixed-body replacement. Use the procedural component pipeline instead.

## Pipeline

1. `ExportHumanReference` constructs the game's default male human, creates its
   actual body parts, poses it standing, and exports geometry plus 61 bone
   transforms. The reference file is `legacy-reference.json`.
2. `build_prototype.py` applies the pinned MakeHuman male shape target, fits its
   A-pose to the game reference and fits the authored clothing/hair/eyes using
   MakeHuman proxy coordinates. Hidden body faces under clothing are removed.
3. Artist-authored source weights are mapped onto the game's bones. Finger
   weights collapse onto its hand bones; facial weights collapse onto its head.
   Clothing weights interpolate the same source weights through proxy data.
4. The full character gets a 2048px diffuse atlas and alpha cutout mask. The
   output JSON contains world-space bind geometry, four influences per corner,
   separate head/body triangles, and inverse bind matrices.
5. The client uses `animatedBone * inverseBind` for prototype bone uploads.
   This bridge is necessary because legacy meshes use a different coordinate
   convention. Physics, DNA, camera anchors and server data are unchanged.

Rebuild in Blender 5.0.1 or a compatible version:

```bash
python3 fetch_assets.py
blender -b --python-exit-code 1 --python build_prototype.py
python3 package.py --client-dir ../../build/linux-debug/bin
```

`fetch_assets.py` retrieves selected files from the official CC0 system pack
with HTTP byte ranges and records content hashes in `asset-manifest.json`.
The OBJ, shape target, weights and fitting metadata are pinned by
`upstream-commit.txt` and `mpfb-commit.txt`.

## Validation

Build `ExportHumanReference` and run it with an output reference filename plus
the generated asset directory. It exercises the production loader, rejects six
malformed mesh inputs, and renders standing, walking, crouching and right-arm
attack poses with the actual game's matrices. Check those images visually;
successful drawing alone does not establish good deformation.

The current mesh has 32,628 triangles. Inspect the generator log
for the exact current count and bind-pose reconstruction error.

## Limits before default use

This first prototype uses fixed male human proportions, coloring, hairstyle and
clothing. It applies only to selected humanoid anatomy with dry skin and no
power armor; other creatures retain their original appearance. It is not a
replacement for DNA-driven character customization. Different human proportions
still need fitting and held-item/camera alignment checks in the live client.

The outfit is visual only; changing ordinary game clothes does not change this
prototype outfit. Finger articulation, facial expressions, new clothing choices,
skin roughness/normal maps, LODs and crowd profiling are not implemented. Hair
uses alpha cutouts in the body shader; its depth-only shadow silhouette still
needs a cutout-aware treatment. The attack pose can pinch around the shoulder.

Accept this only after checking the same avatar in the live world: appearance,
walking, attacks, held-item contact, first person, clothing/armor fallback,
night lighting, hair edges and performance. Keep the ordinary client available.

## Provenance

The base mesh, eyes, rig weights and proxy metadata come from the official
MakeHuman repository. The male shape target comes from official MPFB. Skin,
eye texture, `short01` hair, `male_worksuit01` and `shoes01` come from the official
MakeHuman system assets CC0 pack. Their headers and `LICENSE.ASSETS.md` record
the CC0 release. `MAKEHUMAN-LICENSE.md` explains the separation between asset
and application-code licensing. No MakeHuman or MPFB application code is embedded
in the game.

Sources: https://github.com/makehumancommunity/makehuman and
https://github.com/makehumancommunity/mpfb2 .
