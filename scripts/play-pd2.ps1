param(
    [string]$Profile='',
    [string]$Pd2Directory='',
    [string]$AccountHost='',
    [string]$Gateway='',
    [string]$OnlineCharacter='',
    [string]$Lang='eng',
    [switch]$Diagnostic,
    [switch]$Hidden,
    [switch]$NoDebugPipe
)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(-not $Profile){$Profile=Join-Path $projectRoot 'pd2.local.json'}
$Profile=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Profile)
if(-not (Test-Path -LiteralPath $Profile -PathType Leaf)){
    if(-not $Pd2Directory){$Pd2Directory=$env:D2X_PD2_DIRECTORY}
    if(-not $Pd2Directory){$Pd2Directory=Read-Host 'PD2 CN directory inside the isolated game copy'}
    $Pd2Directory=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Pd2Directory)
    if(-not (Test-Path -LiteralPath (Join-Path $Pd2Directory 'ProjectDiablo.dll') -PathType Leaf)){
        throw 'ProjectDiablo.dll was not found in the selected PD2 CN directory.'
    }
    if(-not $AccountHost){$AccountHost=$env:D2X_PD2_ACCOUNT_HOST}
    if(-not $AccountHost){
        $ipFile=Join-Path $Pd2Directory 'zylpd2.ip'
        if(Test-Path -LiteralPath $ipFile -PathType Leaf){
            $AccountHost=Get-Content -LiteralPath $ipFile | Where-Object {$_ -match '^\d{1,3}(\.\d{1,3}){3}$'} | Select-Object -First 1
        }
    }
    if(-not $AccountHost){$AccountHost=Read-Host 'PD2 CN account server hostname (TCP, direct connection)'}
    if(-not $AccountHost -or $AccountHost -match '[\s/\\]'){throw 'An account server hostname or IP is required.'}
    # Paths and service configuration only. Account passwords are entered in D2X
    # and use its existing per-configuration credential storage.
    [ordered]@{
        version=1; clientProfile='pd2-cn-normal-20261020'; authentication='pd2-cn'
        originalClientDirectory=(Split-Path -Parent $Pd2Directory); pd2Directory=$Pd2Directory
        accountHost=$AccountHost; accountPort=6112; gamePort=4001; gateway=$Gateway
        gameServer=1; lootAllocation=$true
    } | ConvertTo-Json | Set-Content -LiteralPath $Profile -Encoding utf8
}elseif($Pd2Directory -or $AccountHost -or $Gateway){
    throw 'The profile already exists. Edit it or choose a new -Profile path to change its service/resources.'
}
$parameters=@{ClientProfile=$Profile;Lang=$Lang;PipeName='d2x-pd2-debug'}
if($OnlineCharacter){$parameters.OnlineCharacter=$OnlineCharacter}
if($Diagnostic){$parameters.Diagnostic=$true}
if($Hidden){$parameters.Hidden=$true}
if($NoDebugPipe){$parameters.NoDebugPipe=$true}
& (Join-Path $PSScriptRoot 'play.ps1') @parameters
