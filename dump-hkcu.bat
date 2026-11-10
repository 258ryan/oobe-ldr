@echo off
rem OPTIONAL, for comparing two accounts: run in the account where everything looks right, then in the new user, and send both files.
rem Writes %OUTDIR%\hkcu-<user>.txt with the look-and-feel registry branches of the current user.
if not exist C:\vista-oobe mkdir C:\vista-oobe 2>nul
set OUTDIR=C:\vista-oobe
if not exist C:\vista-oobe\ set OUTDIR=%TEMP%
set OUT=%OUTDIR%\hkcu-%USERNAME%.txt
> "%OUT%" echo ==== HKCU dump of %USERNAME% %date% %time% ====
for %%K in (
 "HKCU\Control Panel"
 "HKCU\Software\Microsoft\Windows\DWM"
 "HKCU\Software\Microsoft\Windows\CurrentVersion\Themes"
 "HKCU\Software\Microsoft\Windows\CurrentVersion\ThemeManager"
 "HKCU\Software\Microsoft\Windows\CurrentVersion\Policies"
 "HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer"
) do (
  >> "%OUT%" echo.
  >> "%OUT%" echo ######## %%~K
  reg export "%%~K" "%TEMP%\_k.reg" /y >nul 2>&1 && type "%TEMP%\_k.reg" >> "%OUT%"
)
>> "%OUT%" echo.
>> "%OUT%" echo ######## HKCU\Software (top-level names)
reg query "HKCU\Software" >> "%OUT%" 2>&1
echo Done - send %OUT%
pause
