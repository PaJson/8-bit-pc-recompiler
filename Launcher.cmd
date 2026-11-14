@echo off
rem Start the game launcher. Double-click this file.
rem First use: builds it (Launcher\build.ps1). Later: a quick incremental rebuild, so changes to the
rem launcher sources are picked up (if that fails, e.g. while it is running, the existing exe starts).
cd /d "%~dp0"
if not exist "Launcher\build\Launcher.exe" (
  echo Building the launcher ...
  powershell -NoProfile -ExecutionPolicy Bypass -File "Launcher\build.ps1" || ( pause & exit /b 1 )
) else if exist "C:\msys64\mingw64\bin\cmake.exe" (
  set "PATH=C:\msys64\mingw64\bin;%PATH%"
  cmake --build "Launcher\build" >nul 2>&1
)
start "" "Launcher\build\Launcher.exe"
