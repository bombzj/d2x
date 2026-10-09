[CmdletBinding()]
param(
    [ValidatePattern('^[A-Za-z0-9_-]{1,80}$')][string]$PipeName = 'd2x-debug',
    [Parameter(Mandatory = $true)]
    [ValidateSet(
        'status', 'pause', 'resume',
        'server-status', 'server-protocol', 'server-commands', 'server-systems', 'server-snapshot', 'server-events',
        'server-pause', 'server-resume', 'server-auto-pause', 'refill-resources',
        'save', 'load', 'cancel-load', 'step',
        'grant-gold', 'player-damage', 'missile-hit', 'grant-experience', 'item-spawn', 'monster-spawn',
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
        'online-resurrect', 'online-recover-corpse',
        'online-npc-message', 'online-npc-close', 'online-npc-travel', 'online-npc-respec',
        'online-items', 'online-ground', 'online-item-action', 'online-item-quote',
        'online-social', 'online-chat', 'online-send-chat', 'online-trade-respond', 'online-trade-offer',
        'online-combat', 'online-skills', 'online-select-skill', 'online-cast', 'online-attack',
        'online-stop-skill', 'online-learn-skill', 'online-spend-attribute', 'online-bind-hotkey',
        'ui-input', 'screenshot', 'quit')]
    [string]$Command,
    [hashtable]$Arguments = @{},
    [ValidateRange(100, 60000)][int]$TimeoutMs = 10000
)
$ErrorActionPreference = 'Stop'
$request = @{ command = $Command }
foreach ($key in $Arguments.Keys) {
    if ($key -eq 'command') { throw 'Arguments cannot replace command' }
    $request[$key] = $Arguments[$key]
}
$pipe = [System.IO.Pipes.NamedPipeClientStream]::new('.', $PipeName, [System.IO.Pipes.PipeDirection]::InOut, [System.IO.Pipes.PipeOptions]::Asynchronous)
$cancellation = [System.Threading.CancellationTokenSource]::new($TimeoutMs)
try {
    $pipe.Connect($TimeoutMs)
    $bytes = [System.Text.Encoding]::UTF8.GetBytes(($request | ConvertTo-Json -Compress -Depth 10) + "`n")
    if ($bytes.Length -gt 16384) { throw 'Request exceeds 16 KiB' }
    $pipe.WriteAsync($bytes, 0, $bytes.Length, $cancellation.Token).GetAwaiter().GetResult() | Out-Null
    $stream = [System.IO.MemoryStream]::new()
    try {
        $buffer = [byte[]]::new(4096)
        $finished = $false
        while (-not $finished) {
            $count = $pipe.ReadAsync($buffer, 0, $buffer.Length, $cancellation.Token).GetAwaiter().GetResult()
            if ($count -eq 0) { throw 'Pipe closed before response' }
            for ($index = 0; $index -lt $count; $index++) {
                if ($buffer[$index] -eq 10) { $finished = $true; break }
                $stream.WriteByte($buffer[$index])
            }
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
