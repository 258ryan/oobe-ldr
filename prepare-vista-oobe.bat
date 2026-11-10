@echo off
rem Stages the complete Vista OOBE folder and replaces System32\oobe in the VM. RUN AS ADMINISTRATOR.
rem   prepare-vista-oobe.bat C:\path\to\vista\oobe
set SRC=%~1
if "%SRC%"=="" (echo usage: prepare-vista-oobe.bat C:\path\to\vista\oobe & exit /b 1)
if not exist "%SRC%\msoobe.exe" (echo %SRC%\msoobe.exe not found & exit /b 1)
if not exist "%SRC%\setup.exe" (echo %SRC%\setup.exe not found & exit /b 1)
if not exist "%SRC%\OOBEResources.dll" (echo %SRC%\OOBEResources.dll not found & exit /b 1)
if not exist "%~dp0runtime\DUser.dll" (echo %~dp0runtime\DUser.dll not found & exit /b 1)
if not exist "%~dp0runtime\dui72.dll" (echo %~dp0runtime\dui72.dll not found & exit /b 1)
if not exist "%~dp0winbrand.dll" (echo %~dp0winbrand.dll not found & exit /b 1)
if not exist "%~dp0oobelaunch.exe" (echo %~dp0oobelaunch.exe not found & exit /b 1)
if not exist "%~dp0oobesetup.exe" (echo %~dp0oobesetup.exe not found & exit /b 1)
set HOME=C:\vista-oobe
set DST=%HOME%\oobe
if not exist "%DST%" mkdir "%DST%"
rem Copy every Vista OOBE file, language folder, and dependency, including winsetup.dll.
robocopy "%SRC%" "%DST%" /MIR /COPY:DAT /R:1 /W:1 /NFL /NDL /NJH /NJS /NP
if errorlevel 8 (echo Could not stage the complete Vista OOBE folder. & exit /b 1)
if not exist "%DST%\winsetup.dll" (echo %DST%\winsetup.dll not found after staging. & exit /b 1)
rem Rename the Vista wizard so the installed launcher can start it without recursion.
move /y "%DST%\msoobe.exe" "%DST%\vistamsoobe.exe" >nul
if errorlevel 1 (echo Could not rename the Vista wizard. & exit /b 1)
rem Keep Vista's setup host beside the winsetup dependency; the wrapper sets both DLL search and working directories.
move /y "%DST%\setup.exe" "%DST%\vista-setup.exe" >nul
if errorlevel 1 (echo Could not rename Vista setup.exe. & exit /b 1)
rem Rename each Vista wizard MUI to match the renamed executable.
for /d %%L in ("%SRC%\*") do if exist "%%~fL\msoobe.exe.mui" (
  if exist "%DST%\%%~nxL\msoobe.exe.mui" (
    move /y "%DST%\%%~nxL\msoobe.exe.mui" "%DST%\%%~nxL\vistamsoobe.exe.mui" >nul
    if errorlevel 1 (echo Could not rename the %%~nxL Vista wizard translation. & exit /b 1)
  )
)
rem use the bundled compatibility builds only in the Vista wizard process (app-local DLL lookup)
if exist "%DST%\DUser.dll" attrib -r "%DST%\DUser.dll"
copy /y "%~dp0runtime\DUser.dll" "%DST%\DUser.dll"
if errorlevel 1 (echo Could not stage the modded DUser.dll. & exit /b 1)
rem the bundled file identifies itself internally as DUI70.dll, so use that loader name
if exist "%DST%\DUI70.dll" attrib -r "%DST%\DUI70.dll"
copy /y "%~dp0runtime\dui72.dll" "%DST%\DUI70.dll"
if errorlevel 1 (echo Could not stage dui72.dll as DUI70.dll. & exit /b 1)
rem the winbrand proxy and replacement launcher
if exist "%DST%\winbrand.dll" attrib -r "%DST%\winbrand.dll"
copy /y "%~dp0winbrand.dll" "%DST%\winbrand.dll"
if errorlevel 1 (echo Could not stage winbrand.dll. & exit /b 1)
if exist "%DST%\msoobe.exe" attrib -r "%DST%\msoobe.exe"
copy /y "%~dp0oobelaunch.exe" "%DST%\msoobe.exe"
if errorlevel 1 (echo Could not stage the replacement OOBE launcher. & exit /b 1)
copy /y "%~dp0oobesetup.exe" "%DST%\setup.exe"
if errorlevel 1 (echo Could not stage the Vista setup wrapper. & exit /b 1)
copy /y "%~dp0oobelaunch.exe" "%HOME%\oobelaunch.exe"
if errorlevel 1 (echo Could not stage oobelaunch.exe. & exit /b 1)
rem your own branding pictures (optional): the "branding" folder next to this script
if exist "%~dp0branding" xcopy /e /i /y "%~dp0branding" "%DST%\branding" >nul
echo.
echo Replacing the complete system OOBE folder (the original is preserved for rollback and fallback)...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-system-msoobe.ps1"
if errorlevel 1 (echo System replacement failed. See the error above. & pause & exit /b 1)
echo.
echo Done. Revert to your VM snapshot if anything fails. Run   %windir%\System32\Sysprep\sysprep.exe /oobe /reboot
pause
