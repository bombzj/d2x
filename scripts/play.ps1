param(
    [string]$Mpq='',
    [string]$OnlineConfig='',
    [string]$ClientProfile='',
    [switch]$Pd2Local,
    [string]$HostLan='',
    [string]$Lan='',
    [ValidateRange(1,65535)][int]$GamePort=4000,
    [string]$HostSaves='',
    [string]$OnlineCharacter='',
    [string]$OnlinePlay='',
    [string]$OnlineCreateGame='',
    [string]$OnlineJoinGame='',
    [string]$GamePacketLog='',
    [string]$HostPacketLog='',
    [switch]$Diagnostic,
    [switch]$Hidden,
    [switch]$NoPauseOnFocusLoss,
    [string]$Lang='',
    [ValidatePattern('^[A-Za-z0-9_-]{1,80}$')][string]$PipeName='d2x-debug',
    [switch]$NoDebugPipe,
    [string]$SorceressScenario=''
)
$ErrorActionPreference='Stop'
function Get-DiagnosticSha256([string]$Path) {
    $stream=[IO.File]::OpenRead($Path)
    try{
        $digest=[Security.Cryptography.SHA256]::Create()
        try{[BitConverter]::ToString($digest.ComputeHash($stream)).Replace('-','')}
        finally{$digest.Dispose()}
    }finally{$stream.Dispose()}
}
if($Pd2Local -and -not $ClientProfile){throw '-Pd2Local requires -ClientProfile.'}
if($HostLan -and $Lan){throw 'Choose either -HostLan or -Lan.'}
if($ClientProfile -and ($Mpq -or $OnlineConfig)){
    throw '-ClientProfile owns its resources and online configuration.'
}
if($OnlinePlay -and ($OnlineCharacter -or $OnlineCreateGame -or $OnlineJoinGame)){throw '-OnlinePlay cannot be combined with manual quick-entry options.'}
if($OnlineCreateGame -and $OnlineJoinGame){throw 'Choose either -OnlineCreateGame or -OnlineJoinGame.'}
if($Diagnostic -and ($GamePacketLog -or $HostPacketLog -or $NoDebugPipe)){throw '-Diagnostic requires its own packet logs and the debug pipe.'}
if(($OnlineCreateGame -or $OnlineJoinGame) -and -not $OnlineCharacter){throw 'Quick entry requires -OnlineCharacter.'}
if($Mpq){$Mpq=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Mpq)}
if($OnlineConfig){$OnlineConfig=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OnlineConfig)}
if($ClientProfile){$ClientProfile=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ClientProfile)}
if($HostSaves){$HostSaves=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($HostSaves)}
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$diagnosticParent=Join-Path $projectRoot 'artifacts/diagnostics'
$previousSorceressRun=$null
if($SorceressScenario){
    if(-not $Diagnostic -or $ClientProfile -or $HostLan -or $Lan -or $HostSaves -or
        $OnlineCharacter -or $OnlinePlay -or $OnlineCreateGame -or $OnlineJoinGame -or
        $Hidden -or $PSBoundParameters.ContainsKey('PipeName')){
        throw 'Play-SOR requires graphical Single Player with its own save directory and diagnostic pipe.'
    }
    if(Get-Process -Name d2x -ErrorAction SilentlyContinue){
        throw 'A D2X instance is already running. Close it before starting Play-SOR.'
    }
    if($SorceressScenario -eq 'continue'){
        $previousSorceressRun=Get-ChildItem -LiteralPath $diagnosticParent -Directory -ErrorAction SilentlyContinue |
            Where-Object {Test-Path -LiteralPath (Join-Path $_.FullName 'sorceress-start.json') -PathType Leaf} |
            Sort-Object Name -Descending | Select-Object -First 1
        if(-not $previousSorceressRun){throw 'No previous Play-SOR run to continue.'}
        $previousInfo=Get-Content -LiteralPath (Join-Path $previousSorceressRun.FullName 'run.json') -Raw | ConvertFrom-Json
        $SorceressScenario=[string]$previousInfo.scenario
    }
    if($SorceressScenario -notmatch '^act([1-5])(?:-([1-5]))?$' -or
        ($Matches[2] -and [int]$Matches[2] -lt [int]$Matches[1])){
        throw 'Choose act1 through act5, an ascending range such as act2-5, or continue.'
    }
    $startAct=[int]$Matches[1]-1
    $SorceressScenario=$SorceressScenario.ToLowerInvariant()
    $actNames=@('One','Two','Three','Four','Five')
    $sorceressName="Act$($actNames[$startAct])Sorc"
    $sorceressTown=@(1,40,75,103,109)[$startAct]
    $sorceressSource=if($previousSorceressRun){
        Join-Path $previousSorceressRun.FullName "saves/$sorceressName.d2s"
    }else{
        Join-Path $projectRoot "artifacts/saves/$sorceressName.d2s"
    }
    if(-not (Test-Path -LiteralPath $sorceressSource -PathType Leaf)){
        throw "Missing 99 Sorceress save: $sorceressSource"
    }
    $NoPauseOnFocusLoss=$true
}
$diagnosticRoot=''
$frameProfile=''
$startupProfile=''
$movementDiffLog=''
if($Diagnostic){
    $runName=(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8)
    $diagnosticRoot=Join-Path $diagnosticParent $runName
    New-Item -ItemType Directory -Path $diagnosticRoot -Force | Out-Null
    $GamePacketLog=Join-Path $diagnosticRoot 'client-packets.jsonl'
    $HostPacketLog=Join-Path $diagnosticRoot 'host-packets.jsonl'
    $frameProfile=Join-Path $diagnosticRoot 'frames.jsonl'
    $startupProfile=Join-Path $diagnosticRoot 'startup.tsv'
    if($SorceressScenario){
        $PipeName="d2x-sor-$runName"
        $HostSaves=Join-Path $diagnosticRoot 'saves'
        New-Item -ItemType Directory -Path $HostSaves | Out-Null
        $sorceressSave=Join-Path $HostSaves "$sorceressName.d2s"
        Copy-Item -LiteralPath $sorceressSource -Destination $sorceressSave
        if($previousSorceressRun){
            foreach($extension in 'map','ma0','ma1','ma2','ma3'){
                $automap=Join-Path (Split-Path -Parent $sorceressSource) "$sorceressName.$extension"
                if(Test-Path -LiteralPath $automap -PathType Leaf){
                    Copy-Item -LiteralPath $automap -Destination $HostSaves
                }
            }
        }
    }
    if(-not ($HostLan -or $Lan -or $OnlineCharacter -or $OnlinePlay -or $OnlineCreateGame -or $OnlineJoinGame)){
        $movementDiffLog=Join-Path $diagnosticRoot 'movement-diff.jsonl'
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
$previousNavigationDump=$env:D2X_NAVIGATION_DUMP
Push-Location $projectRoot
try{
    if($Diagnostic){$env:D2X_NAVIGATION_DUMP=Join-Path $diagnosticRoot 'navigation'}
    $arguments=@()
    if($Hidden){$arguments+='--hidden'}
    if($NoPauseOnFocusLoss){$arguments+='--no-pause-on-focus-loss'}
    if($ClientProfile){$arguments+=@('--client-profile',$ClientProfile)}
    if($Pd2Local){$arguments+='--pd2-local'}
    if($HostLan){$arguments+=@('--host-lan',$HostLan)}
    if($Lan){$arguments+=@('--lan',$Lan)}
    if($PSBoundParameters.ContainsKey('GamePort')){$arguments+=@('--game-port',"$GamePort")}
    if($HostSaves){$arguments+=@('--host-saves',$HostSaves)}
    if($SorceressScenario){$arguments+=@('--load',$sorceressSave)}
    if($OnlineConfig){$arguments+=@('--online-config',$OnlineConfig)}
    if($OnlineCharacter){$arguments+=@('--online-character',$OnlineCharacter)}
    if($OnlinePlay){$arguments+=@('--online-play',$OnlinePlay)}
    if($OnlineCreateGame){$arguments+=@('--online-create-game',$OnlineCreateGame)}
    if($OnlineJoinGame){$arguments+=@('--online-join-game',$OnlineJoinGame)}
    if($Mpq){$arguments+=@('--mpq',$Mpq)}
    if($GamePacketLog){$arguments+=@('--game-packet-log',$GamePacketLog)}
    if($HostPacketLog){$arguments+=@('--host-packet-log',$HostPacketLog)}
    if($frameProfile){$arguments+=@('--frame-profile',$frameProfile)}
    if($startupProfile){$arguments+=@('--startup-profile',$startupProfile)}
    if($movementDiffLog){$arguments+=@('--movement-diff-log',$movementDiffLog)}
    if($Lang){$arguments+=@('--lang',$Lang)}
    if(-not $NoDebugPipe){
        $arguments+=@('--debug-pipe',$PipeName)
        Write-Host "Local debug pipe: $PipeName (current Windows user only)"
    }
    if($Diagnostic){
        $binaryHash=Get-DiagnosticSha256 $exe
        $aiConfig=Join-Path (Split-Path -Parent $exe) 'debug-ai.json'
        if(-not (Test-Path -LiteralPath $aiConfig -PathType Leaf)){$aiConfig=Join-Path $projectRoot 'debug-ai.json'}
        if($SorceressScenario){
            $sourceConfig=Join-Path $projectRoot 'config/debug-ai.json'
            if(Test-Path -LiteralPath $sourceConfig -PathType Leaf){$aiConfig=$sourceConfig}
            if(-not (Test-Path -LiteralPath $aiConfig -PathType Leaf)){
                throw 'Play-SOR requires config/debug-ai.json or a debug-ai.json beside the executable.'
            }
        }
        $aiConfigHash=''
        if(Test-Path -LiteralPath $aiConfig -PathType Leaf){
            $aiConfigSnapshot=Join-Path $diagnosticRoot 'debug-ai.initial.json'
            Copy-Item -LiteralPath $aiConfig -Destination $aiConfigSnapshot
            $aiConfigHash=Get-DiagnosticSha256 $aiConfigSnapshot
        }else{$aiConfig=''}
        $runInfo=[ordered]@{
            executable=$exe; sha256=$binaryHash
            mpq=$Mpq; clientProfile=$ClientProfile; pipe=$PipeName; packets=$GamePacketLog
            hostPackets=$HostPacketLog; packetDirection='C2S+S2C'
            startedAtUtc=[DateTime]::UtcNow.ToString('o'); workingDirectory=$projectRoot
            frames=$frameProfile; startup=$startupProfile; movement=$movementDiffLog
            navigation=$env:D2X_NAVIGATION_DUMP
            defaultAiConfig=$aiConfig; defaultAiConfigSha256=$aiConfigHash
            hidden=[bool]$Hidden; noPauseOnFocusLoss=[bool]$NoPauseOnFocusLoss
            movementScope='Local graphical Single Player only; menu-selected remote games have no authoritative local samples.'
            aiLog='Written under ai-*/events.log when an AI plan starts; the initial config snapshot does not capture later reloads or custom config files.'
            note=$(if($ClientProfile){'D2GS only; SID/MCP authentication payloads are excluded and game tickets are redacted.'}else{'Protocol traces redact tickets and opaque save transfers; authoritative inventory remains available through the debug pipe.'})
        }
        if($SorceressScenario){
            $runInfo.scenario=$SorceressScenario
            $runInfo.saveSource=$sorceressSource
            $runInfo.save=$sorceressSave
            $runInfo.initialSaveSha256=Get-DiagnosticSha256 $sorceressSave
            if($previousSorceressRun){$runInfo.continuedFrom=$previousSorceressRun.FullName}
        }
        $runInfo | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $diagnosticRoot 'run.json') -Encoding utf8
        Write-Host "Diagnostic logs: $diagnosticRoot"
        Write-Host "Frame, navigation runtime and startup profiling enabled. AI behavior logging starts with your AI plan."
        if($movementDiffLog){Write-Host "Movement comparison enabled for local Single Player."}
        else{Write-Host "Movement comparison omitted for explicit remote/TCP startup."}
        if($SorceressScenario){
            # Start-Process joins arguments into a Windows command line.
            $quotedArguments=foreach($argument in $arguments){
                '"' + ($argument -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1') + '"'
            }
            $process=Start-Process -FilePath $exe -ArgumentList $quotedArguments -WorkingDirectory $projectRoot -PassThru `
                -RedirectStandardOutput (Join-Path $diagnosticRoot 'stdout.log') `
                -RedirectStandardError (Join-Path $diagnosticRoot 'stderr.log')
            # Windows PowerShell needs the handle cached before a redirected process exits.
            [void]$process.Handle
            $sendCommand=Join-Path $PSScriptRoot 'Send-D2XCommand.ps1'
            try{
                $ready=$false
                $startup=[Diagnostics.Stopwatch]::StartNew()
                $lastStatus='No response from the debug pipe.'
                while(-not $process.HasExited -and $startup.Elapsed.TotalSeconds -lt 90){
                    try{
                        $status=& $sendCommand -PipeName $PipeName -Command online-status -TimeoutMs 2000
                        $lastStatus="$($status.online.stage) / $($status.online.scene.reason)"
                        $ready=$status.online.stage -eq 'ProtocolReady' -and $status.online.scene.nativeMapReady -and $status.characterUi
                    }catch{$lastStatus=$_.Exception.Message}
                    if($ready){break}
                    Start-Sleep -Milliseconds 250
                }
                if(-not $ready){throw "The game did not become ready. Last status: $lastStatus"}
                if($status.characterUi.name -ne $sorceressName -or $status.characterUi.level -ne 99){
                    throw "Expected level-99 $sorceressName."
                }
                if(-not $previousSorceressRun -and $status.online.scene.area -ne $sorceressTown){
                    throw "Expected $sorceressName in starting town $sorceressTown, got area $($status.online.scene.area)."
                }
                $start=& $sendCommand -PipeName $PipeName -Command debug-ai-start -Arguments @{
                    scenario=$SorceressScenario; configFile=$aiConfigSnapshot
                }
                if($start.ai.state -ne 'running'){throw "The AI did not start: $($start.ai.reason)"}
                $start | ConvertTo-Json -Depth 45 | Set-Content -LiteralPath (Join-Path $diagnosticRoot 'sorceress-start.json') -Encoding utf8
                Write-Host "Started $SorceressScenario for $sorceressName. AI log: $($start.ai.behaviorLog.file)"
            }catch{
                if(-not $process.HasExited){[void]$process.CloseMainWindow()}
                throw "Play-SOR startup failed. $($_.Exception.Message) Logs: $diagnosticRoot"
            }
            $process.WaitForExit()
            $exitCode=$process.ExitCode
        }else{
            # Windows PowerShell treats native stderr as ErrorRecords. Cache/resource
            # notices must be recorded without aborting the interactive game process.
            $nativePreference=$ErrorActionPreference
            try{
                $ErrorActionPreference='Continue'
                & $exe @arguments 1> (Join-Path $diagnosticRoot 'stdout.log') 2> (Join-Path $diagnosticRoot 'stderr.log')
                $exitCode=$LASTEXITCODE
            }finally{$ErrorActionPreference=$nativePreference}
        }
    }else{
        & $exe @arguments
        $exitCode=$LASTEXITCODE
    }
    if($exitCode -ne 0){
        $message=if($null -eq $exitCode){'Could not read the D2X process exit code.'}
        else{"D2X exited with code $exitCode."}
        if($Diagnostic){$message+=" Diagnostic logs: $diagnosticRoot"}
        throw $message
    }
}finally{
    $env:D2X_NAVIGATION_DUMP=$previousNavigationDump
    Pop-Location
}
