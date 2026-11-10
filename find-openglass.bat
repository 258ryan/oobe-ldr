@echo off
rem Run in the account where OpenGlass is installed and configured. Writes C:\vista-oobe\openglass-where.txt - send me that file.
if not exist C:\vista-oobe mkdir C:\vista-oobe 2>nul
set OUTDIR=C:\vista-oobe
if not exist C:\vista-oobe\ set OUTDIR=%TEMP%
set OUT=%OUTDIR%\openglass-where.txt
> "%OUT%" echo ==== find-openglass %date% %time% as %USERNAME% ====
call :sec "per-user registry keys with 'glass' in the name (HKCU\Software)"
reg query "HKCU\Software" /s /f glass /k >> "%OUT%" 2>&1
call :sec "per-user registry values with 'glass' in name or data (HKCU\Software)"
reg query "HKCU\Software" /s /f glass >> "%OUT%" 2>&1
call :sec "HKCU DWM key"
reg query "HKCU\Software\Microsoft\Windows\DWM" >> "%OUT%" 2>&1
call :sec "machine-wide registry keys with 'glass' (HKLM\SOFTWARE)"
reg query "HKLM\SOFTWARE" /s /f glass /k >> "%OUT%" 2>&1
call :sec "HKLM DWM key and dwm.exe execution options"
reg query "HKLM\SOFTWARE\Microsoft\Windows\DWM" >> "%OUT%" 2>&1
reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\dwm.exe" >> "%OUT%" 2>&1
call :sec "services with 'glass' in the name"
sc query state= all | findstr /i "glass" >> "%OUT%" 2>&1
call :sec "scheduled tasks with 'glass' in the name"
schtasks /query /fo list 2>nul | findstr /i "glass" >> "%OUT%" 2>&1
call :sec "autostart entries"
reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" >> "%OUT%" 2>&1
reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" >> "%OUT%" 2>&1
call :sec "files named *glass* (Program Files, ProgramData, Windows, user profile)"
for %%D in ("%ProgramFiles%" "%ProgramFiles(x86)%" "%ProgramData%" "%SystemRoot%" "%USERPROFILE%") do dir /s /b "%%~D\*glass*" >> "%OUT%" 2>nul
call :sec "files named *openglass* anywhere on C: (can be slow)"
dir /s /b "C:\*openglass*" >> "%OUT%" 2>nul
echo.
echo Done - send %OUT%
pause
exit /b
:sec
>> "%OUT%" echo.
>> "%OUT%" echo ---- %~1 ----
exit /b
