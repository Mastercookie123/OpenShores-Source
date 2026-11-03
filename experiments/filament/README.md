**New:** [actual game-character Filament/Vulkan bridge](GAME-BRIDGE.md) now renders DNA-generated, animated bodies on native Windows. The following documents the earlier synthetic scene.

# Filament renderer evaluation

This is an isolated asset/material/animation proof of concept using the official
**Filament v1.79.0** Linux SDK. It does not replace the game renderer or connect
to a server. The normal game build does not require Filament.

The scene imports the original `Asteroid1.3ds` geometry without modifying the
source asset. It adds a synthetic deck, bulkheads, beams, emissive panels and
an articulated box actor with two animated shoulders. Materials use roughness
and metallic values; the viewer supplies environment lighting, sunlight,
shadows, ambient occlusion and tone mapping. These are test materials, not
finished game art. The original game's license and attribution still apply
to its asteroid model.

## Reproduce

On Ubuntu:

```bash
sudo apt install assimp-utils python3-pil
curl -fL https://github.com/google/filament/releases/download/v1.79.0/filament-v1.79.0-linux.tgz -o filament-sdk.tgz
mkdir -p filament-sdk
tar -xzf filament-sdk.tgz -C filament-sdk
bash experiments/filament/render.sh "$PWD/filament-sdk/filament" /tmp/filament-evaluation
```

The output directory contains the portable glTF scene, screenshots at fixed
frame counts, renderer logs and a small report. The official SDK's screenshot
output is TIFF regardless of the requested extension; the script converts it
to PNG for inspection. See the renderer log for the selected device. WSL's
headless OpenGL selected **llvmpipe software rendering** in our initial test;
do not treat those timings as GPU performance measurements. The SDK also
logged a handle-arena warning and continued using heap allocation.

## What this establishes

- A real game mesh can pass through Assimp into Filament.
- Procedural meshes and PBR materials can be rendered in an isolated scene.
- The glTF scene includes hierarchical articulation and animation channels.
- Deterministic screenshot capture is available for future regression checks.

## Gates before choosing a replacement

1. Export one actual player-designed spacecraft, including surface palettes,
   windows, transparency and emissive effects. Preserve its coordinate/unit
   conventions. The legacy rocket and Neptune assets tested failed standard
   Assimp import; diagnose their chunk layout or export from the game's loader
   rather than modifying the originals to satisfy a converter.
2. Export terrain and an actual DNA-generated, skinned creature. The simple
   actor here does not validate Hazeron's skinning or procedural DNA pipeline.
3. Implement and test camera-relative coordinates and origin rebasing during
   a ground-to-orbit flight, including depth precision and distant bodies.
4. Host Filament in a small native C++ client viewport and verify Qt UI
   composition, resize, picking and resource lifetime. The SDK viewer alone
   does not validate integration with the existing Qt renderer.
5. Compare the *same exported scene*, camera, resolution, GPU and quality goals
   with the current renderer. Record frame times, memory, streaming cost and
   image defects on Windows and Linux. Do not compare this synthetic scene's
   appearance or speed with an unrelated in-game scene.

Retain the playable OpenGL path until those gates justify a migration.

Primary sources: [Filament](https://github.com/google/filament),
[pinned SDK release](https://github.com/google/filament/releases/tag/v1.79.0).
