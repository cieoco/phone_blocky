# -*- mode: python ; coding: utf-8 -*-

from pathlib import Path
import os
import runpy
import sys

from PyInstaller.utils.hooks import collect_data_files, collect_submodules

# Do not collect incompatible DLLs from unrelated applications on the build PC's
# PATH (e.g. Poppler's icuuc.dll has different exports from Windows' ICU).
# Package hooks add their own library directories during dependency analysis.
if sys.platform == "win32":
    windows = Path(os.environ["SystemRoot"])
    os.environ["PATH"] = os.pathsep.join(map(str, (
        Path(sys.executable).parent, Path(sys.base_prefix),
        Path(sys.base_prefix) / "DLLs", windows / "System32", windows,
    )))


SPEC_DIR = Path(SPECPATH).resolve()
if (SPEC_DIR / "main.py").is_file():
    APP_DIR = SPEC_DIR
    ROOT = APP_DIR.parent
elif (SPEC_DIR / "motor_tuner" / "main.py").is_file():
    ROOT = SPEC_DIR
    APP_DIR = ROOT / "motor_tuner"
else:
    APP_DIR = Path(__file__).resolve().parent
    ROOT = APP_DIR.parent
profiles = runpy.run_path(str(APP_DIR / "flash_profiles.py"))

datas = []
for profile, _label, _description in profiles["FLASH_PROFILES"]:
    for name, _offset in profiles["FLASH_IMAGES"]:
        src = ROOT / ".pio" / "build" / profile / name
        if not src.is_file():
            raise FileNotFoundError(f"Missing firmware image: {src}")
        datas.append((str(src), f"bundled_bins/{profile}"))
datas += collect_data_files("esptool")

# 接線分頁用的板子圖（QTextBrowser 以 <img src="wemos.jpg"> 載入）
wiring_img = ROOT / "data" / "wemos.jpg"
if wiring_img.is_file():
    datas.append((str(wiring_img), "data"))

hiddenimports = collect_submodules("esptool")

a = Analysis(
    [str(APP_DIR / "main.py")],
    pathex=[str(APP_DIR)],
    binaries=[],
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name="PhoneBlockyMotorTuner",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
