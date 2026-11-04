param([switch]$Build,[switch]$Fixtures,[switch]$ClassicUI,[int]$TimeoutSeconds=240)
$ErrorActionPreference='Stop'
$workspaceRoot=if(Test-Path (Join-Path $PSScriptRoot 'Source')){$PSScriptRoot}else{[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))}
$client=Join-Path $workspaceRoot 'Client'
if($Build){
    & wsl -d Ubuntu -u root -- bash ((Join-Path $workspaceRoot 'build-visual.sh').Replace('C:','/mnt/c').Replace('\','/')) prepare
    if($LASTEXITCODE){throw 'Client test source sync failed'}
    & wsl -d Ubuntu -u root -- bash ((Join-Path $workspaceRoot 'build-filament-release.sh').Replace('C:','/mnt/c').Replace('\','/')) tests
    if($LASTEXITCODE){throw 'Client test build failed'}
    & wsl -d Ubuntu -u root -- cp /opt/hazeron/Source/build/windows-release/bin/AuSceneClient.exe ((Join-Path $client 'AuSceneClient-smoke.exe').Replace('C:','/mnt/c').Replace('\','/'))
    if($LASTEXITCODE){throw 'Client test packaging failed'}
}
$exe=Join-Path $client 'AuSceneClient-smoke.exe'
if(!(Test-Path $exe)){throw 'Build the automated client first: Test-Client.ps1 -Build'}
$run=Join-Path $workspaceRoot ('Artifacts/client-smoke/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory $run -Force|Out-Null
$priorPack=$env:OPENSHORES_BODY_PARTS;$priorLog=$env:QT_FORCE_STDERR_LOGGING;$priorAudio=$env:ALSOFT_DRIVERS
try{
    $env:OPENSHORES_BODY_PARTS=Join-Path $client 'body-parts-modular';$env:QT_FORCE_STDERR_LOGGING='1';$env:ALSOFT_DRIVERS='null'
    $argsList=@('-ini','-graphics-enhanced','-renderer-filament-world','-client-smoke-dir',('"'+$run+'"'))
    if($ClassicUI){$argsList+="-ui-classic"}
    $process=Start-Process $exe -ArgumentList $argsList -WorkingDirectory $client -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run 'stdout.log') -RedirectStandardError (Join-Path $run 'stderr.log')
    $watch=[Diagnostics.Stopwatch]::StartNew()
    while(!$process.WaitForExit(1000)){
        if($watch.Elapsed.TotalSeconds -gt $TimeoutSeconds){$process.Kill();$process.WaitForExit();break}
    }
    $resultPath=Join-Path $run 'result.json'
    if(Test-Path $resultPath){$result=Get-Content $resultPath -Raw|ConvertFrom-Json}else{$result=[pscustomobject]@{passed=$false;error="Client exited without a report (exit $($process.ExitCode))"};$result|ConvertTo-Json|Set-Content $resultPath}
    if($process.ExitCode -ne 0){$result|Add-Member -NotePropertyName passed -NotePropertyValue $false -Force}
    if($process.ExitCode -ne 0 -and !$result.error){$result|Add-Member -NotePropertyName error -NotePropertyValue "Client exited with code $($process.ExitCode) during $($result.phase)" -Force;$result|ConvertTo-Json -Depth 10|Set-Content $resultPath}
    if($watch.Elapsed.TotalSeconds -gt $TimeoutSeconds){$result|Add-Member -NotePropertyName passed -NotePropertyValue $false -Force;$result|Add-Member -NotePropertyName error -NotePropertyValue "Client test timed out" -Force;$result|ConvertTo-Json -Depth 10|Set-Content $resultPath}
    $result|Add-Member -NotePropertyName executableSha256 -NotePropertyValue (Get-FileHash $exe -Algorithm SHA256).Hash -Force
    if($result.samples){$result|Add-Member -NotePropertyName meanRenderMs -NotePropertyValue (($result.samples|Measure-Object elapsedMs -Average).Average) -Force;$result|Add-Member -NotePropertyName meanNativeMs -NotePropertyValue (($result.samples|Measure-Object nativeMs -Average).Average) -Force}
    $result|ConvertTo-Json -Depth 12|Set-Content $resultPath
    $images=Get-ChildItem $run -Filter '*.png'|ForEach-Object{'<figure><img src="'+$_.Name+'"><figcaption>'+$_.BaseName+'</figcaption></figure>'}
    $html='<!doctype html><meta charset="utf-8"><title>Automated client test</title><style>body{background:#121923;color:#e7edf6;font:16px system-ui;margin:35px}img{max-width:100%}figure{margin:20px 0}pre{white-space:pre-wrap}</style><h1>Automated client test</h1><p>Passed: '+$result.passed+' · '+[Net.WebUtility]::HtmlEncode($result.error)+'</p>'+($images -join '')+'<p><a href="result.json">Structured results</a> · <a href="stderr.log">Client log</a></p>'
    Set-Content (Join-Path $run 'index.html') $html
    New-Item -ItemType Directory (Join-Path $workspaceRoot 'Artifacts/client-smoke') -Force|Out-Null
    Set-Content (Join-Path $workspaceRoot 'Artifacts/client-smoke/latest.json') ([pscustomobject]@{run=$run;passed=$result.passed;exitCode=$process.ExitCode;report=(Join-Path $run 'index.html')}|ConvertTo-Json)
    if($process.ExitCode -ne 0 -or !$result.passed){throw "Client test failed: $($result.error). Report: $run/index.html"}
    Write-Output "PASS automated client. Report: $run/index.html"
    if($Fixtures){& (Join-Path $workspaceRoot "Test-FilamentWorld.ps1") -Release -ColorOnly; & (Join-Path $workspaceRoot "Test-FilamentWorld.ps1") -Release}
}finally{$env:OPENSHORES_BODY_PARTS=$priorPack;$env:QT_FORCE_STDERR_LOGGING=$priorLog;$env:ALSOFT_DRIVERS=$priorAudio}
