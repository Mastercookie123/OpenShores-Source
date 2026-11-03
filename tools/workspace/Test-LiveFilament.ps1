param([string]$Only,[switch]$Legacy)
$ErrorActionPreference='Stop'
$client=Join-Path $PSScriptRoot 'Client'
$exe=Join-Path $client 'RenderFilamentTest.exe'
if(!(Test-Path -LiteralPath $exe)){throw 'Build the live Filament client first.'}
$out=Join-Path $PSScriptRoot 'Artifacts/filament-live'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$cases=(Get-Content (Join-Path $PSScriptRoot 'Source/tools/visual_test/cases.json') -Raw | ConvertFrom-Json).cases
$cases+=@(
    [pscustomobject]@{id='depth-occlusion';sex=1;skin=2;pose='stand';depthOccluder=$true},
    [pscustomobject]@{id='dual-palette';sex=1;skin=2;pose='stand';palette=2;bottomPalette=5},
    [pscustomobject]@{id='armor';sex=1;skin=2;pose='walk';armor=$true},
    [pscustomobject]@{id='fog';sex=1;skin=2;pose='stand';fog=$true},
    [pscustomobject]@{id='headless';sex=1;skin=2;pose='stand';headless=$true},
    [pscustomobject]@{id='lamp-shadow-on';sex=1;skin=2;pose='stand';shadows=$true;lighting='point-shadow';lampShadows=2},
    [pscustomobject]@{id='lamp-shadow-off';sex=1;skin=2;pose='stand';shadows=$true;lighting='point-shadow';lampShadows=0}
)
if($Only){$cases=@($cases | Where-Object {$_.id -match $Only})}
if(!$cases){throw 'No matching test cases.'}
$prior=[Environment]::GetEnvironmentVariable('OPENSHORES_RENDERER','Process')
try {
    $env:OPENSHORES_RENDERER=if($Legacy){'opengl'}else{'filament'}
    foreach($case in $cases){
        $caseDir=Join-Path $out ($case.id+$(if($Legacy){'-legacy'}else{''}))
        New-Item -ItemType Directory -Path $caseDir -Force | Out-Null
        $fixture=Join-Path $caseDir 'case.json'
        $case | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $fixture -Encoding UTF8
        $argsList=@((Join-Path $client 'body-parts-modular'),$caseDir,$client,'--case',$fixture) | ForEach-Object {'"'+$_+'"'}
        $process=Start-Process -FilePath $exe -ArgumentList $argsList -WorkingDirectory $client -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput (Join-Path $caseDir 'stdout.log') -RedirectStandardError (Join-Path $caseDir 'process.log')
        if($process.ExitCode -ne 0){throw "Live renderer case $($case.id) failed: $($process.ExitCode); see $caseDir/process.log"}
        $report=Get-Content (Join-Path $caseDir 'render.json') -Raw | ConvertFrom-Json
        if(!$Legacy -and (!$report.filamentFrames -or $report.renderer -ne 'filament')){throw "Filament did not draw $($case.id)"}
        if($report.glErrors -ne 0){throw "OpenGL interop error in $($case.id)"}
        Write-Output "PASS $($case.id): renderer=$($report.renderer), frames=$($report.filamentFrames.Count)"
    }
}finally{[Environment]::SetEnvironmentVariable('OPENSHORES_RENDERER',$prior,'Process')}
