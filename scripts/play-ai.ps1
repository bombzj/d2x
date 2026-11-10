[CmdletBinding(PositionalBinding=$false)]
param(
    [Parameter(Position=0)][string]$ClassOrScenario='sor',
    [Parameter(Position=1)][string]$Scenario='',
    [string]$Mpq='',
    [string]$Lang=''
)
$ErrorActionPreference='Stop'
$aiClass='sor'
if($ClassOrScenario -eq 'continue' -or $ClassOrScenario -match '^act[0-9]'){
    if($Scenario){throw 'Specify the class before the act range or continue.'}
    $Scenario=$ClassOrScenario
}else{
    $aiClass=$ClassOrScenario
}
if(-not $Scenario){$Scenario='act1-5'}
& (Join-Path $PSScriptRoot 'play.ps1') -Diagnostic -AiClass $aiClass -AiScenario $Scenario -Mpq $Mpq -Lang $Lang
