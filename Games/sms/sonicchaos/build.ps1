# Regenerate C from the ROM, build the runner. Usage: ./build.ps1 [-Jobs 4]
param([int]$Jobs=4)
$ErrorActionPreference='Stop'
$env:PATH="C:\msys64\mingw64\bin;$env:PATH"
Set-Location $PSScriptRoot
$fw = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\Recomp\smsggrecomp')).Path
$rom = (Resolve-Path (Join-Path $PSScriptRoot '../../../ROMs/sms/Sonic Chaos (Europe, Brazil) (En).sms')).Path
& (Join-Path $fw 'recompiler\build\SmsRecomp.exe') $rom --game game.toml 2>&1 | Select-String 'cg_emit|error|ERROR'
& (Join-Path $fw 'tools\build_game.ps1') -ProjectRoot $PSScriptRoot -SkipRegenerate -Jobs $Jobs 2>&1 | Select-Object -Last 2
