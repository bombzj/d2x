param(
    [string]$Mpq='',
    [int]$Level=1,
    [int]$Region=-1,
    [string]$Class='',
    [string]$Load='',
    [string]$Save='',
    [ValidatePattern('^[A-Za-z0-9_-]{1,80}$')][string]$PipeName='d2x-debug',
    [switch]$NoDebugPipe,
    [switch]$DebugPaused
)
$ErrorActionPreference='Stop'
if($NoDebugPipe -and $DebugPaused){throw '-DebugPaused cannot be combined with -NoDebugPipe.'}
if($Class -and $Load){throw 'Choose either -Class for a new character or -Load for a saved character.'}
if($Mpq){$Mpq=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Mpq)}
if($Load){$Load=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Load)}
if($Save){$Save=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Save)}
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$exe=Join-Path $projectRoot 'd2x.exe'
if(-not (Test-Path -LiteralPath $exe)){$exe=Join-Path $projectRoot 'build/bin/d2x.exe'}
if(-not (Test-Path -LiteralPath $exe)){$exe=Join-Path $projectRoot 'build/bin/Release/d2x.exe'}
if(-not (Test-Path -LiteralPath $exe)){
    $buildScript=Join-Path $PSScriptRoot 'build.ps1'
    if(-not (Test-Path -LiteralPath $buildScript)){throw 'D2X executable is missing. Rebuild the current package.'}
    & $buildScript
    $exe=Join-Path $projectRoot 'build/bin/d2x.exe'
    if(-not (Test-Path -LiteralPath $exe)){$exe=Join-Path $projectRoot 'build/bin/Release/d2x.exe'}
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
    if($PSBoundParameters.ContainsKey('Level')){$arguments+=@('--level',"$Level")}
    if($PSBoundParameters.ContainsKey('Region')){$arguments+=@('--region',"$Region")}
    if($Class){$arguments+=@('--class',$Class)}
    if($Load){$arguments+=@('--load',$Load)}
    if($Save){$arguments+=@('--save',$Save)}
    if($Mpq){$arguments+=@('--mpq',$Mpq)}
    if(-not $NoDebugPipe){
        $arguments+=@('--debug-pipe',$PipeName)
        if(-not $DebugPaused){$arguments+='--debug-run'}
        Write-Host "Local debug pipe: $PipeName (current Windows user only)"
    }
    & $exe @arguments
    if($LASTEXITCODE -ne 0){throw 'D2X could not start. See the error above.'}
}finally{Pop-Location}
