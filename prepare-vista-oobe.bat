@echo off
rem Builds C:\vista-oobe from the Vista "oobe" folder. RUN IT IN THE VM, AS ADMINISTRATOR.
rem   prepare-vista-oobe.bat C:\path\to\vista\oobe
set SRC=%~1
if "%SRC%"=="" (echo usage: prepare-vista-oobe.bat C:\path\to\vista\oobe & exit /b 1)
if not exist "%SRC%\msoobe.exe" (echo %SRC%\msoobe.exe not found & exit /b 1)
set HOME=C:\vista-oobe
set DST=%HOME%\oobe
mkdir "%DST%\en-US" 2>nul
rem the Vista wizard, renamed so the redirect can never start itself again
copy /y "%SRC%\msoobe.exe" "%DST%\vistamsoobe.exe"
copy /y "%SRC%\en-US\msoobe.exe.mui" "%DST%\en-US\vistamsoobe.exe.mui"
copy /y "%SRC%\OOBEResources.dll" "%DST%\"
rem the proxy and the launcher
copy /y "%~dp0winbrand.dll" "%DST%\winbrand.dll"
copy /y "%~dp0oobelaunch.exe" "%HOME%\oobelaunch.exe"
rem your own branding pictures (optional): the "branding" folder next to this script
if exist "%~dp0branding" xcopy /e /i /y "%~dp0branding" "%DST%\branding" >nul
rem pictures / help texts the wizard looks for inside Windows' own oobe folder (only files that are not there yet)
takeown /f "%windir%\System32\oobe" /a >nul
icacls "%windir%\System32\oobe" /grant Administrators:(OI)(CI)F >nul
if not exist "%windir%\System32\oobe\msoobe.jpg" copy "%SRC%\msoobe.jpg" "%windir%\System32\oobe\"
for %%F in ("%SRC%\en-US\*.rtf") do if not exist "%windir%\System32\oobe\en-US\%%~nxF" copy "%%F" "%windir%\System32\oobe\en-US\"
echo.
echo Done. Next: take a VM SNAPSHOT, import ifeo-install.reg, then run   %windir%\System32\Sysprep\sysprep.exe /oobe /reboot
pause
