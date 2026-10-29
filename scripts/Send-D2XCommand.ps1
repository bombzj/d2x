[CmdletBinding()]
param(
    [ValidatePattern('^[A-Za-z0-9_-]{1,80}$')][string]$PipeName = 'd2x-debug',
    [Parameter(Mandatory = $true)]
    [ValidateSet(
        'status', 'pause', 'resume',
        'headless-status', 'headless-start', 'headless-input',
        'server-status', 'server-protocol', 'server-commands', 'server-systems', 'server-snapshot', 'server-events',
        'server-pause', 'server-resume', 'server-auto-pause', 'server-cancel-step', 'refill-resources',
        'save', 'load', 'cancel-load', 'step',
        'grant-gold', 'set-players', 'player-damage', 'missile-hit', 'grant-experience', 'item-spawn', 'monster-spawn', 'object-spawn',
        'monster-damage', 'monster-kill', 'travel', 'unlock-waypoints',
        'grant-shrine', 'grant-hireling', 'reset-attributes', 'reset-skills',
        'online-status', 'online-login', 'online-realms', 'online-select-realm',
        'online-characters', 'online-select-character', 'online-games', 'online-list-games',
        'online-create-game', 'online-join-game', 'online-game-info', 'online-leave-game', 'online-return-characters',
        'online-cancel', 'online-logout',
        'online-register', 'online-create-character', 'online-delete-character',
        'online-return-realms', 'online-cancel-list',
        'online-world', 'online-move', 'online-use-exit', 'online-interact',
        'online-waypoint-travel', 'online-waypoint-close', 'online-automap',
        'online-move-to-unit', 'online-town-portal', 'online-npc-interact',
        'debug-walk', 'debug-walk-status', 'debug-walk-cancel',
        'online-resurrect', 'online-recover-corpse',
        'online-npc-message', 'online-npc-close', 'online-npc-travel', 'online-npc-respec', 'online-staff-update',
        'online-items', 'online-ground', 'online-item-action', 'online-item-quote',
        'online-social', 'online-chat', 'online-chat-input', 'online-send-chat', 'online-chat-relation', 'online-party-action', 'online-trade-respond', 'online-trade-offer',
        'online-combat', 'online-skills', 'online-select-skill', 'online-cast', 'online-attack',
        'online-stop-skill', 'online-learn-skill', 'online-spend-attribute', 'online-bind-hotkey',
        'ui-input', 'screenshot', 'quit')]
    [string]$Command,
    [hashtable]$Arguments = @{},
    [ValidateRange(100, 60000)][int]$TimeoutMs = 10000,
    [switch]$NoWait
)
$ErrorActionPreference = 'Stop'
$request = @{ command = $Command }
foreach ($key in $Arguments.Keys) {
    if ($key -eq 'command') { throw 'Arguments cannot replace command' }
    $request[$key] = $Arguments[$key]
}
function Send-D2XRequest([hashtable]$Payload) {
    $pipe = [System.IO.Pipes.NamedPipeClientStream]::new('.', $PipeName, [System.IO.Pipes.PipeDirection]::InOut, [System.IO.Pipes.PipeOptions]::Asynchronous)
    $cancellation = [System.Threading.CancellationTokenSource]::new($TimeoutMs)
    try {
        $pipe.Connect($TimeoutMs)
        $bytes = [System.Text.Encoding]::UTF8.GetBytes(($Payload | ConvertTo-Json -Compress -Depth 10) + "`n")
        if ($bytes.Length -gt 16384) { throw 'Request exceeds 16 KiB' }
        $pipe.WriteAsync($bytes, 0, $bytes.Length, $cancellation.Token).GetAwaiter().GetResult() | Out-Null
        $stream = [System.IO.MemoryStream]::new()
        try {
            $buffer = [byte[]]::new(4096)
            $finished = $false
            while (-not $finished) {
                $count = $pipe.ReadAsync($buffer, 0, $buffer.Length, $cancellation.Token).GetAwaiter().GetResult()
                if ($count -eq 0) { throw 'Pipe closed before response' }
                $end = [Array]::IndexOf($buffer, [byte]10, 0, $count)
                $length = if ($end -ge 0) { $end } else { $count }
                $stream.Write($buffer, 0, $length)
                $finished = $end -ge 0
                if ($stream.Length -gt 4194304) { throw 'Response exceeds 4 MiB' }
            }
            $response = [System.Text.Encoding]::UTF8.GetString($stream.ToArray()) | ConvertFrom-Json
            if (-not $response.ok) { throw $response.error }
            $response
        } finally { $stream.Dispose() }
    } finally {
        $cancellation.Dispose()
        $pipe.Dispose()
    }
}
$response = Send-D2XRequest $request
if ($Command -ne 'step' -or $NoWait -or -not $response.server.fixedSteps) {
    $response
    return
}
# A step request is submitted exactly once. Observe its generation-bound job;
# the game pumps native packets and heartbeats between individual frames.
$job = $response.server.fixedSteps
$sequence = $job.sequence
$target = @{
    hostSlot = $response.server.hostSlot
    hostGeneration = $response.server.hostGeneration
    hostPlayer = $response.server.hostPlayer
}
$observe = @{command='server-status'}
$cancel = @{command='server-cancel-step'; amount=$sequence}
foreach ($key in $target.Keys) { $observe[$key]=$target[$key]; $cancel[$key]=$target[$key] }
$pending = $job.pending
$progress = $job.completed
$watch = [Diagnostics.Stopwatch]::StartNew()
try {
    while ($pending) {
        Start-Sleep -Milliseconds 20
        $response = Send-D2XRequest $observe
        $job = $response.server.fixedSteps
        if (-not $job -or $job.sequence -ne $sequence) { throw "Fixed-step job $sequence expired or was replaced; do not resubmit the step request." }
        $pending = $job.pending
        if ($job.cancelled) { throw "Fixed-step job $sequence was cancelled after $($job.completed)/$($job.requested) completed frames." }
        if ($job.completed -ne $progress) { $progress=$job.completed; $watch.Restart() }
        if ($pending -and $watch.ElapsedMilliseconds -ge $TimeoutMs) {
            throw "Fixed-step job $sequence stopped progressing at $progress/$($job.requested) frames (blocked=$($job.blocked))."
        }
    }
    if ($job.completed -ne $job.requested) { throw 'Fixed-step completion count does not match the request.' }
    $response | Add-Member -NotePropertyName status -NotePropertyValue 'applied' -Force
    $response | Add-Member -NotePropertyName message -NotePropertyValue "Authority advanced by $($job.completed) fixed frames" -Force
    $response
} finally {
    $watch.Stop()
    if ($pending) {
        try { Send-D2XRequest $cancel | Out-Null }
        catch { Write-Warning "Could not cancel fixed-step job $sequence; inspect server-status before further mutations: $_" }
    }
}
