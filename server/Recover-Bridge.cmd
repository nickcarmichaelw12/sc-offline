@echo off
cd /d "%~dp0"
sc-local-server.exe --recover-bridge
pause
