param([string]$Config = '', [int]$TimeoutSeconds = 120)
$ErrorActionPreference = 'Stop'
if ($TimeoutSeconds -lt 1) { throw 'TimeoutSeconds must be positive.' }
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $Config) {
    $Config = if (Test-Path (Join-Path $root 'server.json')) { Join-Path $root 'server.json' } else { Join-Path $root 'docs/development/pvpgn-server.example.json' }
}
$Config = (Resolve-Path -LiteralPath $Config).Path
$settings = Get-Content -LiteralPath $Config -Raw | ConvertFrom-Json
$stopFile = [string]$settings.stopFile
if (-not [IO.Path]::IsPathRooted($stopFile)) { $stopFile = Join-Path (Split-Path $Config -Parent) $stopFile }
$stopFile = [IO.Path]::GetFullPath($stopFile)
$pidFile = "$stopFile.process.json"
$record = Get-Content -LiteralPath $pidFile -Raw | ConvertFrom-Json
$process = Get-Process -Id $record.pid -ErrorAction SilentlyContinue
if (-not $process) { throw "Recorded server is no longer running. Inspect its console/recovery files before removing $pidFile." }
if ($process.Path -ne $record.executable -or $process.StartTime.ToUniversalTime().Ticks -ne ([DateTimeOffset]$record.started).UtcDateTime.Ticks -or $record.config -ne $Config) {
    throw 'Process identity mismatch; no stop request was sent.'
}
$null = $process.Handle
[IO.File]::WriteAllText($stopFile, 'stop')
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) { throw 'Graceful shutdown is still pending. No process was killed; inspect the server console.' }
if ($process.ExitCode -ne 0 -or (Test-Path -LiteralPath $stopFile)) { throw 'Server did not confirm a clean shutdown. Recovery files and process record were retained.' }
Remove-Item -LiteralPath $pidFile
Write-Host 'D2X server stopped after acknowledged saves. PvPGN processes were not changed.'
