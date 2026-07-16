@echo off
setlocal EnableExtensions EnableDelayedExpansion
rem Build standalone Phone Blocky motor tuner EXE with bundled firmware bins.
cd /d "%~dp0"

set "PYTHON_EXE=.\.venv\Scripts\python.exe"
set "BIN_DIR=..\.pio\build\esp32dev"
set "BOOTSTRAP="
set "NEED_SETUP=0"

if not exist "%PYTHON_EXE%" (
    set "NEED_SETUP=1"
) else (
    "%PYTHON_EXE%" --version >nul 2>nul
    if errorlevel 1 set "NEED_SETUP=1"
)

if "%NEED_SETUP%"=="1" (
    if exist ".venv" (
        echo [setup] Existing virtual environment is invalid; recreating...
        rmdir /s /q ".venv"
    )

    where py >nul 2>nul
    if !ERRORLEVEL!==0 (
        set "BOOTSTRAP=py -3"
    ) else (
        where python >nul 2>nul
        if !ERRORLEVEL!==0 set "BOOTSTRAP=python"
    )

    if "!BOOTSTRAP!"=="" (
        echo [error] Python was not found. Install Python 3 and enable "Add python.exe to PATH".
        exit /b 1
    )

    echo [setup] Creating virtual environment...
    !BOOTSTRAP! -m venv .venv
    if errorlevel 1 exit /b 1
)

"%PYTHON_EXE%" -m pip install -r requirements.txt
if errorlevel 1 exit /b 1

echo [build] Building firmware images with PlatformIO...
pushd ..
"motor_tuner\.venv\Scripts\python.exe" -m platformio run
if errorlevel 1 (
    popd
    exit /b 1
)
"motor_tuner\.venv\Scripts\python.exe" -m platformio run --target buildfs
if errorlevel 1 (
    popd
    exit /b 1
)
popd

for %%F in (bootloader.bin partitions.bin firmware.bin littlefs.bin) do (
    if not exist "%BIN_DIR%\%%F" (
        echo [error] Missing %BIN_DIR%\%%F
        echo [hint] PlatformIO did not produce the expected firmware image.
        exit /b 1
    )
)

if exist "dist" rmdir /s /q "dist"
if exist "build" rmdir /s /q "build"

"%PYTHON_EXE%" -m PyInstaller --clean --noconfirm phone_blocky_motor_tuner.spec
if errorlevel 1 exit /b 1

echo.
echo [ok] Built dist\PhoneBlockyMotorTuner.exe
exit /b 0
