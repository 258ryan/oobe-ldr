@echo off
rem Run as administrator, in the account where you set up the Vista look.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0copy-to-default.ps1"
pause
