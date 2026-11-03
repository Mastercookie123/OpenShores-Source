#!/usr/bin/env bash
set -euo pipefail
root=/mnt/c/Users/Mason/Documents/ChatGPT/Hazeron/Tools/filament-build
mkdir -p "$root/windows-sdk"
cd "$root/downloads/sdk/Installers"
for file in 'Universal CRT Headers Libraries and Sources-x86_en-us.msi' 'Windows SDK Desktop Headers x64-x86_en-us.msi' 'Windows SDK Desktop Headers x86-x86_en-us.msi' 'Windows SDK Desktop Libs x64-x86_en-us.msi' 'Windows SDK for Windows Store Apps Headers-x86_en-us.msi' 'Windows SDK for Windows Store Apps Libs-x86_en-us.msi'; do
 msiextract -C "$root/windows-sdk" "$file" >/dev/null
done