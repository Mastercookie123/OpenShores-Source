#!/usr/bin/env bash
set -euo pipefail
workspace=/mnt/c/Users/Mason/Documents/ChatGPT/Hazeron
repo=/opt/hazeron/Source
cd "$repo"
mkdir -p build/windows-release
repo_mount=$(bash docker/support/resolve-project-mount.sh "$PWD")
docker run --rm --mount type=volume,source=hazeron-windows-release-cache,target=/build --mount "type=bind,source=$repo_mount,target=/src,readonly" --mount "type=bind,source=$repo_mount/build/windows-release,target=/output" hazeron-mingw-qt5 bash -ec '"${MXE_TARGET}-cmake" -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DAU_OUTPUT_ROOT=/output; cmake --build /build --target AuSceneClient RenderProceduralAvatar --parallel 8'
if [[ "${1:-all}" != tests ]]; then cp build/windows-release/bin/AuSceneClient.exe "$workspace/Client/AuSceneClient-filament-world-r8-release.exe"; fi
cp build/windows-release/bin/RenderProceduralAvatar.exe "$workspace/Client/RenderFilamentWorldReleaseTest.exe"
