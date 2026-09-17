"""phone_blocky 單馬達調校工具.

聚焦三項功能（對齊 motorControl/python_gui/motor_control_v4 的機制，但改走
phone_blocky 的 WebSocket cmd: 協定）：

  1. 單馬達測試 — PWM（全部馬達）/ 閉迴路轉速、位置（M3/M4 有編碼器）
  2. PID 調教 — 即時送出速度環與位置環 PID（韌體僅支援寫入，不回讀）
  3. 輸出曲線圖 — subscribe 10Hz telemetry，繪實際 vs 目標

連線：ws://<esp32-ip>/ws （埠 80）。
"""
from __future__ import annotations

import sys
import os
import json
import time
from datetime import datetime
from pathlib import Path

from PyQt6.QtCore import QProcess, QSettings, Qt, QTimer
from PyQt6.QtWidgets import (
    QApplication,
    QButtonGroup,
    QCheckBox,
    QComboBox,
    QDoubleSpinBox,
    QFileDialog,
    QFrame,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QRadioButton,
    QScrollArea,
    QSizePolicy,
    QSpinBox,
    QStackedWidget,
    QTabWidget,
    QTextBrowser,
    QVBoxLayout,
    QWidget,
)

from chart_widget import MotorChartWidget
from serial_worker import SerialWorker
from ws_worker import WsWorker

NUM_MOTORS = 4
PROJECT_ROOT = Path(__file__).resolve().parents[1]
BUNDLE_ROOT = Path(getattr(sys, "_MEIPASS", PROJECT_ROOT))
DEFAULT_FLASH_DIR = PROJECT_ROOT / ".pio" / "build" / "esp32dev"
BUNDLED_FLASH_DIR = BUNDLE_ROOT / "bundled_bins"
FLASH_IMAGES = (
    ("bootloader.bin", "0x1000"),
    ("partitions.bin", "0x8000"),
    ("firmware.bin", "0x10000"),
    ("littlefs.bin", "0x1B9000"),
)


def bundled_or_project_bin(name: str) -> Path:
    bundled = BUNDLED_FLASH_DIR / name
    if bundled.is_file():
        return bundled
    return DEFAULT_FLASH_DIR / name


def run_bundled_esptool() -> int:
    import esptool

    if sys.stdout is None:
        try:
            sys.stdout = os.fdopen(1, "w", encoding="utf-8", closefd=False)
        except OSError:
            sys.stdout = open(os.devnull, "w", encoding="utf-8")
    if sys.stderr is None:
        try:
            sys.stderr = os.fdopen(2, "w", encoding="utf-8", closefd=False)
        except OSError:
            sys.stderr = open(os.devnull, "w", encoding="utf-8")

    try:
        return int(esptool.main(sys.argv[2:]) or 0)
    except SystemExit as e:
        return int(e.code or 0)


class MotorTuner(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("phone_blocky 馬達調校工具")
        self._resize_for_screen()

        self.settings = QSettings("phone_blocky", "motor_tuner")
        # 兩種傳輸並存，self.active 指向當前選用者（訊號介面一致，統一處理）
        self.ws = WsWorker()
        self.serial = SerialWorker()
        self.active = self.serial
        self.current_motor = 1
        self._compact_cards = None
        self.flash_process: QProcess | None = None
        self._pending_flash_args: list[str] | None = None
        self._autotune_active = False
        self._autotune_ctx: dict | None = None
        self._autotune_samples: list[tuple[float, float]] = []
        self._autotune_awaiting_buffer = False  # 已送 chart_buffer_stop，等韌體高速緩存回傳
        self._speed_id: dict[int, dict[str, float]] = {}
        self._autotune_speed_pid: tuple[float, float, float] | None = None
        self._autotune_pos_pid: tuple[float, float, float] | None = None
        # 由 capabilities 回填：motor_id -> has_encoder
        self.has_encoder: dict[int, bool] = {}

        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)

        root.addWidget(self._build_connection_group())

        self.tabs = QTabWidget()
        self.tabs.addTab(self._build_tuning_tab(), "馬達調校")
        self.tabs.addTab(self._scrollable(self._build_settings_tab()), "設定")
        self.tabs.addTab(self._scrollable(self._build_sensor_tab()), "感測器")
        self.tabs.addTab(self._build_wiring_tab(), "接線")
        self.tabs.addTab(self._scrollable(self._build_flash_tab()), "燒錄")
        root.addWidget(self.tabs, 1)

        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setMaximumBlockCount(500)
        self.log.setMinimumHeight(72)
        self.log.setMaximumHeight(140)
        self.log.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Maximum)
        root.addWidget(self.log)

        # --- 兩個 worker 的訊號都接到同一組處理器（只有連線者會 emit）---
        for w in (self.ws, self.serial):
            w.connected.connect(self._on_connected)
            w.disconnected.connect(self._on_disconnected)
            w.error_occurred.connect(lambda m: self._log(f"錯誤: {m}"))
            w.raw_log.connect(self._log)
            w.data_received.connect(self._on_data)

        self._set_connected_ui(False)
        self._apply_responsive_layout()

    # ------------------------------------------------------------------
    # UI 建構
    # ------------------------------------------------------------------

    def _build_tuning_tab(self) -> QWidget:
        w = QWidget()
        root = QHBoxLayout(w)
        root.setSpacing(10)
        self.tuning_row = QVBoxLayout()
        self.tuning_row.setSpacing(10)
        self.motor_card = self._build_motor_group()
        self.pid_card = self._build_pid_group()
        self.auto_pid_card = self._build_auto_pid_group()
        self.tuning_row.addWidget(self.motor_card)
        self.tuning_row.addWidget(self.pid_card)
        self.tuning_row.addWidget(self.auto_pid_card)
        self.tuning_row.addStretch()

        left = QWidget()
        left.setLayout(self.tuning_row)
        left_scroll = self._scrollable(left)
        left_scroll.setMinimumWidth(420)
        root.addWidget(left_scroll, 1)

        right = QWidget()
        right_layout = QVBoxLayout(right)
        right_layout.setContentsMargins(0, 0, 0, 0)

        self.chart = MotorChartWidget()
        self.chart.setMinimumHeight(220)
        right_layout.addWidget(self.chart, 1)

        self.live = QLabel("（尚無遙測）")
        self.live.setStyleSheet("font-family: Consolas; color: #2d5be3;")
        right_layout.addWidget(self.live)
        root.addWidget(right, 1)
        return w

    def _resize_for_screen(self):
        screen = QApplication.primaryScreen()
        if not screen:
            self.resize(880, 760)
            return
        rect = screen.availableGeometry()
        width = min(1040, max(760, int(rect.width() * 0.82)))
        height = min(840, max(560, int(rect.height() * 0.86)))
        self.resize(width, height)

    def _scrollable(self, widget: QWidget) -> QScrollArea:
        area = QScrollArea()
        area.setWidget(widget)
        area.setWidgetResizable(True)
        area.setFrameShape(QFrame.Shape.NoFrame)
        return area

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self._apply_responsive_layout()

    def _apply_responsive_layout(self):
        if not hasattr(self, "tuning_row") or not hasattr(self, "log"):
            return

        compact = self.width() < 820
        if compact != self._compact_cards:
            self._compact_cards = compact
            self.tuning_row.setStretchFactor(self.motor_card, 0)
            self.tuning_row.setStretchFactor(self.pid_card, 0)
            self.tuning_row.setStretchFactor(self.auto_pid_card, 0)

        h = self.height()
        log_h = 82 if h < 660 else 110 if h < 820 else 140
        self.log.setMaximumHeight(log_h)

    # ------------------------------------------------------------------
    # 燒錄分頁
    # ------------------------------------------------------------------

    def _build_flash_tab(self) -> QWidget:
        w = QWidget()
        v = QVBoxLayout(w)

        target = QGroupBox("燒錄設定")
        tl = QHBoxLayout(target)
        port_hint = QLabel("使用上方「USB（序列）」選擇的 COM 埠")
        port_hint.setStyleSheet("color:#555;")
        tl.addWidget(port_hint, 1)
        tl.addWidget(QLabel("Baud:"))
        self.flash_baud_combo = QComboBox()
        self.flash_baud_combo.addItems(["921600", "460800", "115200"])
        tl.addWidget(self.flash_baud_combo)
        v.addWidget(target)

        files = QGroupBox("燒錄檔案")
        grid = QGridLayout(files)
        for col, head in enumerate(("啟用", "檔案", "Offset", "路徑", "")):
            grid.addWidget(QLabel(f"<b>{head}</b>"), 0, col)
        self.flash_checks: dict[str, QCheckBox] = {}
        self.flash_offsets: dict[str, QLineEdit] = {}
        self.flash_paths: dict[str, QLineEdit] = {}
        for row, (name, offset) in enumerate(FLASH_IMAGES, start=1):
            chk = QCheckBox()
            chk.setChecked(True)
            grid.addWidget(chk, row, 0)
            grid.addWidget(QLabel(name), row, 1)
            off = QLineEdit(offset)
            off.setFixedWidth(88)
            grid.addWidget(off, row, 2)
            path = QLineEdit(str(bundled_or_project_bin(name)))
            grid.addWidget(path, row, 3)
            browse = QPushButton("選擇")
            browse.clicked.connect(lambda _=False, n=name: self._browse_flash_file(n))
            grid.addWidget(browse, row, 4)
            self.flash_checks[name] = chk
            self.flash_offsets[name] = off
            self.flash_paths[name] = path
        grid.setColumnStretch(3, 1)
        v.addWidget(files)

        controls = QHBoxLayout()
        self.chk_flash_erase = QCheckBox("燒錄前清除 flash")
        controls.addWidget(self.chk_flash_erase)
        controls.addStretch()
        self.btn_flash_start = QPushButton("開始燒錄")
        self.btn_flash_start.clicked.connect(self._start_flash)
        self.btn_flash_stop = QPushButton("停止")
        self.btn_flash_stop.setEnabled(False)
        self.btn_flash_stop.clicked.connect(self._stop_flash)
        controls.addWidget(self.btn_flash_start)
        controls.addWidget(self.btn_flash_stop)
        v.addLayout(controls)

        note = QLabel("提示：請先停止序列連線；燒錄會使用 esptool，四個分區 Offset 已依安裝教程預設帶入。")
        note.setWordWrap(True)
        note.setStyleSheet("color:#666;")
        v.addWidget(note)

        self.flash_log = QPlainTextEdit()
        self.flash_log.setReadOnly(True)
        self.flash_log.setMinimumHeight(220)
        v.addWidget(self.flash_log, 1)

        return w

    def _browse_flash_file(self, name: str):
        cur = self.flash_paths[name].text().strip()
        start_dir = str(Path(cur).parent if cur else PROJECT_ROOT)
        path, _ = QFileDialog.getOpenFileName(
            self,
            f"選擇 {name}",
            start_dir,
            "Binary images (*.bin);;All files (*.*)",
        )
        if path:
            self.flash_paths[name].setText(path)

    def _start_flash(self):
        if self.flash_process and self.flash_process.state() != QProcess.ProcessState.NotRunning:
            self._flash_log("燒錄仍在執行中")
            return
        if self.serial.is_connected():
            self._flash_log("請先按上方「斷線」，釋放 USB 序列埠後再燒錄")
            return
        port = self._current_serial_port()
        if not port:
            self._refresh_ports()
            port = self._current_serial_port()
        if not port:
            self._flash_log("請選擇 COM 埠")
            return

        write_args = self._build_flash_write_args(port)
        if not write_args:
            return
        self.settings.setValue("serial_port", port)
        self.flash_log.clear()
        self._flash_log(f"使用 {port} 開始燒錄")
        if self.chk_flash_erase.isChecked():
            self._pending_flash_args = write_args
            self._run_esptool(["--chip", "esp32", "--port", port, "erase_flash"], "清除 flash")
        else:
            self._pending_flash_args = None
            self._run_esptool(write_args, "燒錄韌體")

    def _build_flash_write_args(self, port: str) -> list[str] | None:
        baud = self.flash_baud_combo.currentText().strip() or "921600"
        args = [
            "--chip", "esp32",
            "--port", port,
            "--baud", baud,
            "--before", "default_reset",
            "--after", "hard_reset",
            "write_flash",
            "-z",
        ]
        enabled = 0
        for name, _default_offset in FLASH_IMAGES:
            if not self.flash_checks[name].isChecked():
                continue
            offset = self.flash_offsets[name].text().strip()
            path = self.flash_paths[name].text().strip()
            if not offset:
                self._flash_log(f"{name} 缺少 Offset")
                return None
            if not path or not Path(path).is_file():
                self._flash_log(f"找不到 {name}: {path}")
                return None
            args.extend([offset, path])
            enabled += 1
        if enabled == 0:
            self._flash_log("請至少勾選一個 bin 檔")
            return None
        return args

    def _run_esptool(self, args: list[str], title: str):
        self.flash_process = QProcess(self)
        self.flash_process.setWorkingDirectory(str(PROJECT_ROOT))
        self.flash_process.readyReadStandardOutput.connect(self._read_flash_stdout)
        self.flash_process.readyReadStandardError.connect(self._read_flash_stderr)
        self.flash_process.finished.connect(self._on_flash_finished)
        self.btn_flash_start.setEnabled(False)
        self.btn_flash_stop.setEnabled(True)
        program, process_args, display = self._esptool_process_args(args)
        self._flash_log(f"[{title}] {display}")
        self.flash_process.start(program, process_args)
        if not self.flash_process.waitForStarted(3000):
            self._flash_log("無法啟動 esptool，請確認相依套件已安裝")
            self._set_flash_idle()

    def _esptool_process_args(self, args: list[str]) -> tuple[str, list[str], str]:
        if getattr(sys, "frozen", False):
            process_args = ["--esptool", *args]
            return sys.executable, process_args, f"{Path(sys.executable).name} {' '.join(process_args)}"
        process_args = ["-m", "esptool", *args]
        return sys.executable, process_args, f"python -m esptool {' '.join(args)}"

    def _read_flash_stdout(self):
        if not self.flash_process:
            return
        text = bytes(self.flash_process.readAllStandardOutput()).decode("utf-8", errors="ignore")
        self._flash_log(text.rstrip())

    def _read_flash_stderr(self):
        if not self.flash_process:
            return
        text = bytes(self.flash_process.readAllStandardError()).decode("utf-8", errors="ignore")
        self._flash_log(text.rstrip())

    def _on_flash_finished(self, exit_code: int, _status):
        if exit_code == 0 and self._pending_flash_args:
            args = self._pending_flash_args
            self._pending_flash_args = None
            self._flash_log("清除完成，開始寫入韌體")
            self._run_esptool(args, "燒錄韌體")
            return
        if exit_code == 0:
            self._flash_log("燒錄完成，請重新啟動 ESP32")
        else:
            self._flash_log(f"燒錄失敗，exit code={exit_code}")
        self._pending_flash_args = None
        self._set_flash_idle()

    def _stop_flash(self):
        if self.flash_process and self.flash_process.state() != QProcess.ProcessState.NotRunning:
            self._pending_flash_args = None
            self.flash_process.kill()
            self._flash_log("已停止燒錄程序")
        self._set_flash_idle()

    def _set_flash_idle(self):
        self.btn_flash_start.setEnabled(True)
        self.btn_flash_stop.setEnabled(False)

    def _flash_log(self, text: str):
        if not text:
            return
        self.flash_log.appendPlainText(text)

    # ------------------------------------------------------------------
    # 感測器分頁（參考 data/hardware_test.html，純即時測試、不做報表）
    # ------------------------------------------------------------------

    def _build_sensor_tab(self) -> QWidget:
        w = QWidget()
        grid = QGridLayout(w)
        for col, head in enumerate(("感測器", "腳位", "", "", "即時值")):
            if head:
                grid.addWidget(QLabel(f"<b>{head}</b>"), 0, col)

        self._sensor_cfg: dict[str, dict] = {}

        self.s_btn_gpio = self._gpio_spin(4)
        self._add_sensor_row(
            grid, 1, "數位按鈕 (PULLUP)", [self.s_btn_gpio], "button",
            lambda: {"mode": "hardwareTest", "test": "buttonReading",
                     "gpio": self.s_btn_gpio.value()})

        self.s_dig_gpio = self._gpio_spin(15)
        self._add_sensor_row(
            grid, 2, "數位輸入", [self.s_dig_gpio], "digital",
            lambda: {"mode": "hardwareTest", "test": "digitalReading",
                     "gpio": self.s_dig_gpio.value()})

        self.s_ana_gpio = self._gpio_spin(32)
        self._add_sensor_row(
            grid, 3, "類比輸入 (ADC)", [self.s_ana_gpio], "analog",
            lambda: {"mode": "hardwareTest", "test": "analogReading",
                     "gpio": self.s_ana_gpio.value()})

        self.s_us_trig = self._gpio_spin(2)
        self.s_us_echo = self._gpio_spin(33)
        self._add_sensor_row(
            grid, 4, "超音波 (Trig/Echo)", [self.s_us_trig, self.s_us_echo], "ultrasonic",
            lambda: {"mode": "hardwareTest", "test": "ultrasonicReading",
                     "trig": self.s_us_trig.value(), "echo": self.s_us_echo.value()})

        note = QLabel("提示：輪詢約 4Hz；按鈕/數位回 0/1，類比回 0–4095，超音波回距離 (cm)。需先連線。")
        note.setStyleSheet("color:#666;")
        grid.addWidget(note, 5, 0, 1, 5)
        grid.setRowStretch(6, 1)
        return w

    def _gpio_spin(self, val: int) -> QSpinBox:
        s = QSpinBox()
        s.setRange(0, 39)
        s.setValue(val)
        s.setMinimumWidth(78)
        return s

    def _add_sensor_row(self, grid, row, title, gpio_widgets, key, builder):
        grid.addWidget(QLabel(title), row, 0)
        box = QWidget()
        bl = QHBoxLayout(box)
        bl.setContentsMargins(0, 0, 0, 0)
        for gw in gpio_widgets:
            bl.addWidget(gw)
        bl.addStretch()
        grid.addWidget(box, row, 1)
        start_btn = QPushButton("開始")
        stop_btn = QPushButton("停止")
        stop_btn.setEnabled(False)
        start_btn.clicked.connect(lambda: self._start_sensor(key))
        stop_btn.clicked.connect(lambda: self._stop_sensor(key))
        grid.addWidget(start_btn, row, 2)
        grid.addWidget(stop_btn, row, 3)
        value_lbl = QLabel("—")
        value_lbl.setStyleSheet("font-family:Consolas; font-weight:bold; color:#2d5be3;")
        grid.addWidget(value_lbl, row, 4)
        timer = QTimer(self)
        timer.setInterval(250)
        timer.timeout.connect(lambda: self._poll_sensor(key))
        self._sensor_cfg[key] = {
            "builder": builder, "timer": timer, "value": value_lbl,
            "start": start_btn, "stop": stop_btn,
        }

    def _start_sensor(self, key: str):
        if not self.active.is_connected():
            self._log("未連線，無法開始感測器測試")
            return
        c = self._sensor_cfg[key]
        c["timer"].start()
        c["start"].setEnabled(False)
        c["stop"].setEnabled(True)

    def _stop_sensor(self, key: str):
        c = self._sensor_cfg[key]
        c["timer"].stop()
        c["start"].setEnabled(True)
        c["stop"].setEnabled(False)

    def _stop_all_sensors(self):
        for key in getattr(self, "_sensor_cfg", {}):
            self._stop_sensor(key)

    def _poll_sensor(self, key: str):
        self.active.send_command(self._sensor_cfg[key]["builder"]())

    def _build_wiring_tab(self) -> QWidget:
        """純顯示分頁：移植自 data/wiring.html 的腳位/接線參考。"""
        browser = QTextBrowser()
        browser.setOpenExternalLinks(False)
        # 讓 <img src="wemos.jpg"> 解析：打包後從 _MEIPASS/data 取，原始碼模式從 repo data/ 取
        browser.setSearchPaths([str(BUNDLE_ROOT / "data"), str(PROJECT_ROOT / "data")])
        browser.setHtml(self._wiring_html())
        return browser

    @staticmethod
    def _wiring_html() -> str:
        def sw(color):  # LEGO 線色色塊
            return (f'<span style="background-color:{color};">&nbsp;&nbsp;</span>')

        return f"""
        <h2 style="color:#4d7fff;">🔌 接線說明</h2>
        <p align="center">
          <img src="wemos.jpg" width="360"><br>
          <font size="2" color="#718096">WeMos D1 R32（ESP32）— 本系統使用的控制板</font>
        </p>

        <h3 style="color:#276749;">🟢 舵機 Servo</h3>
        <table border="1" cellpadding="5" cellspacing="0" width="100%">
          <tr bgcolor="#e8f5e9"><th>通道</th><th>GPIO</th><th>脈衝範圍</th><th>LEDC 通道</th></tr>
          <tr><td>S1（Servo 1）</td><td><tt>5</tt></td><td>500–2500 µs</td><td>ch 14</td></tr>
          <tr><td>S2（Servo 2）</td><td><tt>13</tt></td><td>500–2500 µs</td><td>ch 15</td></tr>
        </table>
        <p><font size="2">指令格式：<tt>{{"cmd":"servo","ch":1,"deg":90}}</tt>，角度 0–180°。</font></p>

        <h3 style="color:#6b21a8;">🟣 編碼器（M3 / M4）</h3>
        <table border="1" cellpadding="5" cellspacing="0" width="100%">
          <tr bgcolor="#f3e8ff"><th>馬達</th><th>相</th><th>GPIO</th><th>備註</th></tr>
          <tr><td>M3</td><td>A</td><td><tt>35</tt></td><td>輸入專用腳（無內部上拉）</td></tr>
          <tr><td>M3</td><td>B</td><td><tt>34</tt></td><td>輸入專用腳（無內部上拉）</td></tr>
          <tr><td>M4</td><td>A</td><td><tt>36</tt></td><td>輸入專用腳（無內部上拉）</td></tr>
          <tr><td>M4</td><td>B</td><td><tt>39</tt></td><td>輸入專用腳（無內部上拉）</td></tr>
        </table>
        <p><font size="2">GPIO 34/35/36/39 為 ESP32 輸入專用腳，需自加上拉（3.3V ↔ 10kΩ ↔ 訊號）。
        2X 解碼，每度 = 2 ticks（預設 PPR=360）。</font></p>

        <h3 style="color:#1a56a0;">🔵 使用者可用腳位（Blockly / 感測器）</h3>
        <table border="1" cellpadding="5" cellspacing="0" width="100%">
          <tr bgcolor="#e8f4fd"><th>GPIO</th><th>類型</th><th>預設功能</th><th>備註</th></tr>
          <tr><td><tt>2</tt></td><td>數位 I/O</td><td>數位輸出 / 超音波 Trig</td><td>板子 LED；開機低電位</td></tr>
          <tr><td><tt>4</tt></td><td>數位輸入</td><td>樂高按鈕</td><td>INPUT_PULLUP</td></tr>
          <tr><td><tt>15</tt></td><td>數位輸入</td><td>數位讀取</td><td>Strapping pin，開機勿強拉</td></tr>
          <tr><td><tt>26</tt></td><td>PWM / DAC</td><td>PWM / 類比輸出</td><td>支援 DAC</td></tr>
          <tr><td><tt>32</tt></td><td>數位 / ADC</td><td>類比讀取</td><td>可改作 I2C SDA</td></tr>
          <tr><td><tt>33</tt></td><td>數位 / ADC</td><td>超音波 Echo</td><td>可改作 I2C SCL</td></tr>
        </table>

        <h3 style="color:#7d6608;">🧱 樂高 NXT/EV3 馬達接頭（RJ12 6P6C）</h3>
        <table border="1" cellpadding="5" cellspacing="0" width="100%">
          <tr bgcolor="#fef9e7"><th>腳位</th><th>線色</th><th>訊號</th><th>說明</th></tr>
          <tr><td>Pin 1</td><td>{sw('#ffffff')} 白</td><td>MA0</td><td>馬達電源 A → 驅動板 OUT1</td></tr>
          <tr><td>Pin 2</td><td>{sw('#222222')} 黑</td><td>MA1</td><td>馬達電源 B → 驅動板 OUT2</td></tr>
          <tr><td>Pin 3</td><td>{sw('#e53e3e')} 紅</td><td>GND</td><td>編碼器/邏輯接地 → 系統 GND</td></tr>
          <tr><td>Pin 4</td><td>{sw('#38a169')} 綠</td><td>VCC</td><td>編碼器電源 4.3–5V</td></tr>
          <tr><td>Pin 5</td><td>{sw('#d69e2e')} 黃</td><td>TA（A相）</td><td>編碼器 A → GPIO 35(M3) / 36(M4)</td></tr>
          <tr><td>Pin 6</td><td>{sw('#3182ce')} 藍</td><td>TB（B相）</td><td>編碼器 B → GPIO 34(M3) / 39(M4)</td></tr>
        </table>
        <p><font size="2">水晶頭正面（金屬片朝上、卡榫朝下）由左至右為 Pin 1→6。
        NXT 馬達每圈約 360 脈衝，2X 解碼後每圈 720 ticks。
        直流馬達電壓最高 9V，須透過驅動板（L298N / TB6612FNG / AFMotor Shield）控制。</font></p>

        <h3 style="color:#c53030;">🚫 禁止外接（已被系統佔用）</h3>
        <table border="1" cellpadding="5" cellspacing="0" width="100%">
          <tr bgcolor="#fff5f5"><th>GPIO</th><th>佔用原因</th></tr>
          <tr><td><tt>12 14 16 17 19 23 25 27</tt></td><td>AFMotor Shield 74HCT595 + M1–M4 PWM</td></tr>
          <tr><td><tt>5 13</tt></td><td>Servo S1 / S2</td></tr>
          <tr><td><tt>34 35 36 39</tt></td><td>M3/M4 編碼器（輸入專用腳）</td></tr>
        </table>
        """

    def _build_connection_group(self) -> QGroupBox:
        g = QGroupBox("連線")
        h = QHBoxLayout(g)

        # 傳輸選擇
        self.rb_wifi = QRadioButton("WiFi (WS)")
        self.rb_usb = QRadioButton("USB (序列)")
        self.rb_usb.setChecked(True)
        self.rb_wifi.toggled.connect(self._select_transport)
        h.addWidget(self.rb_wifi)
        h.addWidget(self.rb_usb)

        # 依傳輸切換的輸入（WiFi: IP / USB: COM 埠）
        self.conn_stack = QStackedWidget()
        # page 0: WiFi
        wifi_page = QWidget()
        wl = QHBoxLayout(wifi_page)
        wl.setContentsMargins(0, 0, 0, 0)
        wl.addWidget(QLabel("ESP32 IP:"))
        self.ip_edit = QLineEdit(self.settings.value("ip", "192.168.4.1"))
        self.ip_edit.setFixedWidth(150)
        wl.addWidget(self.ip_edit)
        self.conn_stack.addWidget(wifi_page)
        # page 1: USB
        usb_page = QWidget()
        ul = QHBoxLayout(usb_page)
        ul.setContentsMargins(0, 0, 0, 0)
        ul.addWidget(QLabel("COM 埠:"))
        self.port_combo = QComboBox()
        self.port_combo.setMinimumWidth(260)
        self.port_combo.setSizeAdjustPolicy(QComboBox.SizeAdjustPolicy.AdjustToContents)
        self.port_combo.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        self.port_combo.currentIndexChanged.connect(self._update_port_tooltip)
        self.port_combo.view().setMinimumWidth(420)
        ul.addWidget(self.port_combo)
        btn_refresh = QPushButton("重新整理")
        btn_refresh.clicked.connect(self._refresh_ports)
        ul.addWidget(btn_refresh)
        ul.setStretch(1, 1)
        self.conn_stack.addWidget(usb_page)
        h.addWidget(self.conn_stack, 1)

        self.btn_connect = QPushButton("連線")
        self.btn_connect.clicked.connect(self._toggle_connection)
        h.addWidget(self.btn_connect)
        self.status_lbl = QLabel("● 未連線")
        self.status_lbl.setStyleSheet("color: #b00;")
        h.addWidget(self.status_lbl)
        h.addStretch()
        self._refresh_ports()
        self._select_transport()
        return g

    def _refresh_ports(self):
        cur = self._current_serial_port()
        saved = str(self.settings.value("serial_port", "") or "").strip()
        self._populate_port_combo(self.port_combo, saved, cur)
        self._update_port_tooltip()

    def _populate_port_combo(self, combo: QComboBox, saved: str = "", current: str = ""):
        combo.clear()
        detected = SerialWorker.auto_detect_port()
        for p in SerialWorker.list_port_infos():
            label = p.device
            desc = getattr(p, "description", "") or ""
            if desc and desc != "n/a":
                label = f"{p.device} - {desc}"
            combo.addItem(label, p.device)
            combo.setItemData(combo.count() - 1, label, Qt.ItemDataRole.ToolTipRole)
        for wanted in (current, saved, detected):
            if not wanted:
                continue
            idx = combo.findData(wanted)
            if idx >= 0:
                combo.setCurrentIndex(idx)
                return

    def _current_serial_port(self) -> str:
        port = self.port_combo.currentData()
        if port:
            return str(port).strip()
        return self.port_combo.currentText().split(" - ", 1)[0].strip()

    def _update_port_tooltip(self, _index=None):
        self.port_combo.setToolTip(self.port_combo.currentText())

    def _select_transport(self):
        if self.ws.is_connected() or self.serial.is_connected():
            return  # 連線中不可切換
        use_wifi = self.rb_wifi.isChecked()
        self.active = self.ws if use_wifi else self.serial
        self.conn_stack.setCurrentIndex(0 if use_wifi else 1)

    def _build_motor_group(self) -> QGroupBox:
        g = QGroupBox("單馬達測試")
        v = QVBoxLayout(g)

        # 馬達選擇
        sel = QHBoxLayout()
        sel.addWidget(QLabel("馬達:"))
        self.motor_group = QButtonGroup(self)
        for i in range(1, NUM_MOTORS + 1):
            rb = QRadioButton(f"M{i}")
            rb.setChecked(i == 1)
            rb.clicked.connect(lambda _=False, m=i: self._select_motor(m))
            self.motor_group.addButton(rb, i)
            sel.addWidget(rb)
        sel.addStretch()
        v.addLayout(sel)
        self.cap_lbl = QLabel("")
        self.cap_lbl.setStyleSheet("color: #666;")
        v.addWidget(self.cap_lbl)

        # 開迴路 PWM
        pwm = QHBoxLayout()
        pwm.addWidget(QLabel("PWM duty (±100):"))
        self.pwm_spin = QSpinBox()
        self.pwm_spin.setRange(-100, 100)
        self.pwm_spin.setValue(50)
        pwm.addWidget(self.pwm_spin)
        b = QPushButton("送出 PWM")
        b.clicked.connect(self._send_pwm)
        pwm.addWidget(b)
        v.addLayout(pwm)

        # 閉迴路轉速（M3/M4）
        spd = QHBoxLayout()
        spd.addWidget(QLabel("轉速 RPM:"))
        self.rpm_spin = QSpinBox()
        self.rpm_spin.setRange(-250, 250)
        self.rpm_spin.setValue(120)
        spd.addWidget(self.rpm_spin)
        self.btn_speed = QPushButton("送出轉速")
        self.btn_speed.clicked.connect(self._send_speed)
        spd.addWidget(self.btn_speed)
        v.addLayout(spd)

        # 位置（M3/M4）
        pos = QHBoxLayout()
        pos.addWidget(QLabel("角度 deg:"))
        self.deg_spin = QSpinBox()
        self.deg_spin.setRange(-3600, 3600)
        self.deg_spin.setValue(90)
        pos.addWidget(self.deg_spin)
        self.btn_moveto = QPushButton("move_to")
        self.btn_moveto.clicked.connect(lambda: self._send_move("move_to"))
        pos.addWidget(self.btn_moveto)
        self.btn_moveby = QPushButton("move_by")
        self.btn_moveby.clicked.connect(lambda: self._send_move("move_by"))
        pos.addWidget(self.btn_moveby)
        self.btn_zero = QPushButton("歸零")
        self.btn_zero.clicked.connect(self._send_zero)
        pos.addWidget(self.btn_zero)
        v.addLayout(pos)

        # 停止
        stop = QHBoxLayout()
        b = QPushButton("停止此馬達")
        b.clicked.connect(self._send_stop)
        stop.addWidget(b)
        b = QPushButton("全部停止")
        b.clicked.connect(self._send_stop_all)
        stop.addWidget(b)
        v.addLayout(stop)
        v.addStretch()
        return g

    def _build_pid_group(self) -> QGroupBox:
        g = QGroupBox("PID 調教")
        grid = QGridLayout(g)

        # 速度環
        grid.addWidget(QLabel("<b>速度環</b>"), 0, 0, 1, 4)
        self.kp = self._dspin(2.0, decimals=4)
        self.ki = self._dspin(0.0, decimals=4)
        self.kd = self._dspin(0.1, decimals=4)
        grid.addWidget(QLabel("Kp"), 1, 0)
        grid.addWidget(self.kp, 1, 1)
        grid.addWidget(QLabel("Ki"), 1, 2)
        grid.addWidget(self.ki, 1, 3)
        grid.addWidget(QLabel("Kd"), 2, 0)
        grid.addWidget(self.kd, 2, 1)
        self.min_spd = QSpinBox()
        self.min_spd.setRange(0, 255)
        self.min_spd.setValue(60)
        self.min_spd.setToolTip(
            "速度閉迴路輸出的最小 PWM 限制，不是 RPM。\n"
            "韌體會換算為 minDuty = minSpeed * 100 / 255。\n"
            "用途是讓馬達輸出時至少有足夠力道克服靜摩擦。"
        )
        self.max_spd = QSpinBox()
        self.max_spd.setRange(0, 255)
        self.max_spd.setValue(250)
        self.max_spd.setToolTip(
            "速度閉迴路輸出的最大 PWM 限制，不是 RPM。\n"
            "韌體會換算為 maxDuty = maxSpeed * 100 / 255。\n"
            "用途是限制速度控制最大力道，避免馬達或機構衝太猛。"
        )
        min_speed_lbl = QLabel("minSpeed")
        min_speed_lbl.setToolTip(self.min_spd.toolTip())
        max_speed_lbl = QLabel("maxSpeed")
        max_speed_lbl.setToolTip(self.max_spd.toolTip())
        grid.addWidget(min_speed_lbl, 3, 0)
        grid.addWidget(self.min_spd, 3, 1)
        grid.addWidget(max_speed_lbl, 3, 2)
        grid.addWidget(self.max_spd, 3, 3)
        self.speed_ff_enabled = QCheckBox("前饋 kS")
        self.speed_ff_enabled.setToolTip(
            "啟用速度前饋。啟用後韌體會先依目標 RPM 給基礎 PWM，PID 只修正剩餘誤差。"
        )
        self.speed_ff_enabled.setChecked(False)
        self.speed_ff_ks = self._dspin(0.0, decimals=5)
        self.speed_ff_ks.setRange(-0.5, 1.0)  # 允許小負截距（線性擬合於非零轉速區間）
        self.speed_ff_ks.setSingleStep(0.01)
        self.speed_ff_ks.setToolTip(
            "kS 是起轉/靜摩擦補償，單位是 duty 比例 0.0~1.0。\n"
            "例如 0.144 代表前饋會先給約 14.4% PWM。"
        )
        self.speed_ff_kv = self._dspin(0.0, decimals=6)
        self.speed_ff_kv.setRange(0.0, 1.0)
        self.speed_ff_kv.setSingleStep(0.0001)
        self.speed_ff_kv.setToolTip(
            "kV 是每 1 RPM 需要增加的 duty 比例。\n"
            "前饋公式: duty = kS + kV * abs(目標RPM)。"
        )
        kv_lbl = QLabel("kV")
        kv_lbl.setToolTip(self.speed_ff_kv.toolTip())
        grid.addWidget(self.speed_ff_enabled, 4, 0)
        grid.addWidget(self.speed_ff_ks, 4, 1)
        grid.addWidget(kv_lbl, 4, 2)
        grid.addWidget(self.speed_ff_kv, 4, 3)

        # 位置環
        grid.addWidget(QLabel("<b>位置環</b>"), 5, 0, 1, 4)
        self.pos_kp = self._dspin(1.0, decimals=4)
        self.pos_ki = self._dspin(0.0, decimals=4)
        self.pos_kd = self._dspin(0.05, decimals=4)
        grid.addWidget(QLabel("posKp"), 6, 0)
        grid.addWidget(self.pos_kp, 6, 1)
        grid.addWidget(QLabel("posKi"), 6, 2)
        grid.addWidget(self.pos_ki, 6, 3)
        grid.addWidget(QLabel("posKd"), 7, 0)
        grid.addWidget(self.pos_kd, 7, 1)
        self.pos_max_duty = QSpinBox()
        self.pos_max_duty.setRange(0, 100)
        self.pos_max_duty.setValue(60)
        self.pos_tol = self._dspin(3.0)
        grid.addWidget(QLabel("posMaxDuty"), 8, 0)
        grid.addWidget(self.pos_max_duty, 8, 1)
        grid.addWidget(QLabel("posTolDeg"), 8, 2)
        grid.addWidget(self.pos_tol, 8, 3)

        # 參數生命週期（對速度環＋位置環全部欄位一起操作；NVS 以 motor 命名空間整批存取）
        life = QHBoxLayout()
        for text, slot in (
            ("讀取", self._read_config),
            ("套用RAM", self._apply_config),
            ("寫入NVS", self._write_config),
            ("還原預設", self._reset_config),
        ):
            btn = QPushButton(text)
            btn.clicked.connect(slot)
            life.addWidget(btn)
        life_w = QWidget()
        life_w.setLayout(life)
        grid.addWidget(life_w, 9, 0, 1, 4)
        self.pid_hint = QLabel("（連線後自動讀取目前值）")
        self.pid_hint.setStyleSheet("color:#666;")
        grid.addWidget(self.pid_hint, 10, 0, 1, 4)
        grid.setRowStretch(11, 1)
        return g

    def _build_auto_pid_group(self) -> QGroupBox:
        g = QGroupBox("自動估速度 PID（兩點開迴路辨識）")
        v = QVBoxLayout(g)

        hint = QLabel("開迴路掃描多個 PWM 準位，擬合馬達增益與時間常數後填入速度/角度 PID 參考值。")
        hint.setWordWrap(True)
        hint.setStyleSheet("color:#666;")
        v.addWidget(hint)

        grid = QGridLayout()
        grid.addWidget(QLabel("高準位 PWM(%)"), 0, 0)
        self.spin_autotune_duty = QDoubleSpinBox()
        self.spin_autotune_duty.setRange(35, 95)
        self.spin_autotune_duty.setDecimals(2)
        self.spin_autotune_duty.setSingleStep(5)
        self.spin_autotune_duty.setValue(85)
        grid.addWidget(self.spin_autotune_duty, 0, 1)

        grid.addWidget(QLabel("響應"), 1, 0)
        self.combo_autotune_aggr = QComboBox()
        self.combo_autotune_aggr.addItem("保守（穩）", 1.0)
        self.combo_autotune_aggr.addItem("中等", 0.5)
        self.combo_autotune_aggr.addItem("積極（快）", 0.33)
        grid.addWidget(self.combo_autotune_aggr, 1, 1)
        v.addLayout(grid)

        btns = QHBoxLayout()
        self.btn_autotune_speed = QPushButton("開始自動估")
        self.btn_autotune_speed.clicked.connect(self.on_btn_autotune_speed)
        btns.addWidget(self.btn_autotune_speed)
        v.addLayout(btns)

        self.lbl_autotune_status = QLabel("狀態：待命")
        self.lbl_autotune_status.setWordWrap(True)
        self.lbl_autotune_status.setStyleSheet("color:#555;")
        v.addWidget(self.lbl_autotune_status)

        self.btn_fill_speed_pid = QPushButton("填入速度 PID 欄位")
        self.btn_fill_speed_pid.setEnabled(False)
        self.btn_fill_speed_pid.clicked.connect(self._fill_autotune_speed_pid)
        v.addWidget(self.btn_fill_speed_pid)

        pos_row = QHBoxLayout()
        pos_row.addWidget(QLabel("角度環(Pos):"))
        self.btn_autotune_pos = QPushButton("自動估角度 PID")
        self.btn_autotune_pos.clicked.connect(self.on_btn_autotune_pos)
        pos_row.addWidget(self.btn_autotune_pos, 1)
        v.addLayout(pos_row)

        self.lbl_autotune_pos_status = QLabel("狀態：待命（需先做速度自動估）")
        self.lbl_autotune_pos_status.setWordWrap(True)
        self.lbl_autotune_pos_status.setStyleSheet("color:#555;")
        v.addWidget(self.lbl_autotune_pos_status)

        self.btn_fill_pos_pid = QPushButton("填入角度 PID 欄位")
        self.btn_fill_pos_pid.setEnabled(False)
        self.btn_fill_pos_pid.clicked.connect(self._fill_autotune_pos_pid)
        v.addWidget(self.btn_fill_pos_pid)
        v.addStretch()
        return g

    def _dspin(self, val: float, decimals: int = 3) -> QDoubleSpinBox:
        s = QDoubleSpinBox()
        s.setDecimals(decimals)
        s.setRange(0.0, 1000.0)
        s.setSingleStep(0.1)
        s.setValue(val)
        return s

    # ------------------------------------------------------------------
    # 連線
    # ------------------------------------------------------------------

    # ------------------------------------------------------------------
    # 設定分頁（移植自 data/set.html）
    # ------------------------------------------------------------------

    def _build_settings_tab(self) -> QWidget:
        w = QWidget()
        cols = QHBoxLayout(w)

        # 左欄：馬達參數（motor 命名空間，即時生效、不重啟）
        left = QVBoxLayout()
        left_head = QLabel("<b>馬達參數</b>（即時生效，不重啟）")
        left.addWidget(left_head)
        left.addWidget(self._build_hw_group())
        left.addWidget(self._build_hold_group())

        # 硬體 / Hold 與 PID 共用同一份 NVS（motor 命名空間），故共用生命週期按鈕
        life = QHBoxLayout()
        for text, slot in (
            ("讀取", self._read_config),
            ("套用RAM", self._apply_config),
            ("寫入NVS", self._write_config),
            ("還原預設", self._reset_config),
        ):
            b = QPushButton(text)
            b.clicked.connect(slot)
            life.addWidget(b)
        left.addLayout(life)

        # 參數檔匯出/匯入（換板移植調校參數用；只含馬達參數，不碰 WiFi / 板名）
        xfer_box = QGroupBox("參數檔（換板移植）")
        xfer_v = QVBoxLayout(xfer_box)
        xfer_desc = QLabel(
            "把一塊板調好的馬達參數（PID／前饋／硬體種類／角度保持，共 21 項）複製到新板。"
            "<b>只含馬達參數，不含 WiFi／板名</b>，所以複製參數不會改到新板的名稱。"
        )
        xfer_desc.setWordWrap(True)
        xfer_desc.setStyleSheet("color:#555; font-size:11px;")
        xfer_v.addWidget(xfer_desc)

        xfer_btns = QHBoxLayout()
        btn_export = QPushButton("匯出參數")
        btn_export.setToolTip("把目前欄位的馬達參數存成 .json 檔，供另一塊板匯入。")
        btn_export.clicked.connect(self._export_config_file)
        btn_import = QPushButton("匯入參數")
        btn_import.setToolTip("從 .json 檔讀回參數，只填入欄位先檢視，尚未寫入板子。")
        btn_import.clicked.connect(self._import_config_file)
        btn_import_write = QPushButton("匯入並寫入NVS")
        btn_import_write.setToolTip("讀檔後直接套用並寫入目前連線板子的 NVS，不重啟（需先連線）。")
        btn_import_write.clicked.connect(self._import_and_write_config_file)
        for b in (btn_export, btn_import, btn_import_write):
            xfer_btns.addWidget(b)
        xfer_v.addLayout(xfer_btns)

        xfer_help = QLabel(
            "・<b>匯出參數</b>：把目前值存成 .json 範本檔。<br>"
            "・<b>匯入參數</b>：讀檔只填入欄位（不寫板子，可先核對）。<br>"
            "・<b>匯入並寫入NVS</b>：讀檔後直接寫入目前連線的這塊板（需先連線、不重啟）。<br>"
            "建議順序：先在右欄改名／設 WiFi（會重啟）→ 重連 → 再匯入參數。"
        )
        xfer_help.setWordWrap(True)
        xfer_help.setStyleSheet("color:#777; font-size:11px;")
        xfer_v.addWidget(xfer_help)
        left.addWidget(xfer_box)

        self.set_hint = QLabel(
            "（硬體 / Hold 與 PID 共用同一份 NVS，讀取/寫入會一起作用）"
        )
        self.set_hint.setWordWrap(True)
        self.set_hint.setStyleSheet("color:#666;")
        left.addWidget(self.set_hint)
        left.addStretch()

        # 右欄：控制板設定（wifi 命名空間，送出後會重啟）
        right = QVBoxLayout()
        right_head = QLabel("<b>控制板設定</b>（送出後會重啟）")
        right.addWidget(right_head)
        right.addWidget(self._build_wifi_group())
        right.addStretch()

        left_w = QWidget()
        left_w.setLayout(left)
        right_w = QWidget()
        right_w.setLayout(right)
        cols.addWidget(left_w, 1)
        cols.addWidget(right_w, 1)
        self._on_motor_type()
        return w

    def _build_wifi_group(self) -> QGroupBox:
        g = QGroupBox("WiFi / 控制板 AP 名稱")
        grid = QGridLayout(g)
        self.wifi_ssid = QLineEdit()
        self.wifi_ssid.setPlaceholderText("要讓 ESP32 連上的 WiFi 名稱")
        self.wifi_pwd = QLineEdit()
        self.wifi_pwd.setEchoMode(QLineEdit.EchoMode.Password)
        self.wifi_apname = QLineEdit()
        self.wifi_apname.setPlaceholderText("手機掃描到的控制板名稱；留空=不改")
        self.admin_pwd = QLineEdit("123456")
        self.admin_pwd.setEchoMode(QLineEdit.EchoMode.Password)
        grid.addWidget(QLabel("外部 WiFi SSID"), 0, 0)
        grid.addWidget(self.wifi_ssid, 0, 1)
        grid.addWidget(QLabel("外部 WiFi 密碼"), 1, 0)
        grid.addWidget(self.wifi_pwd, 1, 1)
        grid.addWidget(QLabel("控制板 AP 名稱"), 2, 0)
        grid.addWidget(self.wifi_apname, 2, 1)
        grid.addWidget(QLabel("管理密碼"), 3, 0)
        grid.addWidget(self.admin_pwd, 3, 1)

        mode_box = QGroupBox("更新項目")
        mode_layout = QVBoxLayout(mode_box)
        self.chk_update_wifi = QCheckBox("更新外部 WiFi")
        self.chk_update_name = QCheckBox("更新控制板 AP 名稱")
        self.chk_update_wifi.setChecked(True)
        self.chk_update_wifi.toggled.connect(self._on_wifi_update_mode)
        self.chk_update_name.toggled.connect(self._on_wifi_update_mode)
        mode_layout.addWidget(self.chk_update_wifi)
        mode_layout.addWidget(self.chk_update_name)
        grid.addWidget(mode_box, 4, 0, 1, 2)

        note = QLabel("送出後 ESP32 會寫入設定並重啟，USB/WiFi 連線會暫時中斷。")
        note.setWordWrap(True)
        note.setStyleSheet("color:#666;")
        grid.addWidget(note, 5, 0, 1, 2)

        h = QHBoxLayout()
        self.btn_wifi_update = QPushButton("更新設定")
        self.btn_wifi_update.clicked.connect(self._send_selected_wifi_update)
        h.addStretch()
        h.addWidget(self.btn_wifi_update)
        grid.addLayout(h, 6, 0, 1, 2)
        self._on_wifi_update_mode()
        return g

    def _on_wifi_update_mode(self):
        update_wifi = self.chk_update_wifi.isChecked()
        update_name = self.chk_update_name.isChecked()
        self.wifi_ssid.setEnabled(update_wifi)
        self.wifi_pwd.setEnabled(update_wifi)
        self.wifi_apname.setEnabled(update_name)
        self.btn_wifi_update.setEnabled(update_wifi or update_name)

    def _build_hw_group(self) -> QGroupBox:
        g = QGroupBox("馬達種類 + 硬體參數（未讀取前預設：樂高馬達）")
        v = QVBoxLayout(g)
        h = QHBoxLayout()
        self.rb_lego = QRadioButton("🧱 樂高馬達（預設）")
        self.rb_lego.setChecked(True)
        self.rb_hall = QRadioButton("⚙️ 霍爾馬達")
        self.rb_lego.toggled.connect(self._on_motor_type)
        h.addWidget(self.rb_lego)
        h.addWidget(self.rb_hall)
        h.addStretch()
        v.addLayout(h)
        self.motor_type_badge = QLabel("")
        self.motor_type_badge.setWordWrap(True)
        self.motor_type_badge.setStyleSheet(
            "padding:6px 8px; border-radius:4px; background:#edf7ed; "
            "color:#1f6f35; font-weight:bold;"
        )
        v.addWidget(self.motor_type_badge)
        self.motor_type_desc = QLabel("")
        self.motor_type_desc.setWordWrap(True)
        self.motor_type_desc.setStyleSheet("color:#555; font-size:11px;")
        v.addWidget(self.motor_type_desc)
        f = QHBoxLayout()
        f.addWidget(QLabel("編碼器 PPR"))
        self.hw_ppr = QSpinBox()
        self.hw_ppr.setRange(1, 100000)
        self.hw_ppr.setValue(360)
        f.addWidget(self.hw_ppr)
        self.ratio_label = QLabel("減速比")
        self.hw_ratio = QDoubleSpinBox()
        self.hw_ratio.setRange(0.1, 10000.0)
        self.hw_ratio.setDecimals(1)
        self.hw_ratio.setValue(48.0)
        f.addWidget(self.ratio_label)
        f.addWidget(self.hw_ratio)
        f.addStretch()
        v.addLayout(f)
        return g

    def _build_hold_group(self) -> QGroupBox:
        g = QGroupBox("角度保持 (Hold)")
        v = QVBoxLayout(g)
        intro = QLabel(
            "M3／M4 用 move_to／move_by 到位後，會持續用編碼器回授把軸夾在目標角度"
            "（伺服式保持，被外力推動會夾回）。以下三項決定保持的鬆緊與力道。"
        )
        intro.setWordWrap(True)
        intro.setStyleSheet("color:#555; font-size:11px;")
        v.addWidget(intro)

        f = QHBoxLayout()
        settle_tip = (
            "死區 settle°：誤差進到此範圍內就放鬆、歸零積分、不再出力。\n"
            "調大 → 更安靜、邊緣不抖，但允許的殘留誤差變大；\n"
            "調小 → 更精準但容易在邊緣嗡嗡作響。預設 1.5°。"
        )
        settle_lbl = QLabel("死區 settle°")
        settle_lbl.setToolTip(settle_tip)
        self.hold_settle = QDoubleSpinBox()
        self.hold_settle.setRange(0.2, 10.0)
        self.hold_settle.setDecimals(1)
        self.hold_settle.setValue(1.5)
        self.hold_settle.setToolTip(settle_tip)
        f.addWidget(settle_lbl)
        f.addWidget(self.hold_settle)

        ki_tip = (
            "創爬 KI：卡住推不動時，積分持續累加把力道慢慢頂上來、破靜摩擦再推進目標。\n"
            "殘留誤差收不掉 → 調高；收斂時頂過頭／來回獵動 → 調低。預設 8.0。"
        )
        ki_lbl = QLabel("創爬 KI")
        ki_lbl.setToolTip(ki_tip)
        self.hold_ki = QDoubleSpinBox()
        self.hold_ki.setRange(0.0, 50.0)
        self.hold_ki.setDecimals(1)
        self.hold_ki.setValue(8.0)
        self.hold_ki.setToolTip(ki_tip)
        f.addWidget(ki_lbl)
        f.addWidget(self.hold_ki)

        max_tip = (
            "夾持上限 PWM%：保持時輸出 duty 的上限，限制最大夾持力。\n"
            "頂外力沒力 → 調高；怕長時間頂死發熱或動作太猛 → 調低。預設 60%。"
        )
        max_lbl = QLabel("夾持上限 PWM%")
        max_lbl.setToolTip(max_tip)
        self.hold_max = QSpinBox()
        self.hold_max.setRange(10, 100)
        self.hold_max.setValue(60)
        self.hold_max.setToolTip(max_tip)
        f.addWidget(max_lbl)
        f.addWidget(self.hold_max)
        f.addStretch()
        v.addLayout(f)

        detail = QLabel(
            "・<b>死區 settle°</b>：進此範圍放鬆歸零；大→安靜但殘差大、小→精準但易嗡聲。<br>"
            "・<b>創爬 KI</b>：卡住時積分頂力破靜摩擦；殘差收不掉→調高、頂過頭→調低。<br>"
            "・<b>夾持上限 PWM%</b>：夾持力上限；沒力→調高、怕發熱→調低。"
        )
        detail.setWordWrap(True)
        detail.setStyleSheet("color:#777; font-size:11px;")
        v.addWidget(detail)
        return g

    def _on_motor_type(self):
        # 使用者切換馬達種類 → 帶入該種類的硬體預設
        # （樂高：PPR 360；霍爾：PPR 22、減速比 21.3，對齊 set.html）。
        # 注意：回填韌體設定時請改用 _update_motor_type_ui()，避免覆蓋讀回來的值。
        if self.rb_lego.isChecked():
            self.hw_ppr.setValue(360)
        else:
            self.hw_ppr.setValue(22)
            self.hw_ratio.setValue(21.3)
        self._update_motor_type_ui()

    def _update_motor_type_ui(self):
        lego = self.rb_lego.isChecked()
        self.ratio_label.setVisible(not lego)
        self.hw_ratio.setVisible(not lego)
        self.motor_type_badge.setText(
            "目前選擇：樂高馬達（系統預設）"
            if lego
            else "目前選擇：霍爾馬達"
        )
        self.motor_type_badge.setStyleSheet(
            "padding:6px 8px; border-radius:4px; font-weight:bold; "
            + (
                "background:#edf7ed; color:#1f6f35;"
                if lego
                else "background:#eef4ff; color:#1d4f91;"
            )
        )
        self.motor_type_desc.setText(
            "樂高馬達：這是畫面與韌體的預設選項；編碼器在輸出軸，角度用查找表，PPR 填輸出軸每圈脈衝數。"
            if lego
            else "霍爾馬達：霍爾感測器在馬達軸，需填馬達軸 PPR 與正確減速比。"
        )

    def _send_set_wifi(self, include_name: bool = True):
        ssid = self.wifi_ssid.text().strip()
        if not ssid:
            self._log("請輸入 WiFi SSID")
            return
        name = self.wifi_apname.text().strip()
        if include_name and not name:
            self._log("請輸入控制板 AP 名稱，或取消勾選「更新控制板 AP 名稱」")
            return
        action = "更新 WiFi 與 AP 名稱" if include_name else "更新 WiFi"
        if not self._confirm_reboot_action(action):
            return
        self.active.send_command({
            "cmd": "set_wifi",
            "ssid": ssid,
            "password": self.wifi_pwd.text(),
            "apName": name if include_name else "",
            "adminPassword": self.admin_pwd.text(),
        })
        self._log(f"已送出 {action} → 裝置將重啟、連線會中斷")

    def _send_selected_wifi_update(self):
        update_wifi = self.chk_update_wifi.isChecked()
        update_name = self.chk_update_name.isChecked()
        if not update_wifi and not update_name:
            self._log("請至少勾選一個更新項目")
            return
        if update_wifi:
            self._send_set_wifi(include_name=update_name)
        elif update_name:
            self._send_set_name()

    def _send_set_name(self):
        name = self.wifi_apname.text().strip()
        if not name:
            self._log("請輸入控制板名稱")
            return
        if not self._confirm_reboot_action("只更新 AP 名稱"):
            return
        self.active.send_command({
            "cmd": "set_name",
            "apName": name,
            "adminPassword": self.admin_pwd.text(),
        })
        self._log("已送出 AP 名稱更新 → 裝置將重啟、連線會中斷")

    def _confirm_reboot_action(self, action: str) -> bool:
        ret = QMessageBox.question(
            self,
            action,
            f"{action}後，ESP32 會重啟，連線會暫時中斷。\n\n確定要送出嗎？",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        return ret == QMessageBox.StandardButton.Yes

    def _toggle_connection(self):
        if self.active.is_connected():
            self.active.disconnect_ws() if self.active is self.ws else self.active.disconnect_serial()
            return
        if self.active is self.ws:
            ip = self.ip_edit.text().strip()
            if not ip:
                self._log("請輸入 IP")
                return
            self.settings.setValue("ip", ip)
            self.ws.connect_ws(ip)
        else:
            port = self._current_serial_port()
            if not port:
                self._refresh_ports()
                port = self._current_serial_port()
            if not port:
                self._log("請選擇 COM 埠")
                return
            self.settings.setValue("serial_port", port)
            self.serial.connect_serial(port)

    def _on_connected(self, url: str):
        self._set_connected_ui(True)
        # 連線後查能力、開啟遙測、讀取目前參數回填
        self.active.send_command({"cmd": "capabilities"})
        self.active.send_command({"cmd": "subscribe", "on": True})
        self.active.send_command({"cmd": "read_config"})

    def _on_disconnected(self):
        self._stop_all_sensors()
        self._set_connected_ui(False)

    def _set_connected_ui(self, on: bool):
        self.btn_connect.setText("斷線" if on else "連線")
        self.status_lbl.setText("● 已連線" if on else "● 未連線")
        self.status_lbl.setStyleSheet("color: #080;" if on else "color: #b00;")
        # 連線中不可切換傳輸
        self.rb_wifi.setEnabled(not on)
        self.rb_usb.setEnabled(not on)

    # ------------------------------------------------------------------
    # 馬達選擇與能力
    # ------------------------------------------------------------------

    def _select_motor(self, motor: int):
        self.current_motor = motor
        self._refresh_caps()

    def _refresh_caps(self):
        has_enc = self.has_encoder.get(self.current_motor, None)
        if has_enc is None:
            self.cap_lbl.setText(f"M{self.current_motor}：能力未知（連線後查詢）")
            enable_closed = True
        elif has_enc:
            self.cap_lbl.setText(f"M{self.current_motor}：有編碼器 → 支援轉速/位置閉迴路")
            enable_closed = True
        else:
            self.cap_lbl.setText(f"M{self.current_motor}：無編碼器 → 僅 PWM 開迴路")
            enable_closed = False
        for w in (
            self.btn_speed,
            self.btn_moveto,
            self.btn_moveby,
            self.btn_zero,
            self.rpm_spin,
            self.deg_spin,
        ):
            w.setEnabled(enable_closed)

    # ------------------------------------------------------------------
    # 送出指令（同時驅動圖表記錄）
    # ------------------------------------------------------------------

    def _send_pwm(self):
        m = self.current_motor
        self.chart.set_target_rpm(m, 0.0)
        self.chart.begin_new_run("pwm")
        self.chart.start_recording()
        self.active.send_command({"cmd": "pwm", "motor": m, "duty": self.pwm_spin.value()})

    def _send_speed(self):
        m = self.current_motor
        rpm = self.rpm_spin.value()
        self.chart.rb_rpm.setChecked(True)
        self.chart.set_target_rpm(m, float(rpm))
        self.chart.begin_new_run("speed")
        self.chart.start_recording()
        self.active.send_command({"cmd": "speed", "motor": m, "rpm": rpm})

    def _send_move(self, verb: str):
        m = self.current_motor
        deg = self.deg_spin.value()
        self.chart.rb_pos.setChecked(True)
        self.chart.set_target_deg(m, float(deg))
        self.chart.begin_new_run("position")
        self.chart.start_recording()
        # 韌體 deg 欄位單位為「度 × 100」整數（固定小數點）
        self.active.send_command({"cmd": verb, "motor": m, "deg": int(deg * 100)})

    def _send_zero(self):
        self.active.send_command({"cmd": "zero", "motor": self.current_motor})

    def _send_stop(self):
        self.active.send_command({"cmd": "stop", "motor": self.current_motor})
        self.chart.stop_recording()

    def _send_stop_all(self):
        self.active.send_command({"cmd": "stop"})
        self.chart.stop_recording()

    AUTOTUNE_LEVEL_DUR = 0.9
    AUTOTUNE_LEVEL_FACTORS = (0.5, 0.65, 0.8, 0.95)
    POS_N_BY_LAMBDA = {1.0: 5.0, 0.5: 3.5, 0.33: 2.5}
    POS_INTEGRATOR_GAIN = 6.0

    def on_btn_autotune_speed(self):
        if not self.active.is_connected():
            QMessageBox.warning(self, "自動估失敗", "請先連線")
            return
        m = self.current_motor
        if self.has_encoder.get(m) is False:
            QMessageBox.warning(self, "自動估失敗", f"M{m} 沒有編碼器，無法估速度 PID")
            return
        if self._autotune_active:
            return

        top = float(self.spin_autotune_duty.value())
        lam_factor = float(self.combo_autotune_aggr.currentData() or 1.0)
        levels = sorted({
            round(max(15.0, min(95.0, top * factor)))
            for factor in self.AUTOTUNE_LEVEL_FACTORS
        })
        dur = self.AUTOTUNE_LEVEL_DUR
        total = len(levels) * dur

        reply = QMessageBox.question(
            self,
            "自動估速度 PID",
            f"將對 M{m} 依序送開迴路 PWM {levels}%，每階約 {dur:.1f}s、"
            f"共約 {total:.1f}s，馬達會轉動。\n\n"
            "請確認輪子可自由轉動、周圍安全。是否開始？",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
        )
        if reply != QMessageBox.StandardButton.Yes:
            return

        self._autotune_active = True
        self._autotune_samples = []
        self._autotune_ctx = {
            "motor": m,
            "levels": levels,
            "dur": dur,
            "lam_factor": lam_factor,
            "start_time": time.time(),
        }
        self.btn_autotune_speed.setEnabled(False)
        self.lbl_autotune_status.setText(f"狀態：M{m} 多準位掃描中 {levels}% ...")

        self.chart.rb_rpm.setChecked(True)
        self.chart.begin_new_run("autotune")
        self.chart.start_recording()
        # 韌體端以 20ms（50Hz）高速擷取上升段，停掃後一次回傳；
        # 取代 10Hz telemetry（會把 τ 量成地板值、害 PID 過猛）。
        self.active.send_command(
            {"cmd": "chart_buffer_start", "motors": [m], "interval_ms": 20}
        )
        for i, level in enumerate(levels):
            QTimer.singleShot(
                int(i * dur * 1000),
                lambda level=level: self._autotune_send_level(level),
            )
        QTimer.singleShot(int(total * 1000), self._autotune_stop)

    def _autotune_send_level(self, level_pct: int):
        if not self._autotune_active or not self._autotune_ctx:
            return
        m = int(self._autotune_ctx["motor"])
        self.active.send_command({"cmd": "pwm", "motor": m, "duty": int(level_pct)})
        self._log(f"自動估速度 PID: M{m} PWM={level_pct}%")

    def _autotune_stop(self):
        if not self._autotune_active or not self._autotune_ctx:
            return
        m = int(self._autotune_ctx["motor"])
        self.active.send_command({"cmd": "stop", "motor": m})
        # 停止取樣並要求韌體把高速緩存一次回傳；計算改由 chart_buffer_done 觸發。
        self._autotune_samples = []
        self._autotune_awaiting_buffer = True
        self.active.send_command({"cmd": "chart_buffer_stop"})
        self.lbl_autotune_status.setText("狀態：等待高速緩存回傳並計算...")
        # 逾時保險：舊韌體不支援 chart_buffer 或回傳遺失時，仍用已收到的樣本嘗試計算。
        QTimer.singleShot(3000, self._autotune_buffer_timeout)

    def _autotune_buffer_timeout(self):
        if not self._autotune_awaiting_buffer:
            return
        self._autotune_awaiting_buffer = False
        if len(self._autotune_samples) >= 10:
            self._log("自動估速度 PID: 未收到 chart_buffer_done，用已收樣本計算")
            self._finish_autotune_speed()
        else:
            self._autotune_active = False
            self._autotune_ctx = None
            self.btn_autotune_speed.setEnabled(True)
            self.chart.stop_recording()
            self.lbl_autotune_status.setText(
                "狀態：高速緩存無回應（韌體需支援 chart_buffer，請重新燒錄）"
            )
            self._log("自動估速度 PID 失敗: 未收到高速緩存（chart_buffer 不支援？）")

    def _record_buffer_sample(self, data: dict):
        """韌體高速緩存回傳的單筆樣本（autotune 期間）。時基為韌體 t_ms。"""
        if not self._autotune_awaiting_buffer or not self._autotune_ctx:
            return
        target_motor = int(self._autotune_ctx["motor"])
        t_s = float(data.get("t_ms", 0)) / 1000.0
        for motor in data.get("motors", []):
            if motor.get("i") != target_motor:
                continue
            rpm = motor.get("rpm_meas")
            if isinstance(rpm, (int, float)):
                self._autotune_samples.append((t_s, float(rpm)))
            break

    def _finish_buffer_capture(self, data: dict):
        """收到 chart_buffer_done → 用高速緩存樣本算速度 PID。"""
        if not self._autotune_awaiting_buffer:
            return
        self._autotune_awaiting_buffer = False
        count = data.get("count", len(self._autotune_samples))
        if data.get("overflow"):
            self._log(f"自動估速度 PID: 高速緩存已達上限（{count} 筆），仍可計算")
        else:
            self._log(f"自動估速度 PID: 高速緩存回傳完成（{count} 筆）")
        self._finish_autotune_speed()

    def _record_autotune_sample(self, motors: list):
        # 改用韌體高速緩存後，等待回傳期間不再混入 10Hz telemetry（時基/頻率不同）。
        if self._autotune_awaiting_buffer:
            return
        if not self._autotune_active or not self._autotune_ctx:
            return
        start = float(self._autotune_ctx.get("start_time", time.time()))
        target_motor = int(self._autotune_ctx["motor"])
        for motor in motors:
            if motor.get("i") != target_motor:
                continue
            rpm = motor.get("rpm_meas")
            if isinstance(rpm, (int, float)):
                self._autotune_samples.append((time.time() - start, float(rpm)))
            break

    @staticmethod
    def _window_mean(t: list[float], y: list[float], t_start: float, t_end: float) -> float | None:
        vals = [y[i] for i in range(len(t)) if t_start <= t[i] < t_end]
        return (sum(vals) / len(vals)) if len(vals) >= 2 else None

    @staticmethod
    def _linfit(xs: list[float], ys: list[float]) -> tuple[float, float] | None:
        n = len(xs)
        if n < 2:
            return None
        sx = sum(xs)
        sy = sum(ys)
        sxx = sum(x * x for x in xs)
        sxy = sum(x * y for x, y in zip(xs, ys))
        denom = n * sxx - sx * sx
        if abs(denom) < 1e-12:
            return None
        b = (n * sxy - sx * sy) / denom
        a = (sy - b * sx) / n
        return a, b

    def _finish_autotune_speed(self):
        ctx = self._autotune_ctx or {}
        self._autotune_active = False
        self._autotune_awaiting_buffer = False
        self._autotune_ctx = None
        self.btn_autotune_speed.setEnabled(True)
        self.chart.stop_recording()

        motor = int(ctx.get("motor", self.current_motor))
        levels = list(ctx.get("levels", []))
        dur = float(ctx.get("dur", self.AUTOTUNE_LEVEL_DUR))
        lam_factor = float(ctx.get("lam_factor", 1.0))
        samples = self._autotune_samples
        if len(samples) < 10 or not levels:
            self.lbl_autotune_status.setText("狀態：資料不足，重試（確認連線/編碼器/telemetry）")
            self._log("自動估速度 PID 失敗: telemetry 資料不足")
            return

        t = [p[0] for p in samples]
        y = [p[1] for p in samples]
        moving: list[tuple[int, float, float]] = []
        for i, level in enumerate(levels):
            seg_end = (i + 1) * dur
            rpm = self._window_mean(t, y, seg_end - 0.30, seg_end - 0.02)
            if rpm is not None and abs(rpm) > 5.0:
                moving.append((i, level / 100.0, abs(rpm)))

        if len(moving) < 2:
            self.lbl_autotune_status.setText("狀態：轉動的準位太少，請把高準位 PWM 調高再試")
            self._log("自動估速度 PID 失敗: 可用轉動準位少於 2 點")
            return

        rpms = [rpm for _i, _duty, rpm in moving]
        dutys = [duty for _i, duty, _rpm in moving]
        fit = self._linfit(rpms, dutys)
        if fit is None or fit[1] <= 1e-6:
            self.lbl_autotune_status.setText("狀態：擬合失敗（斜率異常），重試")
            self._log("自動估速度 PID 失敗: duty/rpm 線性擬合斜率異常")
            return

        kS, kV = fit
        # 不把 kS 夾到 >=0：最小平方線在取樣轉速區間（非從 0 起）常有小負截距，
        # 夾成 0 會讓前饋在目標轉速處系統性偏高（馬達穩態衝過頭）。只擋極端值。
        kS = max(-0.5, min(0.95, kS))
        K = 1.0 / kV
        # τ 要量「從 0 起步」的乾淨一階上升 → 用第一個會轉的準位段。
        # 掃描是逐級往上、級間不停轉，最高準位段一進來就已超過 63.2% 門檻，
        # 量到的會是 0.05 地板值（害 Ki=1/(K·τ·λ) 過大 → overshoot）。
        first_idx, _first_duty, first_rpm = moving[0]
        tau = self._estimate_tau_segment(
            t,
            [abs(rpm) for rpm in y],
            first_idx * dur,
            (first_idx + 1) * dur,
            first_rpm,
        )
        self._speed_id[motor] = {"tau": tau, "K": K, "kS": kS, "kV": kV}
        self.speed_ff_enabled.setChecked(True)
        self.speed_ff_ks.setValue(kS)
        self.speed_ff_kv.setValue(kV)
        self._flash_fields(self.speed_ff_ks, self.speed_ff_kv)
        # 前饋撐穩態，立即套到韌體 RAM（只送前饋三欄，applyConfigFromDoc 是部分套用、
        # 不會動到 PID/硬體/Hold）；PID 仍只填欄位、留給使用者按「套用RAM」驗證。
        self.active.send_command({
            "cmd": "apply_config",
            "speedFFEnabled": True,
            "speedFFkS": round(kS, 5),
            "speedFFkV": round(kV, 6),
            "speedFFkA": 0.0,
        })

        lam = max(tau * lam_factor, 0.02)
        # 單位換算：K=1/kV 是「每單位 duty 分數(0~1)」的增益，但韌體速度環的輸出與
        # 預設 PID（Kp≈2.0）都是「duty 百分比(0~100)」。直接用 K 會讓 Kp/Ki 小 100 倍、
        # PID 形同失效（純 PID 收斂要 >10s）。除以 100 換成百分比單位才對齊韌體。
        K_pct = K / 100.0
        kp = self._clamp_to_spin(self.kp, tau / (K_pct * lam))
        ki = self._clamp_to_spin(self.ki, 1.0 / (K_pct * lam))
        kd = 0.0
        self._autotune_speed_pid = (kp, ki, kd)
        self.btn_fill_speed_pid.setEnabled(True)
        self._fill_autotune_speed_pid()

        pts = ", ".join(f"{duty * 100:.0f}%→{rpm:.0f}" for _i, duty, rpm in moving)
        self.lbl_autotune_status.setText(
            f"狀態：完成 kS={kS:.3f}, kV={kV:.5f}, K={K:.0f}, "
            f"τ={tau * 1000:.0f}ms → FF已套用RAM, PID已填 Kp={kp:.4f}, Ki={ki:.4f}（請按套用RAM驗證）"
        )
        self._log(
            f"自動估速度 PID 完成: M{motor}, 掃描 [{pts}], "
            f"duty={kS:.3f}+{kV:.5f}*rpm, K={K:.0f}, tau={tau * 1000:.0f}ms, "
            f"lambda={lam * 1000:.0f}ms, Kp={kp:.4f}, Ki={ki:.4f}, Kd=0; "
            f"前饋已套用 RAM，PID 待按「套用RAM」"
        )

    def _estimate_tau_segment(
        self,
        t: list[float],
        y: list[float],
        seg_start: float,
        seg_end: float,
        rpm_ss: float,
    ) -> float:
        if abs(rpm_ss) < 1.0:
            return 0.1
        sign = 1.0 if rpm_ss >= 0 else -1.0
        target = 0.632 * rpm_ss
        t_ref = None
        for i in range(len(t)):
            if t[i] < seg_start:
                continue
            if t[i] >= seg_end:
                break
            if t_ref is None:
                t_ref = t[i]
            if (sign > 0 and y[i] >= target) or (sign < 0 and y[i] <= target):
                tau = t[i] - t_ref
                return tau if tau > 1e-3 else 0.05
        return 0.1

    @staticmethod
    def _clamp_to_spin(spin: QDoubleSpinBox | QSpinBox, value: float) -> float:
        return max(spin.minimum(), min(spin.maximum(), value))

    def _fill_autotune_speed_pid(self):
        if self._autotune_speed_pid is None:
            return
        kp, ki, kd = self._autotune_speed_pid
        self.kp.setValue(kp)
        self.ki.setValue(ki)
        self.kd.setValue(kd)
        self.btn_fill_speed_pid.setText(f"填入速度 PID 欄位 ({kp:.4f}, {ki:.4f}, {kd:.4f})")
        self.pid_hint.setText(
            f"已填入速度 PID/前饋欄位: Kp={kp:.4f}, Ki={ki:.4f}, Kd={kd:.4f}（請按套用RAM）"
        )
        self._flash_fields(self.kp, self.ki, self.kd)
        self._log(
            f"已填入速度 PID 欄位: Kp={kp:.4f}, Ki={ki:.4f}, Kd={kd:.4f}, "
            f"FF={'on' if self.speed_ff_enabled.isChecked() else 'off'}, "
            f"kS={self.speed_ff_ks.value():.5f}, kV={self.speed_ff_kv.value():.6f}"
        )

    def _fill_autotune_pos_pid(self):
        if self._autotune_pos_pid is None:
            return
        kp, ki, kd = self._autotune_pos_pid
        self.pos_kp.setValue(kp)
        self.pos_ki.setValue(ki)
        self.pos_kd.setValue(kd)
        self.btn_fill_pos_pid.setText(f"填入角度 PID 欄位 ({kp:.4f}, {ki:.4f}, {kd:.4f})")
        self.pid_hint.setText(
            f"已填入角度 PID 欄位: posKp={kp:.4f}, posKi={ki:.4f}, posKd={kd:.4f}（尚未套用 RAM/NVS）"
        )
        self._flash_fields(self.pos_kp, self.pos_ki, self.pos_kd)
        self._log(f"已填入角度 PID 欄位: posKp={kp:.4f}, posKi={ki:.4f}, posKd={kd:.4f}")

    def _flash_fields(self, *fields: QDoubleSpinBox):
        for field in fields:
            field.setStyleSheet("QDoubleSpinBox { background: #fff3b0; }")
        QTimer.singleShot(900, lambda: [field.setStyleSheet("") for field in fields])

    def on_btn_autotune_pos(self):
        m = self.current_motor
        sid = self._speed_id.get(m)
        if not sid:
            self.lbl_autotune_pos_status.setText("狀態：尚無內環資料，請先對同一顆馬達做速度自動估")
            QMessageBox.information(
                self,
                "需要先做速度自動估",
                f"請先對 M{m} 執行速度自動估，再估角度 PID。",
            )
            return

        tau_s = max(float(sid.get("tau", 0.1)), 0.02)
        lam_factor = float(self.combo_autotune_aggr.currentData() or 1.0)
        n_factor = self.POS_N_BY_LAMBDA.get(lam_factor, 3.5)
        tau_pos = n_factor * tau_s
        g = self.POS_INTEGRATOR_GAIN
        kp = 1.0 / (g * tau_pos)
        kd = kp * tau_s
        ki = kp / (4.0 * tau_pos)

        kp = self._clamp_to_spin(self.pos_kp, kp)
        ki = self._clamp_to_spin(self.pos_ki, ki)
        kd = self._clamp_to_spin(self.pos_kd, kd)
        self._autotune_pos_pid = (kp, ki, kd)
        self.btn_fill_pos_pid.setEnabled(True)
        self._fill_autotune_pos_pid()
        self.lbl_autotune_pos_status.setText(
            f"狀態：完成 內環τ={tau_s * 1000:.0f}ms × N{n_factor:.1f} "
            f"→ Pos Kp={kp:.3f}, Ki={ki:.3f}, Kd={kd:.3f}"
        )
        self._log(
            f"自動估角度 PID 完成: M{m}, tau={tau_s * 1000:.0f}ms, N={n_factor:.1f}, "
            f"tau_pos={tau_pos * 1000:.0f}ms, Kp={kp:.3f}, Ki={ki:.3f}, Kd={kd:.3f}"
        )

    def _config_fields(self) -> dict:
        """蒐集兩個分頁的所有參數（PID + 硬體 + Hold）。
        硬體/Hold 與 PID 共用 motor NVS 命名空間，整批送出確保一致。"""
        lego = self.rb_lego.isChecked()
        return {
            # 速度環
            "kp": self.kp.value(),
            "ki": self.ki.value(),
            "kd": self.kd.value(),
            "minSpeed": self.min_spd.value(),
            "maxSpeed": self.max_spd.value(),
            "speedFFEnabled": self.speed_ff_enabled.isChecked(),
            "speedFFkS": self.speed_ff_ks.value(),
            "speedFFkV": self.speed_ff_kv.value(),
            "speedFFkA": 0.0,
            # 位置環
            "posKp": self.pos_kp.value(),
            "posKi": self.pos_ki.value(),
            "posKd": self.pos_kd.value(),
            "posMaxDuty": self.pos_max_duty.value(),
            "posToleranceDeg": self.pos_tol.value(),
            # 硬體（馬達種類 → encoderPos / posCtrlMode）
            "encoderPPR": self.hw_ppr.value(),
            "gearRatio": self.hw_ratio.value(),
            "encoderPos": 1 if lego else 0,
            "posCtrlMode": 0 if lego else 1,
            # Hold
            "holdSettleDeg": self.hold_settle.value(),
            "holdKi": self.hold_ki.value(),
            "holdMaxDuty": self.hold_max.value(),
        }

    def _read_config(self):
        self.active.send_command({"cmd": "read_config"})

    def _apply_config(self):
        self.active.send_command({"cmd": "apply_config", **self._config_fields()})
        self.pid_hint.setText("已套用至 RAM（未寫入 NVS）")

    def _write_config(self):
        self.active.send_command({"cmd": "write_config", **self._config_fields()})
        self.pid_hint.setText("已送出寫入 NVS…（等待韌體回應）")

    def _reset_config(self):
        self.active.send_command({"cmd": "reset_config"})

    # ------------------------------------------------------------------
    # 參數檔匯出/匯入（換板移植：只含 motor 參數，不碰 WiFi/板名）
    # ------------------------------------------------------------------

    def _export_config_file(self):
        default_name = f"motor_params_{datetime.now():%Y%m%d_%H%M%S}.json"
        start_dir = str(self.settings.value("params_dir", str(PROJECT_ROOT)))
        path, _ = QFileDialog.getSaveFileName(
            self, "匯出馬達參數", str(Path(start_dir) / default_name),
            "JSON 參數檔 (*.json);;All files (*.*)",
        )
        if not path:
            return
        payload = {
            "kind": "phone_blocky_motor_params",
            "version": 1,
            "savedAt": datetime.now().isoformat(timespec="seconds"),
            "motorType": "lego" if self.rb_lego.isChecked() else "hall",
            "config": self._config_fields(),
        }
        try:
            Path(path).write_text(
                json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8"
            )
        except OSError as e:
            QMessageBox.warning(self, "匯出失敗", f"無法寫入檔案：\n{e}")
            self._log(f"匯出參數失敗: {e}")
            return
        self.settings.setValue("params_dir", str(Path(path).parent))
        self._log(f"已匯出馬達參數 → {path}")
        self.set_hint.setText(f"✓ 已匯出參數到 {Path(path).name}")

    def _load_config_file(self) -> dict | None:
        """開檔對話框讀回參數；回傳 config dict 或 None（取消/失敗）。"""
        start_dir = str(self.settings.value("params_dir", str(PROJECT_ROOT)))
        path, _ = QFileDialog.getOpenFileName(
            self, "匯入馬達參數", start_dir,
            "JSON 參數檔 (*.json);;All files (*.*)",
        )
        if not path:
            return None
        try:
            payload = json.loads(Path(path).read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as e:
            QMessageBox.warning(self, "匯入失敗", f"無法讀取檔案：\n{e}")
            self._log(f"匯入參數失敗: {e}")
            return None
        # 接受 {..., "config":{...}}（本工具格式）或直接是參數物件（向後相容）
        config = payload.get("config") if isinstance(payload, dict) else None
        if config is None and isinstance(payload, dict):
            config = payload
        if not isinstance(config, dict) or not config:
            QMessageBox.warning(self, "匯入失敗", "檔案格式不正確，找不到參數內容。")
            self._log("匯入參數失敗: 檔案無有效 config")
            return None
        self.settings.setValue("params_dir", str(Path(path).parent))
        self._loaded_params_name = Path(path).name
        return config

    def _import_config_file(self):
        config = self._load_config_file()
        if config is None:
            return
        self._fill_config(config)
        name = getattr(self, "_loaded_params_name", "")
        self._log(f"已匯入參數（僅填入欄位，未寫入板子）: {name}")
        self.set_hint.setText("✓ 已匯入到欄位；按「套用RAM」或「寫入NVS」才會生效")

    def _import_and_write_config_file(self):
        if not self.active.is_connected():
            QMessageBox.information(
                self, "尚未連線", "請先連線到要寫入的控制板，再「匯入並寫入NVS」。"
            )
            return
        config = self._load_config_file()
        if config is None:
            return
        # 先回填欄位，再走既有寫入路徑（write_config 韌體端會 applyConfigFromDoc + 存 NVS）
        self._fill_config(config)
        self._write_config()
        name = getattr(self, "_loaded_params_name", "")
        self._log(f"已匯入並送出寫入 NVS: {name}")

    def _fill_config(self, c: dict):
        """以韌體回傳的 config 物件回填兩個分頁的介面（不觸發訊號）。"""
        pairs = (
            (self.kp, "kp"), (self.ki, "ki"), (self.kd, "kd"),
            (self.min_spd, "minSpeed"), (self.max_spd, "maxSpeed"),
            (self.speed_ff_ks, "speedFFkS"), (self.speed_ff_kv, "speedFFkV"),
            (self.pos_kp, "posKp"), (self.pos_ki, "posKi"), (self.pos_kd, "posKd"),
            (self.pos_max_duty, "posMaxDuty"), (self.pos_tol, "posToleranceDeg"),
            (self.hw_ppr, "encoderPPR"), (self.hw_ratio, "gearRatio"),
            (self.hold_settle, "holdSettleDeg"), (self.hold_ki, "holdKi"),
            (self.hold_max, "holdMaxDuty"),
        )
        for spin, key in pairs:
            if key in c and c[key] is not None:
                spin.blockSignals(True)
                spin.setValue(c[key])
                spin.blockSignals(False)
        if "speedFFEnabled" in c:
            self.speed_ff_enabled.blockSignals(True)
            self.speed_ff_enabled.setChecked(bool(c.get("speedFFEnabled")))
            self.speed_ff_enabled.blockSignals(False)
        # 由 posCtrlMode / encoderPos 推回馬達種類（對齊 set.html）。
        # 用 blockSignals 避免觸發 _on_motor_type 而蓋掉上面剛回填的 PPR/減速比，
        # 只更新顯示（種類徽章、減速比顯示與否）。
        target = (self.rb_hall
                  if (c.get("posCtrlMode") == 1 or c.get("encoderPos") == 0)
                  else self.rb_lego)
        self.rb_lego.blockSignals(True)
        self.rb_hall.blockSignals(True)
        target.setChecked(True)
        self.rb_lego.blockSignals(False)
        self.rb_hall.blockSignals(False)
        self._update_motor_type_ui()
        self.pid_hint.setText("已讀取韌體目前值")

    # ------------------------------------------------------------------
    # 接收
    # ------------------------------------------------------------------

    def _on_data(self, data: dict):
        cmd = data.get("cmd")
        if cmd == "chart_buffer_sample":
            self._record_buffer_sample(data)
            return
        if cmd == "chart_buffer_done":
            self._finish_buffer_capture(data)
            return

        t = data.get("type")
        if t == "telemetry":
            motors = data.get("motors", [])
            self.chart.update_data(motors)
            self._record_autotune_sample(motors)
            self._update_live(motors)
        elif t == "capabilities":
            for m in data.get("motors", []):
                self.has_encoder[m.get("id")] = bool(m.get("has_encoder"))
            self._refresh_caps()
            self._log(f"← capabilities: {self.has_encoder}")
        elif t == "config":
            self._fill_config(data)
            self._log("← config（參數已回填）")
        elif t == "buttonData":
            self._sensor_cfg["button"]["value"].setText(str(data.get("value")))
        elif t == "digitalValue":
            self._sensor_cfg["digital"]["value"].setText(str(data.get("value")))
        elif t == "analogValue":
            v = data.get("value", 0)
            pct = round(v * 100 / 4095) if isinstance(v, (int, float)) else 0
            self._sensor_cfg["analog"]["value"].setText(f"{v}  ({pct}%)")
        elif t == "ultrasonicDistance":
            d = data.get("distance")
            if isinstance(d, (int, float)):
                self._sensor_cfg["ultrasonic"]["value"].setText(
                    "無回波" if d < 0 else f"{d:.1f} cm")
        elif t in ("motor_read", "pong"):
            self._log(f"← {data}")
        elif data.get("err"):
            self.pid_hint.setText(f"⚠️ 韌體回報錯誤: {data['err']}")
            self._log(f"← 錯誤: {data.get('err')}")
        elif data.get("ok") and self.pid_hint.text().startswith("已送出寫入"):
            self.pid_hint.setText("✓ 已寫入 NVS")

    def _update_live(self, motors: list):
        parts = []
        for m in motors:
            i = m.get("i")
            rpm = m.get("rpm_meas")
            deg = m.get("deg")
            duty = m.get("duty")
            rpm_s = f"{rpm:.0f}" if isinstance(rpm, (int, float)) else "—"
            deg_s = f"{deg / 100:.1f}" if isinstance(deg, (int, float)) else "—"
            parts.append(f"M{i}[{m.get('mode')}] duty={duty} rpm={rpm_s} deg={deg_s}")
        self.live.setText("  |  ".join(parts))

    def _log(self, msg: str):
        self.log.appendPlainText(msg)

    def closeEvent(self, event):
        if self.flash_process and self.flash_process.state() != QProcess.ProcessState.NotRunning:
            self.flash_process.kill()
            self.flash_process.waitForFinished(1000)
        if self.active.is_connected():
            self.active.send_command({"cmd": "subscribe", "on": False})
            if self.active is self.ws:
                self.ws.disconnect_ws()
            else:
                self.serial.disconnect_serial()
        super().closeEvent(event)


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--esptool":
        sys.exit(run_bundled_esptool())
    app = QApplication(sys.argv)
    win = MotorTuner()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
