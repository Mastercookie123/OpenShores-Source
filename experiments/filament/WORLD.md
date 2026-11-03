# Full-scene Filament renderer (R7)

This experimental Windows client renders the world's actual draw submissions
through one native Filament Vulkan scene. It supersedes the character-only R2
path when launched with `-renderer-filament-world`. The modular procedural body
system, DNA, skeletons, animation and equipment anchors remain authoritative.

## Launch

From this workspace, double-click `Launch-Filament-World.cmd`, or run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Hazeron.ps1 FilamentWorldClient
```

The title reads **Filament R7 Release**. The local server is unchanged;
use the existing local account. `Launch-Filament-Game.cmd` retains R2 as rollback.

## Coverage

The common `AuOpenGLFunctions` draw boundary captures arrays, indexed geometry,
instanced draws, triangle strips/fans, lines and point sprites. It reads the
existing geometry, material uniforms, texture palettes and animation matrices.
The main world, designer, DNA/body/item previews, contrail preview and 3D star
map opt into this boundary. Qt windows and widgets retain their existing UI.
Legacy offscreen asset generation still executes in GL; duplicate legacy
world shadow-map passes are disabled in full-scene Filament mode.
visible scene geometry is submitted to Filament, with no entity whitelist.
Unsupported visible shader families abort with a named coverage error.

The generated catalog contains 115 original vertex/fragment pairs, each with
opaque, transparent, additive and depth variants (460 compiled materials).
Original vertex transformations, UVs, terrain blends, clothing palettes,
alpha tests, instanced plant placement and wind are retained. Filament supplies
PBR lighting and shared directional/point-light shadows. Fog is applied after
lighting; emissive effects and particle quads are supported.

The native C API avoids the MinGW Qt / MSVC Filament C++ ABI mismatch.
Color is read back once per completed view and composited into the game's
existing framebuffer. Filament retains its own depth buffer for occlusion.
Exporting depth back to GL is optional (`OPENSHORES_FILAMENT_EXPORT_DEPTH=1`)
and enabled in depth-validation fixtures. Live completed views have no later
GL depth consumer, so they skip the extra depth materials, pass and readback.
Sub-viewports and depth clears inside the Filament scene are retained.

## Validation

`Test-FilamentWorld.ps1 -Release` exercises 25 deterministic fixtures using production
DpBody generation and the production draw bridge. It checks renderer activation,
OpenGL errors and preservation of caller GL state. The mixed-world fixture
adds original terrain, structure, instanced flora, item, rock, water, cube-map
sky and particle shader programs to the same scene. Inspect the generated
images as well as the assertions: these checks do not establish visual parity
for every possible world or material.

The NPC globe/fog regression also asserts visible skin pixels in both camera
views. It previously submitted all eleven body parts but rendered no character:
Filament's masked material added an alpha rejection after the legacy shader's
own cutout. Opaque GL draws ignore output alpha, so opaque/depth adapters now
force alpha to one after executing the source shader's explicit discards.
The repaired case measures 68,655 body-view and 73,915 portrait-view skin pixels.
The two-body case verifies that skeleton uniforms are fetched once per body
and reused for its mesh parts, then invalidated for the next character.
Run `-Release -ColorOnly` to exercise the live export mode. Both modes pass
all 25 cases. These mode-specific reports are under `tests-release` and
`tests-release-color-only` in the artifact directory.

`FilamentWorldRuntimeTest.exe` verifies native cross-object point shadows and
depth numerically: the receiver sample changes from 70 to 0 with shadows,
and depth is approximately 0.888888836 (expected 8/9).
It additionally exercises an inactive textured material across 130 frames and
reactivates it. Texture eviction must account for all cached material bindings,
including inactive instances; age alone is insufficient.
It compares color-only and depth-export results (maximum channel difference
2/255 in the checked scene), and tests lazy depth-material creation when
switching back to depth export.

The first live campfire-world audit captured roughly 2,200 submissions across
18 families, with zero unsupported submissions. The updated client captured
20 families over approximately 2,200 submissions, also with zero unsupported
submissions. Ground, trees, tents, rocks,
equipment, characters, water, clouds, atmosphere, particles and HUD draws were
observed. Ground shadows were inspected in the live client. This does not
constitute exhaustive validation of spacecraft interiors, every star-map mode,
every creature or every procedural world.

Coverage and timing: `Client/filament-world-status.json` and
`Client/filament-world-game-stderr.log`. Fixtures: `Artifacts/filament-world/tests`.

## Build

1. Generate materials with Ubuntu Python and cpp:
   `python3 Source/tools/filament/convert_world_materials.py --output Artifacts/filament-world/materials`
2. Run `Compile-FilamentWorld.ps1` using the pinned official Filament 1.79 SDK.
3. Copy the manifest and `.filamat` files to `Client/filament-world-r8`.
4. Run `Build-FilamentDll.ps1 -Source world_backend.cpp -Output OpenShoresFilamentWorld-r8.dll`.
5. Run `build-filament-world.sh` in Ubuntu/WSL. `tests` builds just the harness.
6. For the playable optimized package, run `build-filament-release.sh` in
   Ubuntu/WSL. Its isolated Release cache also builds the Release test harness.
   The launcher selects the most recently packaged Release or debug executable.

The shared SDK/compiler setup and licensing are described in `LIVE.md`.

## Current limits and next work

This is an integration prototype, not the default production renderer.
R3 debug views took approximately 130–150 ms rendering time at 2296×1369.
The R4 Release package reduced this to approximately 65–80 ms in the same
campfire area. R5 removed unused depth export and repeated skeleton reads;
R6 shared parameter data. The final R7 sample measured 26.7 actual frames/sec
and 33.8 ms mean rendering time at 2296×1369 over roughly 2,176 submissions.
Scene capture averaged 12.2 ms and the native backend 20.0 ms. NPCs were
visually checked in the live R7 client. The camera/population varied between
revisions, so these are live-area samples rather than identical replay benchmarks.
The renderer is still below 60 FPS; native scene submission/batching and the
CPU readback bridge remain further optimization work.
During the larger startup scene (about 4,000 submissions), Filament logged
that its driver handle arena was full and fell back to the slower system heap.
The client continued rendering. This is a remaining resource-budget issue;
the SDK exposes `Engine::Config::driverHandleArenaSizeMB` for a future package.
Native instances still
expand into separate submissions; a GPU instance-buffer path and GPU texture
sharing should replace this bridge's CPU capture/readback for production use.
The follow-up implementation reuses instances by geometry occurrence, caches
unchanged material parameters, takes common GL state once per instanced draw,
culls reliably bounded geometry, and updates scene membership incrementally.
R7 additionally uses numeric geometry/texture cache keys, precomputes uniform
locations and parameter descriptors, reads texture dimensions only on cache
misses, and retains parameter-block allocations between frames. Immutable
parameter blocks and instance-buffer reads are shared within each source draw.
Body skeleton arrays are read once per body, with explicit invalidation between
different procedural bodies. The parameter pool trims unused peak capacity.
It switches materials once per color/depth pass instead of once per depth-clear
group. Legacy GL sun/lamp shadow passes are disabled only for the full-scene
Filament path, which renders its own shadows. Stage timings (`captureMs` and
`nativeMs`) separate scene collection from the native backend. Before removing
duplicate GL shadows, a stationary live sample measured 93 ms capture, 32 ms
native, and 128 ms total at 2,198 submissions. Final live measurements should
be taken in the same scene after restarting the updated package.
Geometry and image caches expire unused resources to bound travel-related
growth. Depth-clear layers currently support eight groups per view.

The workspace CPU sampler (`Tools/profile-game.cpp`) samples only the chosen
game process. `Tools/profile-map.py` maps its instruction pointers using the
exact executable's COFF symbols. Stripped DLL export boundaries are approximate;
waiting-thread module totals are not CPU percentages. Keep symbol files paired
with the executable they came from when comparing profiles.

Lighting is an approximation of the legacy lighting model: one strongest sun
plus point lights, two shadowed point lights by default. Legacy spot cones,
planet-horizon attenuation and ringworld shadow panels are not fully replicated.
Water uses simplified PBR lighting with original wave/UV motion; its old
tangent-space lighting is replaced. Sampler filtering is currently standardized
to repeating trilinear filtering rather than reproducing every GL sampler.
Atmospheric colors, transparent ordering, first-person/portrait composition
and uncommon render modes still need broader visual review. Original mesh and
texture quality is unchanged by switching the renderer.

## R8 preview lighting and surfaces

R8 keeps the procedural mesh, original skeleton, clothing masks and item anchors.
It uses the same lightroom_14b irradiance and prefiltered reflection cubemap as
the SDK preview, with F16 / 1/125 s / ISO100 exposure, FXAA and half-resolution
SSAO on the main depth-tested scene. Environment assets live in
`Client/filament-environment`; materials and ABI2 DLL are isolated from R7.
The driver handle arena is now 16 MB instead of the previous default budget.

The legacy eye-space vertex adapters are rotated into a stable Y-up environment
frame, with the inverse camera rotation preserving original clip positions.
Normals, lights and bounds use the same basis. This avoids camera-dependent
probe highlights without changing gameplay coordinates or procedural animation.

Skin roughness is .66, cloth .86, fur .90, scales .58, exoskeleton .48, terrain
.96, rock .92 and water .18. Model shininess maps to bounded roughness. Power
armor clothing masks receive a metallic profile; ordinary skin remains dielectric.
Point-light power is reduced by eight to prevent campfires washing out detail.
The live probe follows the existing ambient light level and gently retains its
color, so night still differs from the studio and zero ambient is truly dark.
This is a fixed environment approximation, not a dynamically captured world probe.

`Test-FilamentWorld.ps1 -Release` and `-ColorOnly` now include a matched
preview-reference scene with the exact DNA, pose, camera, floor and environment.
The SDK viewer overrides its JSON sun using spherical-harmonic direction/color
estimates after loading the IBL; the studio fixture reproduces that behavior.
The live game continues to use the world's actual sun and lamps.
The fixture asserts visible skin pixels, and `tools/visual_test/validate_filament_preview.py`
compares against the original preview. The first matched image scored 0.980
skin-silhouette IoU and 6.06/255 mean RGB error over the shared skin interior.
These metrics cover the fixed comparison scene, not every live lighting condition.


R8 live verification: NPCs were visible in the campfire area, with no unsupported
draws or driver-arena fallback logged. Eight warmed samples at 2560x1369 and
about 2,294 submissions averaged 32 ms rendering (11.375 capture, 19 native),
with 27.97 observed frames/sec. This is still below 60 FPS and is not an
identical replay of the R7 camera/population. All 26 fixture cases passed both
color-only and depth-export paths (52 checks); the native shadow, depth parity
and 130-frame inactive-resource checks also passed.
