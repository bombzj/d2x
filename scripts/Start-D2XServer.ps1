param(
    [string]$Config = '',
    [string]$Executable = ''
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $Config) {
    $Config = if (Test-Path (Join-Path $root 'server.json')) { Join-Path $root 'server.json' } else { Join-Path $root 'docs/development/pvpgn-server.example.json' }
}
if (-not $Executable) {
    $Executable = if (Test-Path (Join-Path $root 'd2x_server.exe')) { Join-Path $root 'd2x_server.exe' } else { Join-Path $root 'build/bin/d2x_server.exe' }
}
$Config = (Resolve-Path -LiteralPath $Config).Path
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$settings = Get-Content -LiteralPath $Config -Raw | ConvertFrom-Json
$stopFile = [string]$settings.stopFile
if (-not [IO.Path]::IsPathRooted($stopFile)) { $stopFile = Join-Path (Split-Path $Config -Parent) $stopFile }
$stopFile = [IO.Path]::GetFullPath($stopFile)
$pidFile = "$stopFile.process.json"
if (Test-Path -LiteralPath $pidFile) { throw "Process record already exists: $pidFile. Stop or inspect the previous server first." }
if (Test-Path -LiteralPath $stopFile) { throw "Stale stop request exists: $stopFile. Inspect the previous shutdown before removing it." }
New-Item -ItemType Directory -Path (Split-Path $stopFile -Parent) -Force | Out-Null
$stdout = "$stopFile.stdout.log"
$stderr = "$stopFile.stderr.log"
$process = Start-Process -FilePath $Executable -ArgumentList @('--config', ('"{0}"' -f $Config)) -WorkingDirectory $root -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
try {
    [ordered]@{ pid = $process.Id; started = $process.StartTime.ToUniversalTime().ToString('o'); executable = $Executable; config = $Config } |
        ConvertTo-Json | Set-Content -LiteralPath $pidFile -Encoding utf8
} catch { throw "Server was launched but its process record could not be written. Inspect process $($process.Id) and request shutdown through $stopFile. $($_.Exception.Message)" }
Write-Host "Started D2X server process $($process.Id). Registration status: $stdout; errors: $stderr."
Write-Host "Stop with scripts/Stop-D2XServer.ps1 -Config `"$Config`". PvPGN processes were not changed."
