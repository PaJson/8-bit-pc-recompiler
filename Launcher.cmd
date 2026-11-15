@echo off
rem Start the game launcher. Double-click this file.
rem First use: builds it (Launcher\build.ps1). Later: a quick incremental rebuild, so changes to the
rem launcher sources are picked up (if that fails, e.g. while it is running, the existing exe starts).
rem After the project folder was moved or renamed, build.ps1 repairs Launcher\build first (the quick
rem build would re-run CMake with the old paths).
setlocal
cd /d "%~dp0"
if not exist "Launcher\build\Launcher.exe" goto full
if not exist "C:\msys64\mingw64\bin\cmake.exe" goto run
set "PATH=C:\msys64\mingw64\bin;%PATH%"
set "HERE=%CD:\=/%"
findstr /i /b /c:"CMAKE_CACHEFILE_DIR:INTERNAL=%HERE%/Launcher/build" "Launcher\build\CMakeCache.txt" >nul 2>&1 || goto moved
cmake --build "Launcher\build" >nul 2>&1
goto run
:moved
powershell -NoProfile -ExecutionPolicy Bypass -File "Launcher\build.ps1" >nul 2>&1
goto run
:full
echo Building the launcher ...
powershell -NoProfile -ExecutionPolicy Bypass -File "Launcher\build.ps1" || ( pause & exit /b 1 )
:run
start "" "Launcher\build\Launcher.exe"
