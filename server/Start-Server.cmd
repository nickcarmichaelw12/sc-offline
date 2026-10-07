@echo off
cd /d "%~dp0"
sc-local-server.exe --open
if errorlevel 1 (
  echo.
  echo Server could not start. If it is already running, open http://127.0.0.1:18870
  pause
)
