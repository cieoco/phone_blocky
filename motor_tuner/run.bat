@echo off
setlocal
rem phone_blocky motor tuner launcher
cd /d "%~dp0"

set "PYTHON_EXE=.\.venv\Scripts\python.exe"
set "BOOTSTRAP="

where py >nul 2>nul
if %ERRORLEVEL%==0 (
    set "BOOTSTRAP=py -3"
) else (
    where python >nul 2>nul
    if %ERRORLEVEL%==0 set "BOOTSTRAP=python"
)

if not exist "%PYTHON_EXE%" goto setup
"%PYTHON_EXE%" --version >nul 2>nul
if errorlevel 1 goto setup
"%PYTHON_EXE%" -c "import esptool, numpy, matplotlib, PyQt6" >nul 2>nul
if errorlevel 1 (
    echo [setup] Virtual environment dependencies are invalid; recreating...
    goto setup
)
goto run

:setup
if "%BOOTSTRAP%"=="" (
    echo [error] Python was not found. Install Python 3 and enable "Add python.exe to PATH".
    exit /b 1
)

if exist ".venv" (
    echo [setup] Existing virtual environment is invalid; recreating...
    rmdir /s /q ".venv"
) else (
    echo [setup] Creating virtual environment...
)

%BOOTSTRAP% -m venv .venv
if errorlevel 1 exit /b 1

"%PYTHON_EXE%" -m pip install --upgrade pip
if errorlevel 1 exit /b 1

"%PYTHON_EXE%" -m pip install -r requirements.txt
if errorlevel 1 exit /b 1

:run
"%PYTHON_EXE%" main.py
exit /b %ERRORLEVEL%
