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
if($Load){$Load=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Load)}
if($Save){$Save=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Save)}
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$exe=Join-Path $projectRoot 'build/bin/d2x.exe'
if(-not (Test-Path -LiteralPath $exe)){$exe=Join-Path $projectRoot 'build/bin/Release/d2x.exe'}
if(-not (Test-Path -LiteralPath $exe)){& (Join-Path $PSScriptRoot 'build.ps1');$exe=Join-Path $projectRoot 'build/bin/d2x.exe';if(-not (Test-Path -LiteralPath $exe)){$exe=Join-Path $projectRoot 'build/bin/Release/d2x.exe'}}
if(-not $Mpq){
    $fullSource=Join-Path $projectRoot 'assets/mpq2'
    if(-not (Test-Path -LiteralPath (Join-Path $fullSource 'd2data.mpq')) -and
       -not (Test-Path -LiteralPath (Join-Path $fullSource 'D2Data.mpq'))){
        throw 'Lord of Destruction expansion MPQ files are required in assets/mpq2.'
    }
    $Mpq=$fullSource
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
