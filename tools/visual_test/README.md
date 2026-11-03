# Native visual test lab

From the existing Windows workspace:

```powershell
.\Test-Visuals.ps1 -OpenReport
# Rebuild the instrumented renderer after production code/shader changes:
.\Test-Visuals.ps1 -Build -OpenReport
# Fast targeted reproduction:
.\Test-Visuals.ps1 -Only human-studio,human-orange-sun,human-strike -OpenReport
# Also prove empty packs and bad checksums are rejected:
.\Test-Visuals.ps1 -SelfCheck -OpenReport
```

Default backend is native Windows OpenGL on the installed GPU, invoked through
WSL interop. It is not Wine or Ubuntu software rendering. `-Backend linux` is a
separate cross-platform check. Prerequisites are this workspace's Ubuntu/Docker
toolchains, Python/Pillow in Ubuntu, native Qt runtime in Client, and the installed
body-parts-modular pack. The local build script synchronizes changed source to the
active /opt/hazeron/Source checkout and uses the existing debug build caches.
No login, server reset, database write or separate test account is required.

## What runs

Sixteen versioned fixtures in cases.json cover studio lighting, real cascaded
sun shadows, warm daylight, night, side/back cameras, female clothing, walking,
crouching, right-arm strike/block, long limbs, multiple limbs and a quadruped.
The renderer uses actual DpBody mesh assembly, DNA scaling, skinning, texture
loading, production depth passes and production body/material shaders. The instrumented renderer fixes animation time at 0.25 seconds by default.
Walking and strike are repeatable pose samples, not continuous animation playback.
Each capture includes portrait and full-body PNGs, a process log and render.json.

Original and candidate run in **separate processes** with fresh model caches.
The first candidate and walking/strike cases are repeated to verify deterministic pixels. The HTML viewer
provides side-by-side views, a wipe slider and a difference overlay. It links to
the actual component/material audit and records pixel differences, triangle counts
and camera/hand grip deltas. Images are measured without modifying source art.

## Gates and audit

- GPU vendor, renderer and driver version; the Windows lane rejects known software GL.
- SHA-256 of executable, installed manifest and embedded glsl.har.
- Hash/path of every accepted component replacement, with rejection events.
- Actual material shader program, uniform location and bound detail texture.
- Linked body shader, enabled shadow maps and OpenGL errors in both depth/color passes.
- Nonblank framebuffers and a detectable A/B image change.
- Maximum hand/camera difference of 0.000001; mismatches remain failures.
- Repeat capture pixel mean absolute error at most 0.01 on the 0..255 scale.

The suite continues after a failed case and returns a nonzero exit code while
retaining its images and diagnostics. summary.json and index.html are written
even on failure. A failed candidate is not a broken test environment.

The first native run found approximately 0.0012 units of hand-grip drift in the
previous candidate. The face/measurement revision fixes it by preserving hand and foot extents. The old compatibility test shared global hand/foot measurement
caches between A/B assemblies and masked this difference. This test deliberately
does not relax the threshold or mark those cases accepted.

## Limits

Mechanical pass means rendering/loading contracts passed, **not** that artwork
looks good. Review the native images before declaring a visual improvement. The
original-resource baseline uses the same current executable; it is not an approved
historical screenshot set. Retain a dated report directory when accepting a build.

This is a controlled character renderer lab, not a live-world replay. The warm
and night fixtures approximate problem lighting; they do not reproduce the exact
planet, sky, terrain, flora, fog or exposure in a live screenshot. There is no held
hammer mesh in these fixtures: grip deltas are checked, but tool contact and full
animation playback still need a later integration fixture or live-world check.
Crowd frame times, networking and server simulation are not covered.

Adding a fixture is a cases.json edit. Keep fixture settings fixed between A/B;
do not update thresholds or reference images merely to conceal a regression.

The campfire-backlit fixture uses a warm point light behind the character, no
front fill, and a dim night ambient. It requires at least four luminance levels
of improvement at the fixed chest sample. The unlit-black control requires that
same skin sample to remain black when both local and ambient lights are off.
These are controlled character fixtures, not an exact replay of the live campfire.
