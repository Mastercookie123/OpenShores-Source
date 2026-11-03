# Live Filament character renderer (Windows, experimental)

This is a playable-client integration, separate from `game_bridge.py` and the
offline glTF viewer. Launch workspace `Launch-Filament-Game.cmd`, or run
`powershell -NoProfile -ExecutionPolicy Bypass -File .\Hazeron.ps1 FilamentClient`.
The executable is `Client/AuSceneClient-filament-r2.exe`; the local account is
`local` / `local`. `Launch-OpenShores.cmd` retains the previous client.

## Implemented live path

`-renderer-filament` selects `OPENSHORES_RENDERER=filament`. All six DpBody skin
draw variants route to `dpbodyfilament.cpp`. The game still generates DNA parts,
poses and attachment anchors. Each draw submits its actual camera projection,
model-view matrix, all 61 current absolute bone matrices, selected skin palette,
clothing/helmet/armor masks and current light sources. Geometry and texture
uploads are cached per GL context, with model invalidation on DeleteGL/destruction.
Loader-thread invalidation queues GPU release for the render thread.

Qt is built with MinGW. `src/renderer-filament/backend.cpp` is built with MSVC
against the official Filament v1.79.0 static libraries. The R2 DLL is named
`OpenShoresFilament-r2.dll` and requires C ABI version 2. `filament_api.h` is a
versioned plain C boundary: no Qt, STL ownership, exceptions or C++ objects cross
toolchains. Both sides assert structure sizes. The DLL creates a native Vulkan
engine, skinned renderables, lit PBR materials and self-shadowing. Bone weights
are normalized to reproduce the legacy homogeneous divide, not retargeted to a
different skeleton. Legacy clockwise triangles become CCW. `flipUV:false` is
essential: the original UVs and GL texture rows are submitted unchanged.

Vulkan renders the conservative projected character bounds (at most 1024 pixels
per axis). The DLL returns synchronous, bottom-row-first premultiplied sRGB
RGBA8 and original OpenGL window-depth floats. A GL 3.2 compositor writes both
into the current game framebuffer at the original draw point. Existing world
depth can hide the body; later items and labels retain their draw ordering.
The compositor restores the GL state it changes. Fog is applied using the
game's existing exponential-squared depth equation. Planet-horizon attenuation
and ringworld shadow-panel modulation are approximated at the body pivot.

## Build and tests in this workspace

1. Official SDK: `Artifacts/renderer-sdk/filament-windows`. The verified release
   archive SHA-256 is
   `5fc8ea5a06bd3de3f43da9ec72a09458193db3e0b574e06b6b3f8ef94c21fce9`.
2. Portable MSVC 14.44.35207 and Windows SDK 10.0.26100 live under
   `Tools/filament-build`. The Microsoft payload manifests/checksums are in that
   directory. Both Desktop and Store CRT packages are required (OLDNAMES.lib).
   The SDK includes UCRT, Desktop and OnecoreUap shared headers/libraries.
3. Run `Source/tools/filament/build-windows.ps1 -Test` from PowerShell 7, or the
   workspace `Build-FilamentDll.ps1 -Test`. This builds the DLL, both `.filamat`
   packages and the native ABI test. It copies Filament's license into Client.
4. In Ubuntu run workspace `build-filament-live.sh`. It syncs edited files to
   `/opt/hazeron/Source`, uses the existing MXE Docker cache, builds the actual
   DpBody harness first and then the playable client, and copies both to Client.
5. Run workspace `Test-LiveFilament.ps1`. It starts a separate native Windows
   game harness process for every case. `-Only REGEX` selects cases; `-Legacy`
   selects the original GL draw path for comparison.
6. Run `python3 Source/experiments/filament/validate_live.py Artifacts/filament-live`
   in Ubuntu. Outputs: `validation.json` and `index.html` in that artifact folder.

Verified on NVIDIA RTX 5080 / Vulkan / driver 610.88: native color, transparent
background, asymmetric row orientation, perspective depth, changing bone
matrices, clothing replacement, target resize and mesh release/recreation.
The actual DpBody harness passes 23 fixtures: original 16 plus world-depth
occlusion, separate top/bottom palettes, armor/helmet, fog, headless view, and lamp-shadow on/off controls.
Five GL/Filament comparisons preserve DNA, triangle counts, part lists, camera
and both hand anchors exactly. Pixel differences demonstrate the new path
actually renders. These captures are lab fixtures; a logged-in gameplay check
is a separate verification step.

## Remaining migration work

* Terrain, plants, buildings, ships, held-item drawing and UI still use OpenGL.
  The alternate AuAnimalia model path remains OpenGL; DpBody procedural creatures
  are supported. This is a hybrid character renderer, not a full engine port.
* CPU readback, depth rendering and GL upload are synchronous per character.
  Representative second lab draws are about 6–10 ms on this GPU; first uploads
  can take hundreds of milliseconds. Crowds require batching and shared GPU
  image synchronization/external-memory interop before production use.
* Only the strongest directional star is used. Point lights use a reference-
  distance photometric mapping; spotlight cones are not yet transferred.
  Ambient is constant SH rather than the old hemispherical model.
* The new scene contains body parts, not world shadow occluders. It supports
  body self-shadows; it cannot yet receive building/terrain shadows in Filament.
  The existing GL world still renders its original shadow passes.
* Procedural fur shells, glow/fade effects, extra material-detail maps and full
  parity for every special render mode still need explicit adapters and tests.
* The original assets remain coarse. A physically based renderer fixes shading
  behavior; it does not supply new anatomy, clothing art or environment assets.

An explicitly requested backend failure is fatal with a diagnostic, rather than
silently drawing the original renderer and making a comparison misleading.
There are no server/database/network-protocol/DNA-save changes.

## R2 lamp-shadow correction

The launcher now selects the R2 executable. Filament mode enables two world lamp
shadow maps, which previously defaulted to Off. The native frame also passes the
lamp-shadow budget to Filament and enables point-light shadow casters. The native
receiver/blocker regression measures 70/255 with shadows off and 0/255 at the same
receiver sample with shadows on. Sun-only tests did not cover this missing path.

The client title identifies Filament R2 (lamp shadows). Its stdout/stderr are
captured in Client/filament-game-stdout.log and Client/filament-game-stderr.log.
After world maps become active, it logs LIVE_FILAMENT world lamp maps=N along
with the native shadow-lamp budget. World objects still are not occluders in
the native body scene; that remaining cross-renderer receiving limitation is
not fixed by enabling the two shadow systems.
