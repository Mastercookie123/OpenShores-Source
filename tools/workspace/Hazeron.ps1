param(
    [ValidateSet('Start','Stop','Status','Logs','BuildClient','Client','ClassicClient','LinuxClient','CharacterClient','ClassicCharacterClient','HumanClient','ClassicHumanClient','ProceduralClient','OriginalProceduralClient','VisualClient','OriginalVisualClient','ModularClient','OriginalModularClient','FilamentClient','FilamentWorldClient')]
    [string]$Action = 'Status'
)
$ErrorActionPreference = 'Stop'
$repo = '/opt/hazeron/Source'
if ($Action -in @('FilamentClient','FilamentWorldClient')) {
    $clientDir=Join-Path $PSScriptRoot 'Client'
    $world=$Action -eq 'FilamentWorldClient'
    $release=Get-Item (Join-Path $clientDir 'AuSceneClient-filament-world-r8-release.exe') -ErrorAction SilentlyContinue
    $debug=Get-Item (Join-Path $clientDir 'AuSceneClient-filament-world-r8-opt.exe') -ErrorAction SilentlyContinue
    $worldExecutable=if($release -and (!$debug -or $release.LastWriteTimeUtc -ge $debug.LastWriteTimeUtc)){$release.Name}else{'AuSceneClient-filament-world-r8-opt.exe'}
    $exe=Join-Path $clientDir $(if($world){$worldExecutable}else{'AuSceneClient-filament-r2.exe'})
    $dll=if($world){'OpenShoresFilamentWorld-r8.dll'}else{'OpenShoresFilament-r2.dll'}
    foreach($file in @($exe,(Join-Path $clientDir $dll),(Join-Path $clientDir 'filament-materials/body.filamat'),(Join-Path $clientDir 'filament-materials/depth.filamat'))){
        if(!(Test-Path -LiteralPath $file)){throw "Live Filament package is incomplete: $file"}
    }
    if($world){foreach($asset in @('filament-world-r8/manifest.json','filament-environment/lightroom_14b_ibl.ktx')){if(!(Test-Path (Join-Path $clientDir $asset))){throw "Full-scene renderer asset is missing: $asset"}}}
    $previousPack=[Environment]::GetEnvironmentVariable('OPENSHORES_BODY_PARTS','Process')
    $previousLog=[Environment]::GetEnvironmentVariable('QT_FORCE_STDERR_LOGGING','Process')
    try {
        $env:OPENSHORES_BODY_PARTS=Join-Path $clientDir 'body-parts-modular'
        $env:QT_FORCE_STDERR_LOGGING='1'
        $rendererFlag=if($world){'-renderer-filament-world'}else{'-renderer-filament'}
        $logPrefix=if($world){'filament-world-r8-game'}else{'filament-game'}
        Start-Process -FilePath $exe -WorkingDirectory $clientDir -ArgumentList '-alocalhost','-graphics-enhanced',$rendererFlag,'-log' -WindowStyle Normal -RedirectStandardOutput (Join-Path $clientDir "$logPrefix-stdout.log") -RedirectStandardError (Join-Path $clientDir "$logPrefix-stderr.log")
    }finally{[Environment]::SetEnvironmentVariable('OPENSHORES_BODY_PARTS',$previousPack,'Process');[Environment]::SetEnvironmentVariable('QT_FORCE_STDERR_LOGGING',$previousLog,'Process')}
    exit 0
}
if ($Action -in @('ModularClient','OriginalModularClient')) {
    $clientDir = Join-Path $PSScriptRoot 'Client'
    $exe = Join-Path $clientDir 'AuSceneClient-modular-r4.exe'
    if (!(Test-Path -LiteralPath $exe)) { throw 'The modular human package is not ready yet.' }
    $previousPack = [Environment]::GetEnvironmentVariable('OPENSHORES_BODY_PARTS','Process')
    try {
        $pack = if ($Action -eq 'ModularClient') { Join-Path $clientDir 'body-parts-modular' } else { $null }
        [Environment]::SetEnvironmentVariable('OPENSHORES_BODY_PARTS',$pack,'Process')
        Start-Process -FilePath $exe -WorkingDirectory $clientDir -ArgumentList '-alocalhost','-graphics-enhanced','-log' -WindowStyle Normal
    } finally {
        [Environment]::SetEnvironmentVariable('OPENSHORES_BODY_PARTS',$previousPack,'Process')
    }
    exit 0
}
if ($Action -in @('VisualClient','OriginalVisualClient')) {
    $clientDir = Join-Path $PSScriptRoot 'Client'
    $exe = Join-Path $clientDir 'AuSceneClient-visual.exe'
    if (!(Test-Path -LiteralPath $exe)) { throw 'The visual client package is not ready yet.' }
    $clientArgs = @('-alocalhost','-graphics-enhanced','-log')
    if ($Action -eq 'VisualClient') { $clientArgs += '-visual-overhaul' }
    Start-Process -FilePath $exe -WorkingDirectory $clientDir -ArgumentList $clientArgs -WindowStyle Normal
    exit 0
}
if ($Action -in @('HumanClient','ClassicHumanClient','ProceduralClient','OriginalProceduralClient')) {
    $clientDir = Join-Path $PSScriptRoot 'Client'
    $exe = Join-Path $clientDir 'AuSceneClient-procedural.exe'
    if (!(Test-Path -LiteralPath $exe)) { throw 'The procedural component package is not ready yet.' }
    $clientArgs = @('-alocalhost','-graphics-enhanced','-log')
    if ($Action -in @('HumanClient','ProceduralClient')) { $clientArgs += '-body-parts-enhanced' }
    Start-Process -FilePath $exe -WorkingDirectory $clientDir -ArgumentList $clientArgs -WindowStyle Normal
    exit 0
}
if ($Action -in @('CharacterClient','ClassicCharacterClient')) {
    $clientDir = Join-Path $PSScriptRoot 'Client'
    $exe = Join-Path $clientDir 'AuSceneClient-character.exe'
    if (!(Test-Path -LiteralPath $exe)) { throw 'The character comparison package is not ready yet.' }
    $characterFlag = if ($Action -eq 'CharacterClient') { '-character-refined' } else { '-character-classic' }
    Start-Process -FilePath $exe -WorkingDirectory $clientDir -ArgumentList '-alocalhost','-graphics-enhanced',$characterFlag,'-log' -WindowStyle Normal
    exit 0
}
switch ($Action) {
    'Start' { wsl -d Ubuntu -u root -- bash -lc "cd $repo && make server-up" }
    'Stop' { wsl -d Ubuntu -u root -- bash -lc "cd $repo && make server-down" }
    'Status' { wsl -d Ubuntu -u root -- bash -lc "cd $repo && make server-status" }
    'Logs' { wsl -d Ubuntu -u root -- bash -lc "cd $repo && make server-logs" }
    'BuildClient' { wsl -d Ubuntu -u root -- bash -lc "cd $repo && make win-client CONFIG=debug" }
    'LinuxClient' {
        Start-Process -FilePath wsl.exe -WindowStyle Hidden -ArgumentList '-d','Ubuntu','-u','root','--','env','ALSOFT_DRIVERS=pulse',"$repo/build/linux-debug/bin/AuSceneClient",'-alocalhost','-graphics-enhanced','-body-parts-enhanced','-log'
    }
    'Client' {
        $clientDir = Join-Path $PSScriptRoot 'Client'
        $exe = Join-Path $clientDir 'AuSceneClient-procedural.exe'
        if (!(Test-Path -LiteralPath $exe)) { throw 'Client package is not ready in the Client folder yet.' }
        Start-Process -FilePath $exe -WorkingDirectory $clientDir -ArgumentList '-alocalhost','-graphics-enhanced','-body-parts-enhanced','-log' -WindowStyle Normal
    }
    'ClassicClient' {
        $clientDir = Join-Path $PSScriptRoot 'Client'
        $exe = Join-Path $clientDir 'AuSceneClient.exe'
        if (!(Test-Path -LiteralPath $exe)) { throw 'Client package is not ready in the Client folder yet.' }
        Start-Process -FilePath $exe -WorkingDirectory $clientDir -ArgumentList '-alocalhost','-graphics-classic','-log' -WindowStyle Normal
    }
}
if ($Action -notin @('Client','ClassicClient','LinuxClient') -and $LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
