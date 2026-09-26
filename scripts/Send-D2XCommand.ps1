[CmdletBinding()]
param(
    [ValidatePattern('^[A-Za-z0-9_-]{1,80}$')][string]$PipeName = 'd2x-debug',
    [Parameter(Mandatory = $true)]
    [ValidateSet(
        'status', 'quest-status', 'monsters', 'monster-spawn', 'monster-damage', 'monster-kill',
        'grant-hireling', 'grant_hireling', 'hireling', 'hireling-panel', 'hireling-equip',
        'ground', 'inventory', 'item', 'item-spawn', 'item-move', 'objects', 'exits', 'view',
        'equip', 'use', 'portal', 'interact', 'talk', 'gossip', 'identify', 'identify-item', 'book-load', 'shop', 'buy',
        'grant-gold', 'gold-transfer', 'cube-drop', 'cube-open', 'grant-experience', 'grant-shrine', 'unlock-waypoints', 'allocate-attribute', 'reset-attributes',
        'character-panel', 'quest-panel', 'skill-tree', 'skill-picker', 'skills',
        'bind-skill-hotkey', 'learn-skill', 'cast-skill', 'stop-channel', 'reset-skills', 'travel', 'waypoint',
        'kill', 'drop', 'pickup', 'move', 'step', 'pause', 'resume', 'save', 'load',
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
    $pipe.WriteAsync($bytes, 0, $bytes.Length, $cancellation.Token).GetAwaiter().GetResult()
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
