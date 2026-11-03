#!/usr/bin/env python3
"""Compile changed production shader variants after resolving engine includes."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2] / 'src/lib/graphics/resources/shaders'
shaders = [p for p in root.glob('*.f.glsl') if '#CharacterSurface' in p.read_text()]
assert len(shaders) == 12, len(shaders)
checks = 0
with tempfile.TemporaryDirectory(prefix='character-shaders-') as temporary:
    for shader in shaders:
        source = shader.read_text()
        for marker, header in [('#SunLightGlobe', 'H_SunLightGlobe.glsl'),
                               ('#SunLightRingworld', 'H_SunLightRingworld.glsl'),
                               ('#SunLight', 'H_SunLight.glsl'),
                               ('#CharacterSurface', 'H_CharacterSurface.glsl')]:
            source = source.replace(marker, (root / header).read_text())
        source = source.replace('MAX_LIGHTS', '8').replace('MAX_ANIMAL_BONES', '256')
        for detailed in [False, True]:
            for shadows in [False, True]:
                defines = '#define PER_PIXEL_LIGHTING\n'
                if detailed: defines += '#define CHARACTER_SURFACE_ENABLED\n'
                if shadows: defines += '#define SHADOWS\n#define SHADOW_MAPS\n#define SHADOW_MAP_PCF 1\n#define LIGHT_SHADOW_MAPS\n'
                version, rest = source.split('#version 150', 1)
                output = Path(temporary) / shader.name
                output.write_text(version + '#version 150\n' + defines + rest)
                result = subprocess.run(['glslangValidator', '-S', 'frag', str(output)], capture_output=True, text=True)
                if result.returncode:
                    raise SystemExit(f'{shader.name}, detail={detailed}, shadows={shadows}\n{result.stdout}{result.stderr}')
                checks += 1
print(f'PASS: {checks} production body/hair shader variants compiled')
