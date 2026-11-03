$ErrorActionPreference='Stop'
$root=Join-Path $PSScriptRoot 'Tools/filament-build'
$catalog=Get-Content (Join-Path $root 'catalog.json') -Raw|ConvertFrom-Json
$compiler=Get-Content (Join-Path $root 'compiler-packages.json') -Raw|ConvertFrom-Json
$sdkPackage=$catalog.packages|Where-Object {$_.id -eq 'Win11SDK_10.0.26100'}|Select-Object -First 1
$plan=@()
foreach($package in $compiler){foreach($payload in $package.payloads){$plan+=@{url=$payload.url;path=(Join-Path $root ('downloads/compiler/'+$payload.fileName));size=$payload.size;sha256=$payload.sha256}}}
foreach($payload in $sdkPackage.payloads){$plan+=@{url=$payload.url;path=(Join-Path $root ('downloads/sdk/'+$payload.fileName));size=$payload.size;sha256=$payload.sha256}}
$plan|ConvertTo-Json -Depth 4|Set-Content (Join-Path $root 'download-plan.json')
$plan|ForEach-Object -Parallel {
 $entry=$_;$parent=Split-Path $entry.path;[IO.Directory]::CreateDirectory($parent)|Out-Null
 if(!(Test-Path -LiteralPath $entry.path) -or (Get-Item -LiteralPath $entry.path).Length -ne $entry.size){Invoke-WebRequest $entry.url -OutFile $entry.path}
 if($entry.sha256 -and (Get-FileHash -LiteralPath $entry.path -Algorithm SHA256).Hash -ne $entry.sha256){throw ('Checksum mismatch: '+$entry.path)}
} -ThrottleLimit 8
[IO.Directory]::CreateDirectory((Join-Path $root 'msvc'))|Out-Null
foreach($package in $compiler){foreach($payload in $package.payloads){[IO.Compression.ZipFile]::ExtractToDirectory((Join-Path $root ('downloads/compiler/'+$payload.fileName)),(Join-Path $root 'msvc'),$true)}}