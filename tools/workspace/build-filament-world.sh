#!/usr/bin/env bash
set -euo pipefail
workspace=/mnt/c/Users/Mason/Documents/ChatGPT/Hazeron
repo=/opt/hazeron/Source
bash "$workspace/build-visual.sh" prepare
cd "$repo"
repo_mount=$(bash docker/support/resolve-project-mount.sh "$PWD")
docker run --rm --mount type=volume,source=hazeron-windows-debug-cache,target=/build --mount "type=bind,source=$repo_mount,target=/src,readonly" --mount "type=bind,source=$repo_mount/build/windows-debug,target=/output" hazeron-mingw-qt5 bash -ec '"${MXE_TARGET}-cmake" -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DAU_OUTPUT_ROOT=/output; cmake --build /build --target RenderProceduralAvatar --parallel 8'
cp build/windows-debug/bin/RenderProceduralAvatar.exe "$workspace/Client/RenderFilamentWorldTest.exe"
if [[ "${1:-all}" == tests ]]; then exit 0; fi
docker run --rm --mount type=volume,source=hazeron-windows-debug-cache,target=/build --mount "type=bind,source=$repo_mount,target=/src,readonly" --mount "type=bind,source=$repo_mount/build/windows-debug,target=/output" hazeron-mingw-qt5 bash -ec 'cmake --build /build --target AuSceneClient --parallel 8'
cp build/windows-debug/bin/AuSceneClient.exe "$workspace/Client/AuSceneClient-filament-world-r8-opt.exe"
