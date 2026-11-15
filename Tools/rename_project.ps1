# rename_project.ps1 - rename the project folder and repair everything that stores its absolute path.
#
# Run it from a normal PowerShell window, NOT from inside VS Code / Claude Code, after closing VS Code,
# the launcher and every game (Windows refuses to rename a folder that a program has open):
#   powershell -NoProfile -ExecutionPolicy Bypass -File "<project folder>\Tools\rename_project.ps1"
# Options:
#   -NewName "<name>"   new folder name (default "SEGA Master System PC Re-compiler"; same parent folder)
#   -Rebuild            also rebuild every game now (long: the paths change, so everything recompiles;
#                       without it each game recompiles on its next build, e.g. R in the launcher)
#   -RepairOnly -Root <folder>   skip the rename, only repair <folder> (e.g. after renaming by hand)
#
# What it does:
#   1. renames the folder (nothing else is touched if that fails);
#   2. copies Claude Code's data for the project (memory + conversations, %USERPROFILE%\.claude\projects\
#      <path with every non-alphanumeric character as '-'>) to the new path's name; the old copy stays;
#   3. every CMake build folder (CMakeCache.txt) gets the new path in its cache, a fresh CMakeFiles/
#      and ninja state, and is configured again with its own settings (Release, -O1, options ...).
# Git repositories, sources, saves, ROMs and ini files contain no absolute paths and need nothing.
param(
    [string]$NewName = 'SEGA Master System PC Re-compiler',
    [switch]$Rebuild,
    [switch]$RepairOnly,
    [string]$Root
)
$ErrorActionPreference = 'Stop'
$cmake = 'C:\msys64\mingw64\bin\cmake.exe'
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"

function Slug([string]$p) { return ($p -replace '[^A-Za-z0-9]', '-') }

# ---------- 1. rename ----------
if ($RepairOnly) {
    if (-not $Root) { throw '-RepairOnly needs -Root <project folder>' }
    $new = (Resolve-Path -LiteralPath $Root).Path
    $old = $null
} else {
    $old = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
    $parent = [IO.Path]::GetDirectoryName($old)
    $new = Join-Path $parent $NewName
    if ($old -eq $new) { throw "The folder is already called '$NewName'." }
    if (Test-Path -LiteralPath $new) { throw "'$new' already exists." }
    $busy = Get-Process | Where-Object { $_.Path -and $_.Path.StartsWith($old + '\', [StringComparison]::OrdinalIgnoreCase) }
    if ($busy) {
        $busy | Format-Table Name, Id, Path -AutoSize | Out-String | Write-Host
        throw 'Close these programs first (launcher, games, ...).'
    }
    $code = Get-Process -Name Code -ErrorAction SilentlyContinue
    if ($code) { Write-Host 'Note: VS Code is running. If it has this folder open, the rename will fail: close it and run this again.' -ForegroundColor Yellow }
    Set-Location -LiteralPath $parent
    [Environment]::CurrentDirectory = $parent        # this process must not hold the folder either
    try { Rename-Item -LiteralPath $old -NewName $NewName }
    catch { throw "Rename failed (a program still has the folder or a file in it open: VS Code, Explorer, a terminal, ...): $($_.Exception.Message)" }
    Write-Host "Renamed: $old" -ForegroundColor Green
    Write-Host "     ->  $new" -ForegroundColor Green
}

# ---------- 3a. find the build folders and the old path they were made in ----------
$caches = Get-ChildItem -LiteralPath $new -Recurse -Filter CMakeCache.txt -File -ErrorAction SilentlyContinue |
          Where-Object { $_.FullName -notmatch '\\_template_sonic\\' }
foreach ($c in $caches) {
    $line = Select-String -LiteralPath $c.FullName -Pattern '^CMAKE_CACHEFILE_DIR:INTERNAL=(.*)$' | Select-Object -First 1
    if (-not $line) { continue }
    $cachedDir = $line.Matches[0].Groups[1].Value.Replace('/', '\')
    $rel = $c.DirectoryName.Substring($new.Length)                     # e.g. \Games\ps-reva\build
    if ($cachedDir.EndsWith($rel, [StringComparison]::OrdinalIgnoreCase)) {
        $guess = $cachedDir.Substring(0, $cachedDir.Length - $rel.Length)
        if (-not $old) { $old = $guess }
    }
}
if (-not $old) { Write-Host 'No build folder points to another path: nothing to repair.'; }

# ---------- 2. Claude Code data ----------
if ($old) {
    $projDir = Join-Path $env:USERPROFILE '.claude\projects'
    if (Test-Path -LiteralPath $projDir) {
        $oldSlug = Slug $old
        $src = Get-ChildItem -LiteralPath $projDir -Directory | Where-Object { $_.Name -ieq $oldSlug } | Select-Object -First 1
        if ($src) {
            $newSlug = Slug $new
            if ($src.Name[0] -cne $newSlug[0]) { $newSlug = $src.Name.Substring(0, 1) + $newSlug.Substring(1) }   # keep the drive letter's case
            $dst = Join-Path $projDir $newSlug
            if (Test-Path -LiteralPath $dst) { Write-Host "Claude Code data already exists for the new path: $dst (left as it is)" -ForegroundColor Yellow }
            else {
                Copy-Item -LiteralPath $src.FullName -Destination $dst -Recurse
                Write-Host "Claude Code data (memory, conversations) copied to: $dst" -ForegroundColor Green
            }
        } else { Write-Host "No Claude Code data found for $old" -ForegroundColor Yellow }
    }
}

# ---------- 3b. repair and reconfigure every build folder ----------
if ($old) {
    # the same repair runs by itself before every game / launcher build (build_game.ps1, Launcher\build.ps1),
    # so this step is only needed for the folders built by hand (recompiler, diagnostic builds) or to do all now
    $repair = Join-Path $new 'Recomp\smsggrecomp\tools\repair_moved_build.ps1'
    $done = @()
    foreach ($c in $caches) {
        $bdir = $c.DirectoryName
        $line = Select-String -LiteralPath $c.FullName -Pattern '^CMAKE_CACHEFILE_DIR:INTERNAL=(.*)$' | Select-Object -First 1
        if (-not $line -or $line.Matches[0].Groups[1].Value.Replace('/', '\').TrimEnd('\') -ieq $bdir) { continue }
        Write-Host "configure  $($bdir.Substring($new.Length + 1))"
        & $repair -Build $bdir -Configure -Cmake $cmake | Out-Null
        if ($LASTEXITCODE -ne 0) { Write-Host "   FAILED: see $bdir\reconfigure_log.txt" -ForegroundColor Red; continue }
        $done += $bdir
    }
    Write-Host "$($done.Count) build folder(s) repaired." -ForegroundColor Green
    if ($Rebuild) {
        foreach ($b in $done) {
            Write-Host "build      $($b.Substring($new.Length + 1)) ..."
            & $cmake --build $b *> (Join-Path $b 'rebuild_log.txt')
            if ($LASTEXITCODE -ne 0) { Write-Host "   FAILED: see $b\rebuild_log.txt" -ForegroundColor Red }
        }
    }
}

Write-Host ''
Write-Host 'Next: open the new folder in VS Code. In Claude Code, /resume lists the earlier conversations.'
Write-Host 'Existing game exes keep working; each game recompiles fully on its next build (paths changed).'
if (-not $RepairOnly) { Write-Host "When everything works, the old Claude Code data folder can be deleted: $(Join-Path $env:USERPROFILE '.claude\projects')\$(Slug $old)" }
