#!/usr/bin/env bash
set -euo pipefail
sdk=$(realpath "${1:?Pass the extracted Filament SDK directory}")
output=$(realpath -m "${2:?Pass an output directory outside the source tree}")
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd "$here/../.." && pwd)
python3 "$here/build_scene.py" --repo "$repo" --output "$output"
for frames in 30 90; do
    "$sdk/bin/gltf_viewer" --api opengl --headless \
        --window-size 1440x900 --frames "$frames" --fixed-timestep 0.0166667 \
        --screenshot "$output/frame-$frames.tif" \
        --settings "$here/courtyard-settings.json" \
        --ibl "$sdk/bin/assets/ibl/lightroom_14b" "$output/courtyard.gltf" \
        > "$output/frame-$frames.log" 2>&1
done
python3 - "$output" <<'PY'
import json
import sys
from pathlib import Path
from PIL import Image, ImageChops
output = Path(sys.argv[1])
images = []
for frame in [30, 90]:
    image = Image.open(output / f'frame-{frame}.tif').convert('RGB')
    image.save(output / f'frame-{frame}.png')
    images.append(image)
changed = ImageChops.difference(*images).getbbox() is not None
scene = json.loads((output / 'courtyard.gltf').read_text())
report = {'renderer': 'Filament v1.79.0', 'asset': 'Original Asteroid1.3ds + synthetic deck and articulated actor',
          'frames': [30, 90], 'images_differ': changed, 'meshes': len(scene['meshes']),
          'nodes': len(scene['nodes']), 'animation_channels': len(scene['animations'][0]['channels']),
          'note': 'An image difference is a smoke test, not proof of correct animation or GPU performance.'}
(output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
PY
