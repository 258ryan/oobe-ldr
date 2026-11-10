@echo off
rem Builds C:\vista-oobe and replaces System32\oobe\msoobe.exe in the VM. RUN AS ADMINISTRATOR.
rem   prepare-vista-oobe.bat C:\path\to\vista\oobe
set SRC=%~1
if "%SRC%"=="" (echo usage: prepare-vista-oobe.bat C:\path\to\vista\oobe & exit /b 1)
if not exist "%SRC%\msoobe.exe" (echo %SRC%\msoobe.exe not found & exit /b 1)
if not exist "%SRC%\OOBEResources.dll" (echo %SRC%\OOBEResources.dll not found & exit /b 1)
if not exist "%~dp0runtime\DUser.dll" (echo %~dp0runtime\DUser.dll not found & exit /b 1)
if not exist "%~dp0runtime\dui72.dll" (echo %~dp0runtime\dui72.dll not found & exit /b 1)
if not exist "%~dp0winbrand.dll" (echo %~dp0winbrand.dll not found & exit /b 1)
if not exist "%~dp0oobelaunch.exe" (echo %~dp0oobelaunch.exe not found & exit /b 1)
set HOME=C:\vista-oobe
set DST=%HOME%\oobe
if not exist "%DST%" mkdir "%DST%"
rem the Vista wizard, renamed so the redirect can never start itself again
copy /y "%SRC%\msoobe.exe" "%DST%\vistamsoobe.exe"
if errorlevel 1 (echo Could not stage the Vista wizard. & exit /b 1)
rem stage every Vista OOBE MUI language provided in the source folder
for /d %%L in ("%SRC%\*") do if exist "%%~fL\msoobe.exe.mui" (
  if not exist "%DST%\%%~nxL" mkdir "%DST%\%%~nxL"
  copy /y "%%~fL\msoobe.exe.mui" "%DST%\%%~nxL\vistamsoobe.exe.mui" >nul
  if errorlevel 1 (echo Could not stage the %%~nxL Vista wizard translation. & exit /b 1)
)
copy /y "%SRC%\OOBEResources.dll" "%DST%\"
if errorlevel 1 (echo Could not stage OOBEResources.dll. & exit /b 1)
if exist "%SRC%\msoobe.jpg" copy /y "%SRC%\msoobe.jpg" "%DST%\msoobe.jpg"
rem use the bundled compatibility builds only in the Vista wizard process (app-local DLL lookup)
copy /y "%~dp0runtime\DUser.dll" "%DST%\DUser.dll"
if errorlevel 1 (echo Could not stage the modded DUser.dll. & exit /b 1)
rem the bundled file identifies itself internally as DUI70.dll, so use that loader name
copy /y "%~dp0runtime\dui72.dll" "%DST%\DUI70.dll"
if errorlevel 1 (echo Could not stage dui72.dll as DUI70.dll. & exit /b 1)
rem the winbrand proxy and replacement launcher
copy /y "%~dp0winbrand.dll" "%DST%\winbrand.dll"
if errorlevel 1 (echo Could not stage winbrand.dll. & exit /b 1)
copy /y "%~dp0oobelaunch.exe" "%HOME%\oobelaunch.exe"
if errorlevel 1 (echo Could not stage oobelaunch.exe. & exit /b 1)
rem your own branding pictures (optional): the "branding" folder next to this script
if exist "%~dp0branding" xcopy /e /i /y "%~dp0branding" "%DST%\branding" >nul
echo.
echo Replacing the system OOBE executable (the original is preserved for rollback and fallback)...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-system-msoobe.ps1"
if errorlevel 1 (echo System replacement failed. See the error above. & pause & exit /b 1)
echo.
echo Done. Take a VM SNAPSHOT, then run   %windir%\System32\Sysprep\sysprep.exe /oobe /reboot
pause
