param(
    [ValidateSet('Release','Debug')][string]$Configuration='Release',
    [ValidateSet('d2x','d2x_pvpgn','d2x_assets','all')][string]$Target='d2x'
)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function Get-BuildInputState {
    $state=@{}
    $files=@(Get-Item -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt'))
    foreach($directory in @('src','cmake')){
        $path=Join-Path $projectRoot $directory
        if(Test-Path -LiteralPath $path){$files+=@(Get-ChildItem -LiteralPath $path -Recurse -File)}
    }
    foreach($file in $files){
        $state[$file.FullName]="$($file.LastWriteTimeUtc.Ticks):$($file.Length)"
    }
    return $state
}
Push-Location $projectRoot
try {
    if(-not (Get-Command cmake -ErrorAction SilentlyContinue)){throw 'CMake 3.25+ is required.'}
    $inputsBefore=Get-BuildInputState
    $configureArgs=@('-S','.', '-B','build', "-DCMAKE_BUILD_TYPE=$Configuration")
    if(-not (Test-Path 'build/CMakeCache.txt')){
        if((Get-Command ninja -ErrorAction SilentlyContinue) -and (Get-Command g++ -ErrorAction SilentlyContinue)){$configureArgs+=@('-G','Ninja')}
    }
    & cmake @configureArgs
    if($LASTEXITCODE -ne 0){throw 'CMake configuration failed.'}
    & cmake --build build --config $Configuration --target $Target --parallel 4
    $buildExitCode=$LASTEXITCODE
    $inputsAfter=Get-BuildInputState
    $changed=@(@($inputsBefore.Keys)+@($inputsAfter.Keys) | Sort-Object -Unique | Where-Object {
        $inputsBefore[$_] -ne $inputsAfter[$_]
    })
    if($changed.Count -gt 0){
        # A compiler can read an old header and finish AFTER the edited header's
        # timestamp. Retouch after compilation so the next build cannot reuse it.
        foreach($path in $changed){
            if(Test-Path -LiteralPath $path){
                (Get-Item -LiteralPath $path).LastWriteTimeUtc=[DateTime]::UtcNow
            }
        }
        throw "Source files changed during compilation. Do not run this build; finish editing and rerun build.ps1. Changed inputs:`n$($changed -join "`n")"
    }
    if($buildExitCode -ne 0){throw 'C++ build failed.'}
    Write-Host "Build complete: $Target ($Configuration)."
} finally {Pop-Location}
