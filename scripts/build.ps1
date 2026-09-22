param([ValidateSet('Release','Debug')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Push-Location $projectRoot
try {
    if(-not (Get-Command cmake -ErrorAction SilentlyContinue)){throw 'CMake 3.25+ is required.'}
    $configureArgs=@('-S','.', '-B','build', "-DCMAKE_BUILD_TYPE=$Configuration")
    if(-not (Test-Path 'build/CMakeCache.txt')){
        if((Get-Command ninja -ErrorAction SilentlyContinue) -and (Get-Command g++ -ErrorAction SilentlyContinue)){$configureArgs+=@('-G','Ninja')}
    }
    & cmake @configureArgs
    if($LASTEXITCODE -ne 0){throw 'CMake configuration failed.'}
    & cmake --build build --config $Configuration --parallel 8
    if($LASTEXITCODE -ne 0){throw 'C++ build failed.'}
    Write-Host 'Build complete. Run Play.cmd.'
} finally {Pop-Location}
