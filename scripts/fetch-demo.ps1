param([switch]$KeepSource)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$rawDirectory=Join-Path $projectRoot 'downloads/demo-mpq'
$targetDirectory=Join-Path $projectRoot 'assets/mpq'
$pack=Join-Path $targetDirectory 'd2x-mvp.mpq'
if(Test-Path -LiteralPath $pack){Write-Host "Resource pack already exists: $pack";return}
# The publicly released 1.04 demo is a self-extracting MPQ containing uncompressed
# MPQ members. Download only their byte ranges; never execute the installer.
# These hashes were recorded after the full demo matched its published MD5:
# 9ae5033551a078937cd5d1f388cd8438 (138309685 bytes).
$demoUrl='https://archive.org/download/DiabloIiDemo/DiabloIIDemo.exe'
$members=@(
    @{Name='d2char.mpq';Offset=956846L;Size=20786750L;Sha256='d3786b7dd6901197b2a163e263321f169de72fa59dd518e74b4d2466b8874cb7'},
    @{Name='d2data.mpq';Offset=21822300L;Size=44301122L;Sha256='82ed65b7f574234a22a36abb4a6d6a1e7f8bebc4746192f39cdb6603ee382d49'},
    @{Name='d2sfx.mpq';Offset=99134118L;Size=10887212L;Sha256='1652c791ca1874be8126f549c478f29810e354ee10720b5afc89a9fbef2934c4'},
    @{Name='patch_d2.mpq';Offset=135159197L;Size=1342817L;Sha256='f3dedcab99cd0fe213500f9bce0d09f675cb392de9e99ebdb7b5d956a17af7e6'}
)
New-Item -ItemType Directory -Force -Path $rawDirectory,$targetDirectory | Out-Null
foreach($member in $members){
    $path=Join-Path $rawDirectory $member.Name
    $valid=(Test-Path -LiteralPath $path) -and ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -eq $member.Sha256)
    if(-not $valid){
        $end=$member.Offset+$member.Size-1
        Write-Host "Downloading $($member.Name) ($([math]::Round($member.Size/1MB,1)) MiB)"
        & curl.exe --location --fail --retry 2 --range "$($member.Offset)-$end" --output $path $demoUrl
        if($LASTEXITCODE -ne 0){throw "Download failed: $($member.Name). Run this script again to resume completed members."}
    }
    if((Get-Item -LiteralPath $path).Length -ne $member.Size -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $member.Sha256){throw "Resource size/hash mismatch: $($member.Name). The server may not support byte ranges."}
}
$exe=Join-Path $projectRoot 'build/bin/d2x.exe'
if(-not (Test-Path -LiteralPath $exe)){$exe=Join-Path $projectRoot 'build/bin/Release/d2x.exe'}
if(-not (Test-Path -LiteralPath $exe)){& (Join-Path $PSScriptRoot 'build.ps1');$exe=Join-Path $projectRoot 'build/bin/d2x.exe';if(-not (Test-Path -LiteralPath $exe)){$exe=Join-Path $projectRoot 'build/bin/Release/d2x.exe'}}
Push-Location $projectRoot
try{
    # Loading the actual game collects resources for available preset levels, their variants,
    # characters, animations and sounds, then creates a standard compressed MPQ.
    & $exe --mpq $rawDirectory --hidden --frames 1 --pack $pack
    if($LASTEXITCODE -ne 0){throw 'Resource import failed. The downloaded MPQs have been retained.'}
}finally{Pop-Location}
if(-not $KeepSource){
    $resolvedRaw=[IO.Path]::GetFullPath($rawDirectory)
    $expectedRaw=[IO.Path]::GetFullPath((Join-Path $projectRoot 'downloads/demo-mpq'))
    if($resolvedRaw -ne $expectedRaw -or -not $resolvedRaw.StartsWith($projectRoot+[IO.Path]::DirectorySeparatorChar)){throw 'Unexpected cleanup path.'}
    Remove-Item -LiteralPath $resolvedRaw -Recurse -Force
}
Write-Host "Ready: $pack ($([math]::Round((Get-Item -LiteralPath $pack).Length/1MB,2)) MiB). No game installation required."
