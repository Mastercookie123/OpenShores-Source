param([string]$Source='backend.cpp',[string]$Output='OpenShoresFilament-r2.dll',[switch]$Test)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$vc=Join-Path $root 'Tools/filament-build/msvc/Contents/VC/Tools/MSVC/14.44.35207'
$sdk=Join-Path $root 'Tools/filament-build/windows-sdk/Program Files/Windows Kits/10'
$filament=Join-Path $root 'Artifacts/renderer-sdk/filament-windows'
$env:PATH="$vc/bin/Hostx64/x64;"+$env:PATH
$env:INCLUDE="$vc/include;$sdk/Include/10.0.26100.0/ucrt;$sdk/Include/10.0.26100.0/shared;$sdk/Include/10.0.26100.0/um;$sdk/Include/10.0.26100.0/winrt;$filament/include"
$env:LIB="$vc/lib/x64;$sdk/Lib/10.0.26100.0/um/x64;$sdk/Lib/10.0.26100.0/ucrt/x64;$filament/lib/x86_64/mt"
$libs=Get-ChildItem "$filament/lib/x86_64/mt" -Filter '*.lib'|ForEach-Object {$_.FullName}
$sourcePath=Join-Path $root "Source/src/renderer-filament/$Source"
$outputPath=Join-Path $root "Client/$Output"
& "$vc/bin/Hostx64/x64/cl.exe" /nologo /LD /DOF_BUILD_DLL /EHsc /std:c++20 /MT /O2 /Zc:__cplusplus $sourcePath "/Fo$root/Tools/filament-build/backend.obj" /link "/OUT:$outputPath" "/IMPLIB:$outputPath.lib" @libs kernel32.lib user32.lib gdi32.lib opengl32.lib shell32.lib shlwapi.lib ole32.lib advapi32.lib ws2_32.lib dxgi.lib d3d11.lib
if($LASTEXITCODE -ne 0){throw 'Filament backend DLL compilation failed.'}
if($Source -eq 'backend.cpp'){
    $materials=Join-Path $root 'Client/filament-materials'
    New-Item -ItemType Directory -Path $materials -Force | Out-Null
    foreach($material in @('body','depth')){
        & "$filament/bin/matc.exe" -a vulkan -p desktop -o "$materials/$material.filamat" "$root/Source/src/renderer-filament/$material.mat"
        if($LASTEXITCODE -ne 0){throw "Filament material compilation failed: $material"}
    }
    Copy-Item -LiteralPath (Join-Path $root 'Artifacts/renderer-sdk/FILAMENT-LICENSE.txt') -Destination (Join-Path $root 'Client/FILAMENT-LICENSE.txt') -Force
}
if($Test){
    & "$vc/bin/Hostx64/x64/cl.exe" /nologo /EHsc /std:c++20 /MT /O2 "$root/Source/src/renderer-filament/runtime_test.cpp" "/Fo$root/Tools/filament-build/runtime_test.obj" "/Fe$root/Client/FilamentRuntimeTest.exe" /link kernel32.lib
    if($LASTEXITCODE -ne 0){throw 'Native runtime test compilation failed.'}
    & "$root/Client/FilamentRuntimeTest.exe" $outputPath "$root/Client/filament-materials"
    if($LASTEXITCODE -ne 0){throw 'Native Filament runtime checks failed.'}
}
