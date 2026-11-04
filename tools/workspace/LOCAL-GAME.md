# Local community fork

Current character path: `Hazeron.ps1 Client` or `ProceduralClient` uses the
original DNA-generated assembly and animation with the optional compatible
limb pack. `OriginalProceduralClient` uses the same build with original assets.
The fixed-human experiment is retired; `HumanClient` now aliases the procedural
client. See `Source/experiments/procedural-parts/README.md` for asset authoring.

Compatibility gate: 80 DNA/sex variants, four poses each, zero grip/camera/seam
anchor difference and zero bone-matrix difference. The initial pack covers 16
arm components, not a complete new character-art set. Both clients compile.
Live check: the user confirmed that the hammer stays attached while moving and
attacking in the procedural client on November 1, 2026.

Active repository: `/opt/hazeron/Source` in Ubuntu (WSL 2).

Windows reference checkout: `C:\Users\Mason\Documents\ChatGPT\Hazeron\Source`.
Use the Ubuntu checkout for builds and edits so the two copies do not diverge.
Both have origin set to Mastercookie123/OpenShores-Source, upstream set to
Celarious/OpenShores-Source, and original set to Hazeron-Freehold-LLC/Shores-of-Hazeron.

## Run it from PowerShell

From this workspace:

```powershell
.\Hazeron.ps1 Start
.\Hazeron.ps1 Status
.\Hazeron.ps1 LinuxClient
.\Hazeron.ps1 Logs
.\Hazeron.ps1 Stop
```

`Start` builds changes as needed and starts the local stack. `Stop` preserves
the database and reports. `Logs` follows logs until Ctrl+C.

Web status: http://localhost/

Development account: `local`, password `local`.

This is a localhost development deployment with intentionally known test
credentials. Configure a separate production environment before remote hosting.
Do not run `make server-reset` on a universe you want to retain.

## Build from Ubuntu

```bash
cd /opt/hazeron/Source
make server-up
make linux-client CONFIG=debug
ALSOFT_DRIVERS=pulse ./build/linux-debug/bin/AuSceneClient -alocalhost -log
make win-client CONFIG=debug
```

Ubuntu's initial setup was completed for build access as root; the PowerShell
helper explicitly uses root. Set up a regular Linux development user before
multi-user development. Docker Desktop's Ubuntu integration must remain enabled.

The Windows compiler image is `hazeron-mingw-qt5`. Its initial MXE/Qt toolchain
build is substantial. The Linux compiler image is `hazeron-linux-qt5`.
Compiler caches live in Docker volumes and should be retained between builds.

## Verified on November 1, 2026

- All 11 local services started; game/database/web health checks passed.
- Windows and Ubuntu both received HTTP 200 from the local web service.
- The status CGI reported all expected game roles and one scene worker.
- Full container removal/recreation without volume deletion retained the
  `local` account and the two initial universe atom records.
- Existing RunTests suite passed: one test case, one assertion. This does not
  establish gameplay or protocol correctness.
- Linux client built and launched through WSL GUI support.
- The server subsequently reported one avatar online from the local client.
  The player confirmed successful world entry, movement, attack and held-item
  switching, covering the first interactive gameplay/UDP smoke test.
- Native Windows client build remains pending its initial toolchain build.

A one-time background job (`Finish-WindowsClient.ps1`) is waiting for the
active Windows toolchain process, then will build the native client and copy
its complete runtime into the workspace `Client` folder. Read
`windows-client-status.txt` for its state and `windows-client-build.log` for
client build output. Avoid restarting Windows or Docker until this job finishes.
Once the status says READY, use `.\Hazeron.ps1 Client`. Native Windows gameplay
has not been tested yet. The Linux client remains available in the meantime.

The initial bootstrap added an LF line-ending rule for all shell scripts in
`.gitattributes`, needed for Linux execution from Windows. Later graphics
changes are described below.

## Graphics first pass — November 1, 2026

The enhanced native Windows client is now installed in `Client` and has been
observed rendering the live world. It uses the NVIDIA GeForce RTX 5080 OpenGL
driver. A bundled Mesa `opengl32.dll` caused a white startup window; it and
`libgallium_wgl.dll` are preserved under `Client/optional-mesa`, and the build
script now packages them there by default.

`Hazeron.ps1 Client` selects enhanced lighting/shadows, texture detail and 8x
requested anisotropic filtering. `Hazeron.ps1 ClassicClient` selects the classic
lighting/filtering preset for comparison. Close one client before comparing
the other at the same location/time/camera. The preserved previous executable
is `Client/AuSceneClient-baseline.exe`.

See `Source/docs/GRAPHICS-FIRST-PASS.md` for details. The isolated Filament
evaluation is under `Source/experiments/filament`; actual renders and its report
are in `Artifacts/filament/final`. This is a renderer evaluation, not an in-game
Filament integration. Planet-scale precision and actual ship/DNA export remain
future migration gates.

Validation: both clients compiled; 6 texture cases passed on NVIDIA OpenGL;
6 passed on Mesa with anisotropy available and 6 with its extensions disabled;
Classic/Enhanced persisted settings matched their profiles; the existing
one-case test suite passed. Native daylight image-quality/performance comparison
has not been measured yet. Changes remain local and uncommitted.

Build and verification logs are in the workspace beside this document.

## Character comparison — November 1, 2026

Run `.\Hazeron.ps1 CharacterClient` for the opt-in body mesh smoothing and
skin/hair shader pass. Run `.\Hazeron.ps1 ClassicCharacterClient` for the same
build without that pass. Close one before signing into the same avatar in the
other. Both use the enhanced general graphics settings.

The comparison executable is `Client/AuSceneClient-character.exe`; the regular
client is preserved. See `Source/docs/CHARACTER-PASS.md` for scope and limits.
Both Windows and Linux clients built successfully; 5 test cases with 793
assertions passed, and 48 body/hair shader variants compiled. The actual skull
and arm assets also rendered through the body loader/refinement probe.
Full animated-avatar appearance, seams and performance still need in-game
verification. This improves existing geometry; new anatomy and hair artwork
remain necessary for a larger visual overhaul. Changes are local and uncommitted.

## Replacement human prototype — retired experiment

The fixed human mesh, skin/eyes, authored hair, overalls and shoes remain an
offline art reference. They are no longer selected by the live client launcher.
That experiment did not preserve procedural generation and had arm/grip issues.

The asset pipeline, editable Blender file, source revisions, CC0 notices and
limits are documented in `Source/experiments/human-replacement/README.md`.
The current asset has 32,628 triangles and a 2048px color/cutout atlas. It uses
the game's existing animation bones through a new inverse-bind bridge.

The production loader has rendered standing, walking, crouching and right-arm
attack poses in the offline OpenGL probe. Six malformed mesh cases are rejected.
These renders are not a live-world acceptance test. Check held items, first
person, lighting and motion on the actual avatar before making it the default.
This prototype has fixed male proportions, coloring, hair and clothing; it does
not yet preserve all DNA customization or ordinary outfit changes.

## Current procedural visual pass (November 1, 2026)

The fixed whole-human prototype above has been retired from the production path.
Players and creatures use original DNA, bones, clothing and attachment points.
The user confirmed held-tool motion works in the restored 16-arm procedural client.

`./Hazeron.ps1 VisualClient` opens the separate visual comparison build;
`OriginalVisualClient` opens the same executable with original assets.
The visual pack has 299 compatible modular parts plus skin microdetail/material
adjustments. Faces retain their original components after experimental remeshing
failed visual inspection. This is an incremental character/creature pass, not a
complete environment or modern character-art replacement. Existing `Client` and
`ProceduralClient` commands retain the previously working package.

See `Source/experiments/procedural-parts/README.md` for verification and authoring,
and `Source/experiments/visual-overhaul/README.md` for real renderer comparisons,
remaining art work and generated-material provenance.

## Modular human and shading revision

`./Hazeron.ps1 ModularClient` launches the reworked human component set with
post-DNA normal correction and reduced skin normal-detail amplitude.
`OriginalModularClient` compares original assets in the same new binary.
The separate pack contains 373 components, including actual default human hands
and feet. See `Source/experiments/procedural-parts/MODULAR-HUMAN.md` for scope,
verification and source editing. Original palette/hair artwork remains in use;
this is a geometry/shading revision, not a finished replacement art library.

## Repeatable native visual test lab

Run `./Test-Visuals.ps1 -OpenReport` (add `-Build` after code/shader changes).
It captures 13 fixtures on the real Windows GPU using isolated original/candidate
processes, and writes `Artifacts/visual-tests/latest/index.html` with side-by-side,
wipe/difference views and verified component/material hashes. `-SelfCheck` proves
missing assets and wrong checksums are rejected. See
`Source/tools/visual_test/README.md` for scope and limitations.

The first native run detected about 0.0012 units of grip drift. The previous
shared-process comparison masked it through cached hand/foot measurements.
Current candidate acceptance remains failed; the test threshold was not relaxed.
This lab covers production character rendering and frozen poses. It does not
reproduce the live planet or render a held hammer mesh.

### Human face/measurement revision

The modular pack now contains 385 components and includes the actual default
human nose type 2, mouth type 6 and eyes type 2. The previously edited nose/mouth
families did not cover those selected by default. Upper-arm bulges are reduced
and jaw width increased. Hand/foot bounding extrema now remain exact, fixing the
native grip regression. Native 13-case comparison passes with zero grip/camera
deltas, including repeatable walking/attack samples using the lab-only clock.
Original palette/hair art remains a visual limitation. Assets are installed for
`ModularClient`; an already running client needs restarting to load them.

Review `Artifacts/visual-tests/face-pass/index.html`; earlier candidate captures
are retained under `Artifacts/visual-tests/pre-face-pass`.

### Campfire and silhouette revision

Double-click `Launch-OpenShores.cmd` to launch the current character build without
manually running a PowerShell script. `Hazeron.ps1 ModularClient` now selects
`Client/AuSceneClient-modular-r4.exe` with the revised 385-component pack.

Human arms/legs use continuous anatomical radial profiles. Local skin lighting
includes a small reflected-light approximation near fires/lamps, with no emissive
floor. The native suite now has 16 fixtures including a backlit campfire target
and a fully unlit no-glow control. See
`Source/experiments/procedural-parts/CAMPFIRE-PASS.md` for scope and rollback paths.

## Filament / Vulkan renderer prototype

`Try-Filament.cmd` opens a separate, interactive renderer preview using actual
DNA-generated game meshes and their original bone transforms. It is hosted by
the official Filament v1.79.0 Windows SDK viewer on the RTX 5080/Vulkan backend.
It is not yet embedded in the playable Qt game or connected to the server.

`Filament-Preview.ps1 -Variant human`, `-Variant multi`, and `-Variant quadruped`
select the generated anatomy. The original game's sampled walking poses drive
61 joint nodes; no fixed replacement human rig is substituted. Three serialized
buffer/pose validations total 9,581 checks with maximum position error below
0.000001 game feet. Native render logs confirm the NVIDIA device.

Review `Artifacts/filament-character/index.html` for captures and validation.
See `Source/experiments/filament/GAME-BRIDGE.md` for rebuild steps and the live
client integration work still required. The renderer preview currently exports
skin only and rejects different top/bottom palette offsets; clothing, held
items, full world rendering and Qt composition remain migration work.
`Launch-OpenShores.cmd` continues to launch the playable OpenGL client.

## Automated client verification

`Test-Client.cmd` runs the real client in an isolated single-player test world,
drives movement/attack/equipment controls, captures screenshots and timing data,
and exits with a pass/fail result. It requires no login or desktop interaction.
Use `Test-Client.cmd -Build -Fixtures` to rebuild and include the visual suites.
Results and crash logs are under `Artifacts/client-smoke`; `latest.json` points
to the latest report. See `Source/tools/visual_test/CLIENT.md` for details.

The new command interface is available with `Launch-Modern-UI.cmd` (or the
existing `Launch-Filament-World.cmd`). `Launch-Classic-UI.cmd` selects the old
interface in the same build. Press Ctrl+K to find panels, and use the rail's
collapse button to reclaim space. Inventory now has a name filter.
