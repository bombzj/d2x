param()
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$binary = Join-Path $root 'build/bin/d2x_server.exe'
if (-not (Test-Path -LiteralPath $binary)) { throw 'Build the d2x_pvpgn target before packaging.' }
$destination = Join-Path $root 'dist/server'
New-Item -ItemType Directory -Path (Join-Path $destination 'scripts') -Force | Out-Null
Copy-Item -LiteralPath $binary -Destination $destination -Force
$runtime = Join-Path $root 'build/bin/d2x_bncs_legacy.dll'
if (Test-Path -LiteralPath $runtime) { Copy-Item -LiteralPath $runtime -Destination $destination -Force }
foreach ($name in @('Start-D2XServer.ps1', 'Stop-D2XServer.ps1')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $destination 'scripts') -Force
}
foreach ($name in @('LICENSE', 'BASELINE.md')) { Copy-Item -LiteralPath (Join-Path $root $name) -Destination $destination -Force }
Copy-Item -LiteralPath (Join-Path $root 'docs/development/PVPGN_SERVER.md') -Destination (Join-Path $destination 'README.md') -Force
Copy-Item -LiteralPath (Join-Path $root 'docs/licenses') -Destination $destination -Recurse -Force
$config = Join-Path $destination 'server.json'
if (-not (Test-Path -LiteralPath $config)) {
    Copy-Item -LiteralPath (Join-Path $root 'docs/development/pvpgn-server.example.json') -Destination $config
}
Write-Host "Updated $destination. Existing configuration, recovery files and original PvPGN scripts were not modified."