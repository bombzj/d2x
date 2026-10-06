param(
    [string]$Mpq='',
    [string]$OnlineConfig='',
    [string]$OnlineCharacter='',
    [string]$OnlineCreateGame='',
    [string]$OnlineJoinGame='',
    [ValidatePattern('^[A-Za-z0-9_-]{1,80}$')][string]$PipeName='d2x-debug',
    [switch]$NoDebugPipe
)
$ErrorActionPreference='Stop'
if($OnlineCreateGame -and $OnlineJoinGame){throw 'Choose either -OnlineCreateGame or -OnlineJoinGame.'}
if(($OnlineCreateGame -or $OnlineJoinGame) -and -not $OnlineCharacter){throw 'Quick entry requires -OnlineCharacter.'}
if($Mpq){$Mpq=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Mpq)}
if($OnlineConfig){$OnlineConfig=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OnlineConfig)}
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$exe=Join-Path $projectRoot 'd2x.exe'
if(-not (Test-Path -LiteralPath $exe)){$exe=Join-Path $projectRoot 'build/bin/d2x.exe'}
if(-not (Test-Path -LiteralPath $exe)){$exe=Join-Path $projectRoot 'build/bin/Release/d2x.exe'}
if(-not (Test-Path -LiteralPath $exe)){
    throw 'D2X executable is missing. Build explicitly with scripts/build.ps1 before launching.'
}
if(-not $Mpq){
    foreach($folder in @($projectRoot,(Split-Path -Parent $exe))){
        if(Test-Path -LiteralPath (Join-Path $folder 'd2data.mpq') -PathType Leaf){$Mpq=$folder;break}
    }
}
if(-not $Mpq){
    $searchRoot=Get-Item -LiteralPath $projectRoot
    while($searchRoot){
        $fullSource=Join-Path $searchRoot.FullName 'assets/mpq2'
        if(Test-Path -LiteralPath (Join-Path $fullSource 'd2data.mpq')){$Mpq=$fullSource;break}
        $searchRoot=$searchRoot.Parent
    }
    if(-not $Mpq){throw 'Original MPQs were not found. Place them beside d2x.exe or supply -Mpq <folder>.'}
}
Push-Location $projectRoot
try{
    $arguments=@()
    if($OnlineConfig){$arguments+=@('--online-config',$OnlineConfig)}
    if($OnlineCharacter){$arguments+=@('--online-character',$OnlineCharacter)}
    if($OnlineCreateGame){$arguments+=@('--online-create-game',$OnlineCreateGame)}
    if($OnlineJoinGame){$arguments+=@('--online-join-game',$OnlineJoinGame)}
    if($Mpq){$arguments+=@('--mpq',$Mpq)}
    if(-not $NoDebugPipe){
        $arguments+=@('--debug-pipe',$PipeName)
        Write-Host "Local debug pipe: $PipeName (current Windows user only)"
    }
    & $exe @arguments
    if($LASTEXITCODE -ne 0){throw 'D2X could not start. See the error above.'}
}finally{Pop-Location}
