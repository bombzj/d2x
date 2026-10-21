param([string]$OutputDirectory='',[switch]$Archive)
$ErrorActionPreference='Stop'
$script:copiedFiles=0
$script:copiedBytes=0L
$script:unchangedFiles=0
function Copy-ChangedFile([string]$Source,[string]$Destination) {
    $sourceFile=Get-Item -LiteralPath $Source
    if(Test-Path -LiteralPath $Destination -PathType Leaf){
        $targetFile=Get-Item -LiteralPath $Destination
        if($sourceFile.Length -eq $targetFile.Length -and
           (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash -eq (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash){
            $script:unchangedFiles++
            return
        }
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
    $script:copiedFiles++
    $script:copiedBytes+=$sourceFile.Length
}
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$exe=Join-Path $projectRoot 'build/bin/d2x.exe'
if(-not (Test-Path -LiteralPath $exe)){$exe=Join-Path $projectRoot 'build/bin/Release/d2x.exe'}
if(-not (Test-Path -LiteralPath $exe)){throw 'Build Release with scripts/build.ps1 before packaging.'}
$networkRuntime=Join-Path (Split-Path -Parent $exe) 'd2x_bncs_legacy.dll'
if(-not (Test-Path -LiteralPath $networkRuntime)){throw 'Rebuild the current source before packaging: d2x_bncs_legacy.dll is required.'}
$destination=Join-Path $projectRoot 'dist/current'
if($Archive -and -not $OutputDirectory){throw '-Archive requires a new -OutputDirectory to exclude existing saves, credentials and diagnostic files.'}
if($OutputDirectory){
    $destination=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
    $distributionRoot=[IO.Path]::GetFullPath((Join-Path $projectRoot 'dist'))+[IO.Path]::DirectorySeparatorChar
    if(-not $destination.StartsWith($distributionRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'Package output must be a new directory inside this project dist folder.'}
    if(Test-Path -LiteralPath $destination){throw 'Package output already exists; choose a new directory.'}
}
foreach($folder in @($destination,(Join-Path $destination 'scripts'),(Join-Path $destination 'docs'))){
    New-Item -ItemType Directory -Path $folder -Force | Out-Null
}
Copy-ChangedFile $exe (Join-Path $destination 'd2x.exe')
Copy-ChangedFile $networkRuntime (Join-Path $destination 'd2x_bncs_legacy.dll')
$onlineProfile=Join-Path $projectRoot 'online.local.json'
if(Test-Path -LiteralPath $onlineProfile){
    $profile=Get-Content -LiteralPath $onlineProfile -Raw | ConvertFrom-Json
    if($profile.authentication -eq 'pvpgn'){
        # Package only connection settings, never account passwords or CD keys.
        $profileJson=[ordered]@{
            gateway=$profile.gateway; accountHost=$profile.accountHost
            accountPort=$profile.accountPort; gamePort=$profile.gamePort
            originalClientDirectory=$profile.originalClientDirectory
            authentication='pvpgn'; keyOwner=$profile.keyOwner
        } | ConvertTo-Json
        $profilePath=Join-Path $destination 'online.local.json'
        if(-not (Test-Path -LiteralPath $profilePath) -or ([string](Get-Content -LiteralPath $profilePath -Raw)).Trim() -ne $profileJson.Trim()){
            Set-Content -LiteralPath $profilePath -Value $profileJson -Encoding utf8
            $script:copiedFiles++
            $script:copiedBytes+=(Get-Item -LiteralPath $profilePath).Length
        } else {$script:unchangedFiles++}
    }
}
$pd2Profile=Join-Path $projectRoot 'pd2.local.json'
if(Test-Path -LiteralPath $pd2Profile){
    $profile=Get-Content -LiteralPath $pd2Profile -Raw | ConvertFrom-Json
    if($profile.authentication -eq 'pd2-cn' -and $profile.clientProfile -eq 'pd2-cn-normal-20261020'){
        $publicProfile=[ordered]@{}
        foreach($key in @('version','clientProfile','authentication','accountHost','accountPort','gamePort','gateway')){$publicProfile[$key]=$profile.$key}
        $publicProfile.gameServer=if($null -ne $profile.gameServer){$profile.gameServer}else{1}
        $publicProfile.lootAllocation=if($null -ne $profile.lootAllocation){$profile.lootAllocation}else{$true}
        foreach($key in @('originalClientDirectory','pd2Directory')){
            $value=[string]$profile.$key
            if(-not [IO.Path]::IsPathRooted($value)){$value=Join-Path $projectRoot $value}
            $publicProfile[$key]=[IO.Path]::GetFullPath($value)
        }
        $profilePath=Join-Path $destination 'pd2.local.json'
        $profileJson=$publicProfile | ConvertTo-Json
        if(-not (Test-Path -LiteralPath $profilePath) -or ([string](Get-Content -LiteralPath $profilePath -Raw)).Trim() -ne $profileJson.Trim()){
            Set-Content -LiteralPath $profilePath -Value $profileJson -Encoding utf8
            $script:copiedFiles++;$script:copiedBytes+=(Get-Item -LiteralPath $profilePath).Length
        }else{$script:unchangedFiles++}
    }
}
foreach($name in @('Play.cmd','PlayPD2.cmd','Play-Diagnostic.cmd','README.md','BASELINE.md','LICENSE')){
    Copy-ChangedFile (Join-Path $projectRoot $name) (Join-Path $destination $name)
}
foreach($name in @('play.ps1','play-pd2.ps1','Send-D2XCommand.ps1')){
    Copy-ChangedFile (Join-Path $PSScriptRoot $name) (Join-Path $destination "scripts/$name")
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
foreach($file in Get-ChildItem -LiteralPath $sourceDocs -File -Recurse){
    $relative=$file.FullName.Substring($sourceDocs.Length + 1)
    Copy-ChangedFile $file.FullName (Join-Path $packagedDocs $relative)
}
Write-Host ('Files copied: {0} ({1:N2} MiB); unchanged files skipped: {2}.' -f $script:copiedFiles,($script:copiedBytes/1MB),$script:unchangedFiles)
Write-Host "Updated $destination. Original MPQs can be beside d2x.exe, in ancestor assets/mpq2, or supplied through -Mpq."
Write-Host 'Existing saves and artifacts in the run directory are preserved.'
if($Archive){
    $archivePath=$destination+'.zip'
    if(Test-Path -LiteralPath $archivePath){throw 'Archive already exists; choose a new output directory.'}
    Compress-Archive -LiteralPath $destination -DestinationPath $archivePath -CompressionLevel Optimal
    Write-Host "Archive: $archivePath"
}
