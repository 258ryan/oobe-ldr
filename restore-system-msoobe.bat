@echo off
rem Restores the original System32\oobe\msoobe.exe preserved by prepare-vista-oobe.bat.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0restore-system-msoobe.ps1"
if errorlevel 1 (echo Restore failed. See the error above. & pause & exit /b 1)
echo.
echo The original Windows OOBE executable has been restored.
pause
