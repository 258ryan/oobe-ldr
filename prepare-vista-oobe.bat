@echo off
rem Builds C:\vista-oobe and replaces System32\oobe\msoobe.exe in the VM. RUN AS ADMINISTRATOR.
rem   prepare-vista-oobe.bat C:\path\to\vista\oobe C:\path\to\modded\duser.dll C:\path\to\dui72.dll
set SRC=%~1
if "%SRC%"=="" (echo usage: prepare-vista-oobe.bat C:\path\to\vista\oobe C:\path\to\modded\duser.dll C:\path\to\dui72.dll & exit /b 1)
if "%~2"=="" (echo Supply the modded DUser.dll as the second argument. & exit /b 1)
if "%~3"=="" (echo Supply dui72.dll as the third argument. & exit /b 1)
if not exist "%SRC%\msoobe.exe" (echo %SRC%\msoobe.exe not found & exit /b 1)
if not exist "%~2" (echo %~2 not found & exit /b 1)
if not exist "%~3" (echo %~3 not found & exit /b 1)
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
if exist "%SRC%\msoobe.jpg" copy /y "%SRC%\msoobe.jpg" "%DST%\msoobe.jpg"
rem use the supplied compatibility builds only in the Vista wizard process (app-local DLL lookup)
copy /y "%~2" "%DST%\DUser.dll"
if errorlevel 1 (echo Could not stage the modded DUser.dll. & exit /b 1)
rem the supplied file identifies itself internally as DUI70.dll, so use that loader name
copy /y "%~3" "%DST%\DUI70.dll"
if errorlevel 1 (echo Could not stage dui72.dll as DUI70.dll. & exit /b 1)
rem the winbrand proxy and replacement launcher
copy /y "%~dp0winbrand.dll" "%DST%\winbrand.dll"
copy /y "%~dp0oobelaunch.exe" "%HOME%\oobelaunch.exe"
rem your own branding pictures (optional): the "branding" folder next to this script
if exist "%~dp0branding" xcopy /e /i /y "%~dp0branding" "%DST%\branding" >nul
echo.
echo Replacing the system OOBE executable (the original is preserved for rollback and fallback)...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-system-msoobe.ps1"
if errorlevel 1 (echo System replacement failed. See the error above. & pause & exit /b 1)
echo.
echo Done. Take a VM SNAPSHOT, then run   %windir%\System32\Sysprep\sysprep.exe /oobe /reboot
pause
