# Play. ./play.ps1 [-Scale 3] [-Speed 1.0]   Keys: arrows, Z/X = buttons, Enter = Start, Tab = x4 turbo, Esc = quit
param([int]$Scale = 3, [double]$Speed = 1.0)
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force saves | Out-Null
$env:SMS_SAVE_DIR = (Resolve-Path saves).Path
& .\build\PsJaSmspowerRecomp.exe (Resolve-Path '../../ROMs/Patched/Phantasy Star (Japan) [SMSPower Retranslation 2.01 New Sprites].sms').Path --window $Scale --speed $Speed
