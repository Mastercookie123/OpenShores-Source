# Filament / Vulkan procedural renderer prototype

For the newer **playable client integration**, see [LIVE.md](LIVE.md).
This document describes the earlier offline glTF preview only.

This is a working renderer development path for actual game characters. It uses
the official Filament v1.79.0 native Windows viewer as the host. It is **not yet
embedded in the playable Qt game client**, and does not connect to the server.

## Run the implemented preview

Double-click workspace `Try-Filament.cmd`, or run:

```powershell
.\Filament-Preview.ps1
.\Filament-Preview.ps1 -Variant multi
.\Filament-Preview.ps1 -Variant quadruped
.\Filament-Preview.ps1 -Capture -Variant human
```

The interactive viewer renders through Vulkan on the RTX 5080 and plays the
exported game walking cycle. Its viewer controls can orbit the camera and change
lighting/material inspection. This is a separate preview window, not a new
gameplay mode. `Launch-OpenShores.cmd` still opens the existing OpenGL game.

## Implemented data path

1. `ExportRendererScene`, an explicit CMake target linked to the game's real
   AuData/AuBodyPartsRes libraries, constructs DpBody from DNA and loads the
   same optional, checksum-verified modular components. It exports render
   vertices, four bone influences, palette offsets, camera/grip anchors and
   all 61 absolute bone matrices. No MakeHuman or fixed replacement rig is used.
2. `game_bridge.py` samples 13 points over the actual DNA's animation period.
   It exports indexed glTF meshes, normals, UVs, original texture artwork, a
   dielectric rough PBR material, the original bone indices, and TRS animation.
   Mesh instances and all joint nodes share a common coordinate-conversion root.
3. The legacy shader multiplies canonical vertices by absolute game bone
   matrices. Identity glTF inverse binds deliberately retain that convention;
   independent joint nodes represent the absolute matrices, rather than
   inventing a parent chain that would apply those transformations twice.
4. The root converts feet to meters and Z-up to Y-up. Clockwise legacy triangles
   become CCW glTF triangles; texture V and palette offset are converted explicitly.
   Legacy non-unit weights are normalized for glTF: this matches the Cartesian
   position after the legacy homogeneous perspective divide. Normals follow the
   existing post-DNA smoothing path.
5. Filament gltfio loads that scene and provides physically based materials,
   image-based lighting, sunlight/shadows, ambient occlusion and tone mapping.
   The ground plane is a neutral synthetic reference, not exported game terrain.

## Rebuild

The existing workspace build uses `/opt/hazeron/Source` in Ubuntu and the
already-configured Linux Docker cache. Run workspace `build-renderer-bridge.sh`,
then from the workspace in Ubuntu:

```bash
python3 Source/experiments/filament/game_bridge.py \
  --output Artifacts/filament-character/human --variant human --pose walk
python3 Source/experiments/filament/validate_bridge.py Artifacts/filament-character/human
```

Other variants are `multi` and `quadruped`; `--pose strike` exports the game's
strike/block poses. `--dna` accepts an actual 63-character DNA string. Different
top/bottom palette offsets are currently rejected: their interpolated palette
blend requires a custom Filament material. They are not silently discarded.
Ordinary clothes, procedural fur shells and held tools are not yet exported.

The official Windows SDK is under workspace `Artifacts/renderer-sdk/filament-windows`.
Pinned download: https://github.com/google/filament/releases/tag/v1.79.0
Archive SHA-256: `5fc8ea5a06bd3de3f43da9ec72a09458193db3e0b574e06b6b3f8ef94c21fce9`.
The offline preview uses the SDK's bin tree; the live DLL also uses its headers
and static libraries. SDK binaries and
generated scene files are outside the source checkout.

## Verification performed

Human, six-limb-pair and quadruped assemblies all render on native NVIDIA Vulkan.
The logs identify NVIDIA GeForce RTX 5080, driver 610.88, backend feature level 3.
`validate_bridge.py` reads the actual serialized buffers, checks accessor bounds,
indices, weights and texture presence, and reconstructs vertex positions from
the exported animation channels. Across 9,581 sampled vertex/pose checks, maximum
float32 conversion error is under 0.000001 game feet. All 61 bones and original
triangle counts are preserved for all three anatomies.

Native captures at frames 240 and 270 show animation changes. Early captures
can precede asynchronous loading; they are not accepted as a complete scene.
The 1410x1000 viewer window yields a 1000x1000 3D viewport because the SDK viewer
reserves sidebar space. Framing covers the sampled animation bounds, not only a
standing humanoid. Image differences alone do not establish correct animation;
the serialized skinning checks provide the geometric evidence.

No frame-time or performance conclusion is claimed. The preview lighting is
different from the live world, so these pictures are not a controlled old/new
renderer benchmark. The original character topology and artwork still limit
appearance even under a physically based renderer.

## Work required for a playable backend

The next implementation boundary is a native client viewport and live scene
adapter, not additional material tweaks to the old GLSL shaders. That work must:

- Submit actual game camera, entity transforms, dynamic geometry and bone updates
  to Filament each frame, instead of reloading glTF or playing exported clips.
- Cache geometry by DNA/component revision; retain live ordinary-clothing,
  palette-blend, alpha-cutout, emissive and carried-item behavior.
- Render terrain, structures, ships, plants, particles, atmosphere and distant
  bodies with camera-relative coordinates and origin rebasing.
- Compose the Qt UI, handle resizing/picking and manage swap-chain/resource
  lifetime. The MinGW Qt client and prebuilt Windows SDK also need a compatible
  native build boundary; the external viewer does not prove this integration.
- Replay the same world scene/camera/GPU in both backends and measure memory,
  frame times, streaming, shadows and ground-to-orbit precision before switching
  the playable default.

Primary implementation references: [Filament](https://github.com/google/filament),
[gltfio](https://google.github.io/filament/dup/gltfio.html),
[glTF 2.0](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html).
