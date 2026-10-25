param(
    [Parameter(Mandatory=$true)]
    [ValidateSet('Start','Status','Stop')][string]$Action,
    [string]$LabRoot=(Join-Path $PSScriptRoot '../artifacts/d2gs-local/pd2-native-realm-lab')
)
$ErrorActionPreference='Stop'
$LabRoot=[IO.Path]::GetFullPath($LabRoot)
$recordPath=Join-Path $LabRoot 'backend-processes.local.json'
$components=@(
    @{Name='bnetd';Port=16112},
    @{Name='d2dbs';Port=16114},
    @{Name='d2cs';Port=16113}
)
function Get-OwnedProcess($Record) {
    $process=Get-Process -Id $Record.id -ErrorAction SilentlyContinue
    if(-not $process){return $null}
    if($process.Path -ne $Record.path -or $process.StartTime.ToUniversalTime().Ticks -ne [long]$Record.startedTicks){
        throw "Process identity changed for $($Record.name); refusing to manage it."
    }
    return $process
}
$records=@()
if(Test-Path -LiteralPath $recordPath){
    $records=@(Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json)
    foreach($record in $records){
        if($record.name -notin $components.Name -or $record.path -ne [IO.Path]::GetFullPath((Join-Path $LabRoot "candidate-backend/$($record.name).exe"))){
            throw 'Invalid backend process record.'
        }
    }
}
if($Action -eq 'Status'){
    foreach($component in $components){
        $record=$records | Where-Object name -eq $component.Name
        $process=if($record){Get-OwnedProcess $record}else{$null}
        $listeners=@(Get-NetTCPConnection -State Listen -LocalPort $component.Port -ErrorAction SilentlyContinue)
        [pscustomobject]@{
            Component=$component.Name
            Port=$component.Port
            OwnedProcess=$(if($process){$process.Id}else{$null})
            ListeningProcess=($listeners.OwningProcess -join ',')
        }
    }
    Write-Host 'Backend status only. The native PD2 game engine is managed separately.'
    return
}
if($Action -eq 'Stop'){
    $owned=@($records | ForEach-Object {Get-OwnedProcess $_} | Where-Object {$_})
    $connections=@(Get-NetTCPConnection -State Established -ErrorAction SilentlyContinue |
        Where-Object {$_.LocalPort -in $components.Port})
    foreach($connection in $connections){
        $peer=@(Get-NetTCPConnection -State Established -ErrorAction SilentlyContinue |
            Where-Object {$_.LocalPort -eq $connection.RemotePort -and $_.RemotePort -eq $connection.LocalPort -and $_.OwningProcess -in $owned.Id})
        if(-not $peer){throw 'A client or game server is still connected. Log out clients and save/stop the game server first.'}
    }
    foreach($name in @('d2cs','d2dbs','bnetd')){
        $record=$records | Where-Object name -eq $name
        if($record){
            $process=Get-OwnedProcess $record
            if($process){
                Stop-Process -InputObject $process
                if(-not $process.WaitForExit(10000)){throw "Backend $name did not stop."}
            }
        }
    }
    if(Test-Path -LiteralPath $recordPath){Remove-Item -LiteralPath $recordPath}
    Write-Host 'Owned backends stopped. No game engine was managed.'
    return
}
foreach($record in $records){
    if(Get-OwnedProcess $record){throw 'Owned backends already exist. Use Status or Stop first.'}
}
foreach($component in $components){
    foreach($relative in @("candidate-backend/$($component.Name).exe","backend/conf/$($component.Name).conf")){
        if(-not (Test-Path -LiteralPath (Join-Path $LabRoot $relative) -PathType Leaf)){throw "Missing $relative"}
    }
}
$ports=@($components.Port)+16200
if(Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue | Where-Object {$_.LocalPort -in $ports}){
    throw 'An experimental backend port is occupied. No process was started or stopped.'
}
$runDirectory=Join-Path $LabRoot ('logs/backend-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $runDirectory | Out-Null
$records=@()
foreach($component in $components){
    $name=$component.Name
    $exe=[IO.Path]::GetFullPath((Join-Path $LabRoot "candidate-backend/$name.exe"))
    $process=Start-Process -FilePath $exe -ArgumentList @('-f','-c',"conf/$name.conf") -WorkingDirectory (Join-Path $LabRoot 'backend') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $runDirectory "$name.stdout.log") -RedirectStandardError (Join-Path $runDirectory "$name.stderr.log")
    $records+=@{name=$name;id=$process.Id;path=$exe;startedTicks=$process.StartTime.ToUniversalTime().Ticks}
    ConvertTo-Json -InputObject $records | Set-Content -LiteralPath $recordPath -Encoding utf8
}
Write-Host "Created backend processes. Logs: $runDirectory"
Write-Host 'Use Status and backend/var/d2cs.log to confirm listeners and Realm authentication. No PD2 game engine is started.'
