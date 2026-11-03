@echo off
cd /d "%~dp0"
where py >nul 2>nul
if errorlevel 1 (
  python server.py --open
) else (
  py -3 server.py --open
)
if errorlevel 1 pause
