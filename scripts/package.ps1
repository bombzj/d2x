param()
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$exe=Join-Path $projectRoot 'build/bin/d2x.exe'
if(-not (Test-Path -LiteralPath $exe)){$exe=Join-Path $projectRoot 'build/bin/Release/d2x.exe'}
if(-not (Test-Path -LiteralPath $exe)){throw 'Build Release with scripts/build.ps1 before packaging.'}
$networkRuntime=Join-Path (Split-Path -Parent $exe) 'd2x_bncs_legacy.dll'
if(-not (Test-Path -LiteralPath $networkRuntime)){throw 'Rebuild the current source before packaging: d2x_bncs_legacy.dll is required.'}
$destination=Join-Path $projectRoot 'dist/current'
foreach($folder in @($destination,(Join-Path $destination 'scripts'),(Join-Path $destination 'docs'))){
    New-Item -ItemType Directory -Path $folder -Force | Out-Null
}
Copy-Item -LiteralPath $exe -Destination (Join-Path $destination 'd2x.exe') -Force
Copy-Item -LiteralPath $networkRuntime -Destination $destination -Force
$onlineProfile=Join-Path $projectRoot 'online.local.json'
if(Test-Path -LiteralPath $onlineProfile){
    $profile=Get-Content -LiteralPath $onlineProfile -Raw | ConvertFrom-Json
    if($profile.authentication -eq 'pvpgn'){
        # Package only connection settings, never account passwords or CD keys.
        [ordered]@{
            gateway=$profile.gateway; accountHost=$profile.accountHost
            accountPort=$profile.accountPort; gamePort=$profile.gamePort
            originalClientDirectory=$profile.originalClientDirectory
            authentication='pvpgn'; keyOwner=$profile.keyOwner
        } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $destination 'online.local.json') -Encoding utf8
    }
}
foreach($name in @('Play.cmd','README.md','BASELINE.md','LICENSE')){
    Copy-Item -LiteralPath (Join-Path $projectRoot $name) -Destination $destination -Force
}
foreach($name in @('play.ps1','Send-D2XCommand.ps1')){
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $destination 'scripts') -Force
}
$sourceDocs=Join-Path $projectRoot 'docs'
$packagedDocs=[IO.Path]::GetFullPath((Join-Path $destination 'docs'))
# Remove obsolete documentation after source moves; preserve saves and artifacts.
foreach($file in Get-ChildItem -LiteralPath $packagedDocs -File -Recurse){
    $relative=$file.FullName.Substring($packagedDocs.Length + 1)
    if(-not (Test-Path -LiteralPath (Join-Path $sourceDocs $relative) -PathType Leaf)){
        Remove-Item -LiteralPath $file.FullName
    }
}
Copy-Item -Path (Join-Path $sourceDocs '*') -Destination $packagedDocs -Recurse -Force
Write-Host "Updated $destination. Original MPQs can be beside d2x.exe, in ancestor assets/mpq2, or supplied through -Mpq."
Write-Host 'Existing saves and artifacts in the run directory are preserved.'
