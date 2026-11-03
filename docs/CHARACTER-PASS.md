# Character mesh and material comparison

This opt-in pass changes the actual cached creature body meshes and production
body/hair shaders. It does not replace DNA, animations, collision shapes or the
original body-part files. It is not a new sculpted character asset.

## Compare

The workspace contains `Client/AuSceneClient-character.exe`. Use the same
character, camera, time, resolution and general graphics settings for each run:

```powershell
.\AuSceneClient-character.exe -alocalhost -graphics-enhanced -character-classic -log
.\AuSceneClient-character.exe -alocalhost -graphics-enhanced -character-refined -log
```

Close the first client before logging into the same avatar in the second.
The workspace `Hazeron.ps1` exposes these as `ClassicCharacterClient` and
`CharacterClient`. A freshly built executable supports both flags. Without
`-character-refined`, character refinement remains off.

## Geometry

- Recompute angle-weighted surface normals after DNA scaling, welding shading
  across coincident UV vertices. Authored normals guide grouping; opposite
  sides of double-sided surfaces are kept separate.
- Refine small body-part meshes by up to two curved edge subdivision passes.
  Curvature displacement is bounded to 12% of the edge length per pass.
- Keep open boundaries, inconsistent edge normals and non-manifold edges
  straight. This avoids pulling apart separately generated anatomy pieces.
- Interpolate UVs and bone influences, combine matching bone indices, retain
  at most four influences and normalize their weights.
- Preserve triangles whose top/bottom skin palette flags differ because the
  legacy vertex format cannot represent fractional values of that flag.
- Skip inputs over 4,000 triangles; refinement stops once the current pass
  exceeds that threshold, bounding output at 16,000 triangles per object.

The real `0Skull.3dsN` test asset changes from 626 to 10,016 triangles. This
increases mesh generation cost, cache memory and draw cost; it is deliberately
opt-in until in-game quality and performance have been compared. Intentionally
angular creature features can become softer. Existing UVs, anatomy boundaries
and a low-resolution base shape still limit the result.

## Materials

All six body and six hair fragment variants are updated, covering interiors,
planet surfaces, ringworlds and fog. Shader source archives are now rebuilt
automatically as client dependencies so source edits reach the executable.

- Skin uses a small wrapped-diffuse approximation for softer light transitions.
  Planetary horizon attenuation also gates this contribution at night.
- The weak skin highlight is untinted, rather than multiplied by the skin
  pigment. Clothing retains its prior lighting path.
- Fine skin modulation and hair fibers fade out according to screen-space
  derivatives to reduce distant aliasing.
- Hair uses a weaker highlight and a small UV-oriented fiber-normal variation.
  This improves surface presentation; it does not create a new hairstyle mesh.

These are lightweight legacy-renderer treatments, not full PBR skin, physical
subsurface scattering or new high-resolution skin/hair artwork.

## Validation and tools

`RunTests` includes planar UV/skinning, shared curved edges, mixed-palette
topology and cross-bone transition cases. `RenderCharacterProbe` uses the
game's own custom body loader to render a real body part before and after
refinement under identical simple lighting:

```bash
HAZERON_BUILD_CONFIG=debug bash docker/linux/build.sh RunTests
./build/linux-debug/bin/RunTests
HAZERON_BUILD_CONFIG=debug bash docker/linux/build.sh RenderCharacterProbe
./build/linux-debug/bin/RenderCharacterProbe \
  resources/bodies/shards/00/data/0Skull.3dsN /tmp/character-comparison
python3 tools/rendering/validate_character_shaders.py
```

The shader validator requires `glslang-tools` and compiles 48 production
fragment combinations: refined/classic, shadowed/unshadowed, across the 12
changed shaders. The body-part comparison isolates geometry and normals; it
does not prove the complete in-game avatar or hair appearance.

In-game acceptance includes a close-up face/shoulder comparison, walking and
arm poses without gaps, hair transparency, clothing and nighttime behavior.
Check a crowd before making this the default. A future asset overhaul needs
better base anatomy, facial features and authored skin/hair materials; adding
triangles cannot supply those features by itself.
