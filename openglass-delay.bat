@echo off
rem Run as administrator, BEFORE taking the snapshot / running sysprep. Makes OpenGlass start only after the new user has signed in:
rem the service is set to manual start and stopped now; oobelaunch.exe starts it again (and sets it back to automatic) at the new user's first sign-in.
if not exist C:\vista-oobe mkdir C:\vista-oobe 2>nul
sc query OpenGlassHost >nul 2>&1 || (echo Service OpenGlassHost not found - is OpenGlass installed? & pause & exit /b 1)
sc config OpenGlassHost start= demand
sc stop OpenGlassHost
echo delay > C:\vista-oobe\glassdelay.txt
echo.
echo Done. OpenGlassHost is now manual start. Next: take the snapshot, then run sysprep /oobe /reboot.
echo To undo: sc config OpenGlassHost start= auto  and delete C:\vista-oobe\glassdelay.txt
pause
