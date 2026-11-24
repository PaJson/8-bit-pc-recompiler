# Test a release zip in Windows Sandbox (a clean Windows with no MSYS2, no compiler, no network).
#
#   Tools\sandbox_test.ps1                      newest Release\*.zip
#   Tools\sandbox_test.ps1 -Zip <file.zip>      e.g. a zip downloaded from a GitHub workflow run
#
# Writes Release\_sandbox\ (test.wsb + setup.ps1) and starts the sandbox. Inside it:
#   - the zip's folder is mapped read-only to C:\Host\Release, your ROMs\sms read-only to C:\Host\ROMs
#   - setup.ps1 unzips the release to the sandbox Desktop and opens it and C:\Host\ROMs in Explorer
#   - you then do what a player does: copy ROMs into ROMs\sms\ of the release, start Launcher.exe
# Nothing in the sandbox is kept after it is closed.
param([string]$Zip = "", [int]$MemoryMB = 8192)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $Zip) {
    $z = Get-ChildItem (Join-Path $root "Release") -Filter *.zip | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $z) { throw "no zip in Release\ (run: python -I Tools/make_release.py --zip)" }
    $Zip = $z.FullName
}
$Zip = (Resolve-Path $Zip).Path
$wsbExe = Join-Path $env:WINDIR "System32\WindowsSandbox.exe"
if (-not (Test-Path $wsbExe)) { throw "Windows Sandbox is not installed (Windows Features -> Windows Sandbox)" }

$box = Join-Path $root "Release\_sandbox"
New-Item -ItemType Directory -Force $box | Out-Null
$roms = Join-Path $root "ROMs\sms"
$zipName = Split-Path -Leaf $Zip

# runs inside the sandbox (as WDAGUtilityAccount); tar.exe (bsdtar) is part of Windows and unzips fast
@"
`$desk = [Environment]::GetFolderPath('Desktop')
`$log = Join-Path `$desk 'setup.log'
"unzipping $zipName ..." | Out-File `$log
tar.exe -xf "C:\Host\Release\$zipName" -C "`$desk" 2>&1 | Out-File -Append `$log
"done, exit code `$LASTEXITCODE" | Out-File -Append `$log
`$rel = Get-ChildItem `$desk -Directory | Where-Object { Test-Path (Join-Path `$_.FullName 'Launcher.exe') } | Select-Object -First 1
if (`$rel) { Start-Process explorer.exe `$rel.FullName }
Start-Process explorer.exe 'C:\Host\ROMs'
"@ | Set-Content -Encoding UTF8 (Join-Path $box "setup.ps1")

# the .wsb file (XML); paths are this PC's, so it is written here and not committed
$esc = { param($s) [System.Security.SecurityElement]::Escape($s) }
@"
<Configuration>
  <MemoryInMB>$MemoryMB</MemoryInMB>
  <Networking>Disable</Networking>
  <vGPU>Enable</vGPU>
  <AudioInput>Disable</AudioInput>
  <ClipboardRedirection>Enable</ClipboardRedirection>
  <MappedFolders>
    <MappedFolder>
      <HostFolder>$(& $esc (Split-Path -Parent $Zip))</HostFolder>
      <SandboxFolder>C:\Host\Release</SandboxFolder>
      <ReadOnly>true</ReadOnly>
    </MappedFolder>
    <MappedFolder>
      <HostFolder>$(& $esc $roms)</HostFolder>
      <SandboxFolder>C:\Host\ROMs</SandboxFolder>
      <ReadOnly>true</ReadOnly>
    </MappedFolder>
    <MappedFolder>
      <HostFolder>$(& $esc $box)</HostFolder>
      <SandboxFolder>C:\Host\Setup</SandboxFolder>
      <ReadOnly>true</ReadOnly>
    </MappedFolder>
  </MappedFolders>
  <LogonCommand>
    <Command>powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\Host\Setup\setup.ps1</Command>
  </LogonCommand>
</Configuration>
"@ | Set-Content -Encoding UTF8 (Join-Path $box "test.wsb")

Write-Host "Sandbox for $Zip"
Write-Host "Config: $box\test.wsb"
Start-Process (Join-Path $box "test.wsb")
