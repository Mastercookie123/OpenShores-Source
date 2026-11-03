param([string]$Only,[int]$Threads=8)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$compiler=Join-Path $root 'Artifacts/renderer-sdk/filament-windows/bin/matc.exe'
$directory=Join-Path $root 'Artifacts/filament-world/materials'
$inputs=@(Get-ChildItem -LiteralPath $directory -Filter '*.mat' | Where-Object {!$Only -or $_.Name -match $Only})
$results=@($inputs | ForEach-Object -Parallel {
    $inputPath=$_.FullName;$outputPath=[IO.Path]::ChangeExtension($inputPath,'.filamat');$log=$inputPath+'.log'
    & $using:compiler -a vulkan -p desktop -o $outputPath $inputPath *> $log
    [pscustomobject]@{name=$_.Name;passed=$LASTEXITCODE -eq 0;error=$(if($LASTEXITCODE -ne 0){Get-Content $log -Tail 9|Out-String}else{''})}
} -ThrottleLimit $Threads)
$results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $directory 'compile-report.json') -Encoding UTF8
$failed=@($results|Where-Object {!$_.passed})
Write-Output "Native world materials: $($results.Count-$failed.Count)/$($results.Count) compiled."
$failed | Select-Object -First 12 | Format-List
if($failed.Count){exit 1}
