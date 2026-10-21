param(
    [string]$Mpq='',
    [string]$OnlineConfig='',
    [string]$ClientProfile='',
    [string]$OnlineCharacter='',
    [string]$OnlinePlay='',
    [string]$OnlineCreateGame='',
    [string]$OnlineJoinGame='',
    [string]$GamePacketLog='',
    [string]$HostPacketLog='',
    [switch]$Diagnostic,
    [switch]$Hidden,
    [string]$Lang='',
    [ValidatePattern('^[A-Za-z0-9_-]{1,80}$')][string]$PipeName='d2x-debug',
    [switch]$NoDebugPipe
)
$ErrorActionPreference='Stop'
if($ClientProfile -and ($Mpq -or $OnlineConfig -or $OnlinePlay -or $OnlineCreateGame -or $OnlineJoinGame -or $HostPacketLog -or $GamePacketLog)){
    throw '-ClientProfile owns its resource/service selection and currently supports the account lobby only.'
}
if($OnlinePlay -and ($OnlineCharacter -or $OnlineCreateGame -or $OnlineJoinGame)){throw '-OnlinePlay cannot be combined with manual quick-entry options.'}
if($OnlineCreateGame -and $OnlineJoinGame){throw 'Choose either -OnlineCreateGame or -OnlineJoinGame.'}
if($Diagnostic -and ($GamePacketLog -or $HostPacketLog -or $NoDebugPipe)){throw '-Diagnostic requires its own packet logs and the debug pipe.'}
if(($OnlineCreateGame -or $OnlineJoinGame) -and -not $OnlineCharacter){throw 'Quick entry requires -OnlineCharacter.'}
if($Mpq){$Mpq=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Mpq)}
if($OnlineConfig){$OnlineConfig=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OnlineConfig)}
if($ClientProfile){$ClientProfile=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ClientProfile)}
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$diagnosticRoot=''
if($Diagnostic){
    $runName=(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8)
    $diagnosticRoot=Join-Path $projectRoot "artifacts/diagnostics/$runName"
    New-Item -ItemType Directory -Path $diagnosticRoot -Force | Out-Null
    if(-not $ClientProfile){
        $GamePacketLog=Join-Path $diagnosticRoot 'client-packets.jsonl'
        $HostPacketLog=Join-Path $diagnosticRoot 'host-packets.jsonl'
    }
}
if($GamePacketLog){$GamePacketLog=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($GamePacketLog)}
if($HostPacketLog){$HostPacketLog=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($HostPacketLog)}
$exe=Join-Path $projectRoot 'd2x.exe'
if(-not (Test-Path -LiteralPath $exe)){$exe=Join-Path $projectRoot 'build/bin/d2x.exe'}
if(-not (Test-Path -LiteralPath $exe)){$exe=Join-Path $projectRoot 'build/bin/Release/d2x.exe'}
if(-not (Test-Path -LiteralPath $exe)){
    throw 'D2X executable is missing. Build explicitly with scripts/build.ps1 before launching.'
}
if(-not $Mpq -and -not $ClientProfile){
    foreach($folder in @($projectRoot,(Split-Path -Parent $exe))){
        if(Test-Path -LiteralPath (Join-Path $folder 'd2data.mpq') -PathType Leaf){$Mpq=$folder;break}
    }
}
if(-not $Mpq -and -not $ClientProfile){
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
    if($Hidden){$arguments+='--hidden'}
    if($ClientProfile){$arguments+=@('--client-profile',$ClientProfile)}
    if($OnlineConfig){$arguments+=@('--online-config',$OnlineConfig)}
    if($OnlineCharacter){$arguments+=@('--online-character',$OnlineCharacter)}
    if($OnlinePlay){$arguments+=@('--online-play',$OnlinePlay)}
    if($OnlineCreateGame){$arguments+=@('--online-create-game',$OnlineCreateGame)}
    if($OnlineJoinGame){$arguments+=@('--online-join-game',$OnlineJoinGame)}
    if($Mpq){$arguments+=@('--mpq',$Mpq)}
    if($GamePacketLog){$arguments+=@('--game-packet-log',$GamePacketLog)}
    if($HostPacketLog){$arguments+=@('--host-packet-log',$HostPacketLog)}
    if($Lang){$arguments+=@('--lang',$Lang)}
    if(-not $NoDebugPipe){
        $arguments+=@('--debug-pipe',$PipeName)
        Write-Host "Local debug pipe: $PipeName (current Windows user only)"
    }
    if($Diagnostic){
        $binaryStream=[IO.File]::OpenRead($exe)
        $digest=[Security.Cryptography.SHA256]::Create()
        try{$binaryHash=[BitConverter]::ToString($digest.ComputeHash($binaryStream)).Replace('-','')}
        finally{$digest.Dispose();$binaryStream.Dispose()}
        [ordered]@{
            executable=$exe; sha256=$binaryHash
            mpq=$Mpq; clientProfile=$ClientProfile; pipe=$PipeName; packets=$GamePacketLog
            hostPackets=$HostPacketLog; packetDirection=$(if($ClientProfile){'none (account lobby counters only)'}else{'C2S+S2C'})
            note=$(if($ClientProfile){'No authentication payloads are captured. Read packet IDs and counts through online-status.'}else{'Protocol traces redact tickets and opaque save transfers; authoritative inventory remains available through the debug pipe.'})
        } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $diagnosticRoot 'run.json') -Encoding utf8
        Write-Host "Diagnostic logs: $diagnosticRoot"
        # Windows PowerShell treats native stderr as ErrorRecords. Cache/resource
        # notices must be recorded without aborting the interactive game process.
        $nativePreference=$ErrorActionPreference
        try{
            $ErrorActionPreference='Continue'
            & $exe @arguments 1> (Join-Path $diagnosticRoot 'stdout.log') 2> (Join-Path $diagnosticRoot 'stderr.log')
        }finally{$ErrorActionPreference=$nativePreference}
    }else{& $exe @arguments}
    if($LASTEXITCODE -ne 0){throw 'D2X could not start. See the error above.'}
}finally{Pop-Location}
