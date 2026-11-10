@echo off
rem Run this in the Windows 10 VM (any user is fine - it only READS the files, nothing is run or changed).
rem 1. Copy the Vista "oobe" folder to the VM, e.g. C:\vista-oobe\oobe
rem 2. Put impcheck.exe next to this file, run it:   run-impcheck.bat C:\vista-oobe\oobe
set D=%~1
if "%D%"=="" set D=C:\vista-oobe\oobe
"%~dp0impcheck.exe" "%D%\msoobe.exe" "%D%\oobeldr.exe" "%D%\winsetup.dll" "%D%\win32ui.dll" "%D%\windeploy.exe" "%D%\audit.exe" "%D%\spprgrss.dll" "%D%\wdsutil.dll" "%D%\diagnostic.dll" "%D%\diagER.dll" "%D%\pnpibs.dll"
echo.
echo Send me impcheck.txt (it is next to impcheck.exe).
pause
