# Headless soak test: ./check.ps1 [-Frames 20000] [-Press <press file>]  -> prints dispatch misses
param([int]$Frames=20000, [string]$Press='')
$ErrorActionPreference='Stop'
$env:SMS_NO_SAVE=1
Set-Location $PSScriptRoot
$rom = (Resolve-Path '../../../ROMs/sms/Patched/Golvellius - Valley of Doom (USA, Europe, Brazil) (En) [Golvellius Plus].sms').Path
$args2 = @($rom,'--frames',$Frames,'--report','report.json')
if($Press){ $args2 += @('--press-file',(Resolve-Path $Press).Path) }
Set-Location build; Remove-Item dispatch_misses.log -ErrorAction SilentlyContinue
& .\GolvelliusPlusRecomp.exe @args2 2>&1 | Select-String 'miss|exec\]|timing|stopped|does not match'
"--- dispatch_misses.log:"; Get-Content dispatch_misses.log -ErrorAction SilentlyContinue
