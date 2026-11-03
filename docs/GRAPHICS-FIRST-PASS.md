# Graphics first pass

The game still uses its existing OpenGL renderer. This change enables and
exposes existing lighting/shadow features, improves mipmapped texture filtering,
and adds an isolated Filament evaluation scene. It is not an engine migration.

## Try the enhanced client

```powershell
.\AuSceneClient.exe -alocalhost -graphics-enhanced -log
```

This explicitly saves per-pixel lighting, sunlight/object shadows, 8x requested
anisotropy, 256-pixel procedural ground textures, 512-pixel plant textures and
2048-pixel skybox panes. Filtering is capped by the driver's supported maximum.
The CLI option preserves unrelated render preferences and view distances.

In Render settings, **Enhanced** enables the same lighting/filtering features
and higher texture detail with normal draw distances. **Default** now enables
lighting and shadows; **Max Graphics** does too. **Low Memory** disables those
features and requests standard filtering. Existing saved lighting preferences
are respected until a preset or explicit launch option is selected.

Texture filtering changes in settings require a client restart to update all
cached textures. Unsupported anisotropy extensions retain standard filtering;
non-mipmapped textures (including pixel-aligned UI textures) are unaffected.

Windows packaging now keeps MXE's software `opengl32.dll` and companion
`libgallium_wgl.dll` under `optional-mesa/`, allowing the system's GPU OpenGL
loader to be used by default. The bundled software loader failed to activate
a drawing surface on the development machine, leaving the entire Qt window
white; the same probe and game rendered after switching to the system loader.
The optional files are preserved for explicit compatibility experiments.

## Compare on the same scene

Use this updated executable for both runs:

```powershell
.\AuSceneClient.exe -alocalhost -graphics-classic -log
.\AuSceneClient.exe -alocalhost -graphics-enhanced -log
```

Close the first run before launching the second. Both options save the selected
settings. Classic turns off per-pixel lighting and shadows, requests standard
filtering and restores the prior 128/256/1024 ground/plant/sky texture sizes.
It is a controlled visual preset, not a rollback to the previous executable.

Keep camera, time of day, location, resolution and view distances identical.
Check a sunlit building edge, a road or deck viewed at a shallow angle, a lamp
near a flat wall, and terrain/trees. Check inside a ship as well as outdoors.
Record frame rate with the existing draw-count logging option. Lower shadow
distance or disable Object Shadows if their cost is too high on a given GPU.

## Validation

`RenderTextureProbe` is an explicit GPU-context smoke-test target, excluded from
normal builds. It creates actual game textures, checks filtering for mipmapped
and non-mipmapped textures at 1x/8x/16x, and verifies hardware clamping and GL
errors. Run it on a desktop with a working OpenGL context; a missing context
is a test failure, not a pass.

```bash
HAZERON_BUILD_CONFIG=debug bash docker/linux/build.sh RenderTextureProbe
./build/linux-debug/bin/RenderTextureProbe
MESA_EXTENSION_OVERRIDE='-GL_EXT_texture_filter_anisotropic -GL_ARB_texture_filter_anisotropic' \
    ./build/linux-debug/bin/RenderTextureProbe
```

The second command is a Mesa-specific unsupported-extension test. Other drivers
need a separate device/context without the extension to exercise that path.

## Next renderer decision

See [the isolated Filament evaluation](../experiments/filament/README.md).
Actual ship export, DNA skinning, terrain, camera-relative planetary precision,
Qt integration and GPU performance remain migration gates. Improve material
and texture art after establishing comparable captures; changing the graphics
API alone cannot supply those assets. Audit color-space handling before adding
HDR/exposure or tone mapping to the existing pipeline, including distinguishing
color textures from normal/data maps.
