@echo off
rem Start the game launcher (builds it first if needed). Double-click this file.
cd /d "%~dp0"
if not exist "Launcher\build\Launcher.exe" (
  echo Building the launcher ...
  powershell -NoProfile -ExecutionPolicy Bypass -File "Launcher\build.ps1" || ( pause & exit /b 1 )
)
start "" "Launcher\build\Launcher.exe"
