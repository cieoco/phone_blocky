"""Run with motor_tuner/.venv/Scripts/python; no USB opened or firmware written."""
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / ".pio" / "tuner-ui-check"
OUT.mkdir(parents=True, exist_ok=True)
os.environ["QT_QPA_PLATFORM"] = "offscreen"
os.environ["MPLCONFIGDIR"] = str(OUT / "matplotlib")
sys.path.insert(0, str(ROOT / "motor_tuner"))
from PyQt6.QtCore import QSettings
from PyQt6.QtGui import QFont, QFontDatabase
from PyQt6.QtWidgets import QApplication
import main as tuner

tuner.QSettings = lambda *_: QSettings(str(OUT / "settings.ini"), QSettings.Format.IniFormat)
app = QApplication([])
if os.name == "nt":
    font_id = QFontDatabase.addApplicationFont("C:/Windows/Fonts/msjh.ttc")
    families = QFontDatabase.applicationFontFamilies(font_id)
    if families:
        app.setFont(QFont(families[0], 10))
win = tuner.MotorTuner()
assert win.flash_profile_combo.currentData() == "esp32dev_competitor"
assert not win.chk_flash_erase.isChecked()
for index, role in [(0, "esp32dev_competitor"), (1, "esp32dev"), (0, "esp32dev_competitor")]:
    win.flash_profile_combo.setCurrentIndex(index)
    args = win._build_flash_write_args("COM_TEST_ONLY")
    assert args and args[:4] == ["--chip", "esp32", "--port", "COM_TEST_ONLY"], (role, args, win.flash_log.toPlainText())
    for name, offset in tuner.FLASH_IMAGES:
        expected = str(ROOT / ".pio" / "build" / role / name)
        assert expected in args and args[args.index(expected)-1] == offset
        assert win.flash_checks[name].isChecked()
    # A switch must replace manual overrides and restore all four images.
    win.flash_offsets["firmware.bin"].setText("0x0")
    win.flash_paths["firmware.bin"].setText("missing-image.bin")
    assert win._build_flash_write_args("COM_TEST_ONLY") is None
    win.flash_checks["littlefs.bin"].setChecked(False)
win._select_flash_profile()

# Simulate esptool's process lifecycle without launching a process or opening USB.
class Signal:
    def connect(self, _slot):
        pass


class FakeProcess:
    def __init__(self, _parent):
        self.readyReadStandardOutput = Signal()
        self.readyReadStandardError = Signal()
        self.finished = Signal()

    def setWorkingDirectory(self, _path):
        pass

    def start(self, program, args):
        assert program and "write_flash" in args

    def waitForStarted(self, _timeout):
        return True


real_process = tuner.QProcess
tuner.QProcess = FakeProcess
try:
    win._run_esptool(win._build_flash_write_args("COM_TEST_ONLY"), "test")
    assert not win.flash_profile_combo.isEnabled()
    assert not win.flash_files_group.isEnabled()
    win._on_flash_finished(1, None)
    assert win.flash_profile_combo.isEnabled() and win.flash_files_group.isEnabled()
finally:
    tuner.QProcess = real_process
    win.flash_process = None

# Exercise the actual frozen-app resolution with a missing selected-role image.
# It must not silently pick the local teaching firmware that exists on this PC.
original = tuner.BUNDLE_ROOT
tuner.BUNDLE_ROOT = OUT / "missing-bundle"
sys.frozen = True
win._select_flash_profile()
assert win._build_flash_write_args("COM_TEST_ONLY") is None
del sys.frozen
tuner.BUNDLE_ROOT = original
win._select_flash_profile()
win.flash_log.clear()
win.tabs.setCurrentIndex(4)
win.show()
app.processEvents()
assert win.grab().save(str(OUT / "flash-tab.png"))
win.close()
print("Tuner flash UI PASS: default competitor, both roles, complete switch, missing image refusal, ESP32 chip and offsets; no hardware written")
