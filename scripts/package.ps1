param()
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$exe=Join-Path $projectRoot 'build/bin/d2x.exe'
if(-not (Test-Path -LiteralPath $exe)){$exe=Join-Path $projectRoot 'build/bin/Release/d2x.exe'}
if(-not (Test-Path -LiteralPath $exe)){throw 'Build Release with scripts/build.ps1 before packaging.'}
$destination=Join-Path $projectRoot 'dist/current'
foreach($folder in @($destination,(Join-Path $destination 'scripts'),(Join-Path $destination 'docs'))){
    New-Item -ItemType Directory -Path $folder -Force | Out-Null
}
Copy-Item -LiteralPath $exe -Destination (Join-Path $destination 'd2x.exe') -Force
foreach($name in @('Play.cmd','README.md','BASELINE.md','LICENSE')){
    Copy-Item -LiteralPath (Join-Path $projectRoot $name) -Destination $destination -Force
}
foreach($name in @('play.ps1','Send-D2XCommand.ps1')){
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $destination 'scripts') -Force
}
Copy-Item -Path (Join-Path $projectRoot 'docs/*') -Destination (Join-Path $destination 'docs') -Recurse -Force
Write-Host "Updated $destination. Original MPQs are shared through -Mpq or ancestor assets/mpq2."
Write-Host 'Existing saves and artifacts in the run directory are preserved.'