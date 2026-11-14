[CmdletBinding(PositionalBinding=$false)]
param(
    [Parameter(Position=0)][string]$ClassOrScenario='sor',
    [Parameter(Position=1)][string]$Scenario='',
    [Parameter(Position=2)][string]$JoinScenario='',
    [string]$Mpq='',
    [string]$Lang=''
)
$ErrorActionPreference='Stop'
$lan=''
if($ClassOrScenario -eq 'join'){
    $lan='127.0.0.1'
    $ClassOrScenario=if($Scenario){$Scenario}else{'sor'}
    $Scenario=$JoinScenario
}elseif($JoinScenario){
    throw 'The third positional argument requires join before the class and scenario.'
}
$aiClass='sor'
if($ClassOrScenario -eq 'continue' -or $ClassOrScenario -match '^act[0-9]'){
    if($Scenario){throw 'Specify the class before the act range or continue.'}
    $Scenario=$ClassOrScenario
}else{
    $aiClass=$ClassOrScenario
}
if(-not $Scenario){$Scenario='act1-5'}
& (Join-Path $PSScriptRoot 'play.ps1') -Diagnostic -AiClass $aiClass -AiScenario $Scenario -Mpq $Mpq -Lang $Lang -Lan $lan
