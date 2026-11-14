# Build the launcher: ./build.ps1   (then run Launcher.ps1 in the project root, or Launcher/build/Launcher.exe)
$ErrorActionPreference = 'Stop'
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
Set-Location $PSScriptRoot
& cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe `
        -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/ninja.exe -DCMAKE_PREFIX_PATH=C:/msys64/mingw64 | Out-Null
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
& cmake --build build
if ($LASTEXITCODE -ne 0) { throw "build failed" }
