param([string]$Mpq='',[int]$Level=1,[int]$Region=-1)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$exe=Join-Path $projectRoot 'build/bin/d2x.exe'
if(-not (Test-Path -LiteralPath $exe)){$exe=Join-Path $projectRoot 'build/bin/Release/d2x.exe'}
if(-not (Test-Path -LiteralPath $exe)){& (Join-Path $PSScriptRoot 'build.ps1');$exe=Join-Path $projectRoot 'build/bin/d2x.exe';if(-not (Test-Path -LiteralPath $exe)){$exe=Join-Path $projectRoot 'build/bin/Release/d2x.exe'}}
if(-not $Mpq){
    $fullPack=Join-Path $projectRoot 'assets/mpq2/d2x-act1.mpq'
    $fullSource=Join-Path $projectRoot 'assets/mpq2'
    if(Test-Path -LiteralPath (Join-Path $fullSource 'd2data.mpq')){$Mpq=$fullSource}
    elseif(Test-Path -LiteralPath $fullPack){$Mpq=$fullPack}
    elseif(-not (Get-ChildItem -LiteralPath (Join-Path $projectRoot 'assets/mpq') -Filter '*.mpq' -ErrorAction SilentlyContinue)){& (Join-Path $PSScriptRoot 'fetch-demo.ps1')}
}
Push-Location $projectRoot
try{$arguments=@('--level',"$Level",'--region',"$Region");if($Mpq){$arguments+=@('--mpq',$Mpq)};& $exe @arguments;if($LASTEXITCODE -ne 0){throw 'D2X could not start. See the error above.'}}finally{Pop-Location}
