#!/usr/bin/env bash
set -euo pipefail
root=/mnt/c/Users/Mason/Documents/ChatGPT/Hazeron/Tools/filament-build
cd "$root/downloads/sdk/Installers"
for file in 'Windows SDK for Windows Store Apps Headers OnecoreUap-x86_en-us.msi' 'Windows SDK OnecoreUap Headers x86-x86_en-us.msi' 'Windows SDK OnecoreUap Headers x64-x86_en-us.msi'; do
 msiextract -C "$root/windows-sdk" "$file" >/dev/null
done