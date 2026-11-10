@echo off
rem Run AFTER the first sign-in, as the new user (ideally in an elevated prompt). Writes C:\vista-oobe\verify.txt - send me that file.
if not exist C:\vista-oobe mkdir C:\vista-oobe 2>nul
set OUTDIR=C:\vista-oobe
if not exist C:\vista-oobe\ set OUTDIR=%TEMP%
set OUT=%OUTDIR%\verify.txt
> "%OUT%" echo ==== verify-oobe %date% %time% ====
call :sec "who / computer"
>> "%OUT%" echo user=%USERNAME%  computer=%COMPUTERNAME%
>> "%OUT%" hostname
call :sec "local accounts (net user)"
net user >> "%OUT%" 2>&1
call :sec "administrators group"
net localgroup administrators >> "%OUT%" 2>&1
call :sec "built-in Administrator account"
net user administrator 2>&1 | findstr /i "active name" >> "%OUT%"
call :sec "time zone / time"
tzutil /g >> "%OUT%" 2>&1
>> "%OUT%" echo %date% %time%
call :sec "region / language (current user)"
reg query "HKCU\Control Panel\International" /v LocaleName >> "%OUT%" 2>&1
reg query "HKCU\Control Panel\International" /v sCountry >> "%OUT%" 2>&1
reg query "HKCU\Control Panel\International\Geo" /v Name >> "%OUT%" 2>&1
reg query "HKCU\Keyboard Layout\Preload" >> "%OUT%" 2>&1
call :sec "wallpaper"
reg query "HKCU\Control Panel\Desktop" /v Wallpaper >> "%OUT%" 2>&1
call :sec "Windows Update settings"
reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\WindowsUpdate\Auto Update" >> "%OUT%" 2>&1
reg query "HKLM\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU" >> "%OUT%" 2>&1
call :sec "Defender / error reporting"
reg query "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting" /v Disabled >> "%OUT%" 2>&1
reg query "HKLM\SOFTWARE\Microsoft\Windows Defender\Spynet" >> "%OUT%" 2>&1
call :sec "network profile"
powershell -NoProfile -Command "Get-NetConnectionProfile | Format-List Name,NetworkCategory,IPv4Connectivity" >> "%OUT%" 2>&1
call :sec "OOBE / setup state flags"
reg query "HKLM\SYSTEM\Setup" /v OOBEInProgress >> "%OUT%" 2>&1
reg query "HKLM\SYSTEM\Setup" /v SystemSetupInProgress >> "%OUT%" 2>&1
reg query "HKLM\SYSTEM\Setup" /v SetupType >> "%OUT%" 2>&1
reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Setup\State" >> "%OUT%" 2>&1
reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Setup\OOBE" >> "%OUT%" 2>&1
reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\OOBE" >> "%OUT%" 2>&1
call :sec "activation"
cscript //nologo %windir%\system32\slmgr.vbs /dli >> "%OUT%" 2>&1
cscript //nologo %windir%\system32\slmgr.vbs /xpr >> "%OUT%" 2>&1
call :sec "services the wizard configured"
sc query fdrespub >> "%OUT%" 2>&1
sc query upnphost >> "%OUT%" 2>&1
sc query netprofm >> "%OUT%" 2>&1
call :sec "Windhawk"
sc query windhawk >> "%OUT%" 2>&1
reg query "HKLM\SOFTWARE\Windhawk" >> "%OUT%" 2>&1
reg query "HKLM\SOFTWARE\Windhawk\Engine\Mods" >> "%OUT%" 2>&1
dir /b "C:\ProgramData\Windhawk\ModsSource" >> "%OUT%" 2>&1
reg query "HKCU\Software\Windhawk" >> "%OUT%" 2>&1
reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" >> "%OUT%" 2>&1
reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run" >> "%OUT%" 2>&1
tasklist /fi "imagename eq windhawk.exe" >> "%OUT%" 2>&1
call :sec "scheduled tasks (glass / dwm / windhawk) and who they run as"
schtasks /query /fo list /v 2>nul | findstr /i "TaskName Run.As.User Task.To.Run Logon.Mode" | findstr /i "glass dwm windhawk Run.As Logon.Mode Task.To.Run" >> "%OUT%" 2>&1
call :sec "dwm.exe loaded modules"
tasklist /m /fi "imagename eq dwm.exe" >> "%OUT%" 2>&1
call :sec "wizard log: errors and warnings"
findstr /i /c:"error" /c:"warning" /c:"fail" C:\Windows\Panther\UnattendGC\setupact.log >> "%OUT%" 2>&1
type C:\Windows\Panther\UnattendGC\setuperr.log >> "%OUT%" 2>&1
echo.
echo Done - send %OUT%
pause
exit /b
:sec
>> "%OUT%" echo.
>> "%OUT%" echo ---- %~1 ----
exit /b
