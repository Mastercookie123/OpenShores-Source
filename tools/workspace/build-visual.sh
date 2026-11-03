#!/usr/bin/env bash
set -euo pipefail
workspace=/mnt/c/Users/Mason/Documents/ChatGPT/Hazeron
repo=/opt/hazeron/Source
files=(src/lib/global/core/aupose.cpp src/lib/math/aubodysurface.cpp src/lib/math/aubodysurface.h src/test/math/body_surface.cpp src/lib/data/designs/au3dsglsl.cpp src/lib/data/biology/dpbody.cpp src/lib/graphics/auglsl.cpp src/app/clients/scene/main.cpp src/test/CMakeLists.txt src/test/procedural_parts.cc src/test/procedural_avatar.cc src/lib/graphics/resources/shaders/H_CharacterSurface.glsl)
files+=(src/lib/data/biology/dpbody.h src/lib/data/biology/dpbodyfilament.cpp src/lib/data/biology/filamentbridge.h src/lib/data/designs/au3dsglsl.h src/renderer-filament/filament_api.h)
files+=(src/lib/graphics/auglsl.h src/lib/graphics/aufilamentworld.h src/lib/graphics/aufilamentworld.cpp src/lib/designer/core/audesignerview.cpp src/renderer-filament/world_api.h)
files+=(src/lib/atom/ui/audnaview.cpp src/lib/atom/ui/auitemview.cpp src/lib/atom/ui/aubodyrenderwidget.cpp src/lib/client/spacecraft/aucontrailsample.cpp src/lib/client/navigation/austarmapwidget.cpp)
files+=(src/lib/atom/entities/celestial/dasolarsystem.cpp)
files+=(src/lib/graphics/CMakeLists.txt)
for shader in "$workspace"/Source/src/lib/graphics/resources/shaders/Body*.f.glsl; do files+=("${shader#"$workspace/Source/"}"); done
for file in "${files[@]}"; do
 mkdir -p "$(dirname "$repo/$file")"
 if ! cmp -s "$workspace/Source/$file" "$repo/$file"; then cp "$workspace/Source/$file" "$repo/$file"; fi
done
mkdir -p "$repo/experiments/procedural-parts"
while IFS= read -r -d '' file; do
 relative="${file#"$workspace/Source/experiments/procedural-parts/pack/"}"
 target_file="$repo/experiments/procedural-parts/pack/$relative"
 mkdir -p "$(dirname "$target_file")"
 if ! cmp -s "$file" "$target_file"; then cp "$file" "$target_file"; fi
done < <(find "$workspace/Source/experiments/procedural-parts/pack" -type f -print0)
cp -r "$workspace/Source/tools/body_parts" "$repo/tools/"
cd "$repo"
export HAZERON_LINUX_SKIP_TOOLCHAIN_BUILD=1
case "${1:-tests}" in
 prepare) make shader-assets CONFIG=debug; cp src/lib/graphics/resources/glsl.har "$workspace/Source/src/lib/graphics/resources/glsl.har" ;;
 tests)
 HAZERON_BUILD_CONFIG=debug bash docker/linux/build.sh ValidateProceduralParts
 ./build/linux-debug/bin/ValidateProceduralParts "$repo/experiments/procedural-parts/pack"
 ;;
 windows) make win-client CONFIG=debug ;;
 linux) make linux-client CONFIG=debug ;;
 *) exit 2 ;;
esac
if [[ "${1:-tests}" == windows || "${1:-tests}" == linux ]]; then python3 tools/body_parts/package.py --client-dir "build/${1}-debug/bin"; fi
