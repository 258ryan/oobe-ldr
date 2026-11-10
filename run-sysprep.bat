@echo off
rem VM ONLY, after a snapshot. Run from the BUILT-IN Administrator account (the one whose profile you customised in audit mode).
rem /generalize is required: CopyProfile is only processed in the "specialize" pass, which only runs after a generalize.
if /i not "%USERNAME%"=="Administrator" echo WARNING: you are "%USERNAME%", not the built-in Administrator - CopyProfile copies the profile of the account running sysprep.
pause
%windir%\System32\Sysprep\sysprep.exe /generalize /oobe /reboot /unattend:C:\vista-oobe\unattend-copyprofile.xml
