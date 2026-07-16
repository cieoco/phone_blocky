"""USB 序列傳輸層 — 對齊韌體解耦後的 Serial JSON 通道.

韌體 serialTask 現在會把以 '{' 開頭的整行交給同一個 JSON router
（processCommands(line, Comm::CH_SERIAL)），回應/telemetry 經 Serial.println
逐行送回。故本 worker：送出 = json + '\n'；接收 = 依 '\n' 切行、只把能解析成
JSON 物件的行往上送（其餘為韌體 debug log，忽略）。

訊號介面刻意與 ws_worker.WsWorker 完全一致，讓主視窗能統一對待兩種傳輸。
"""
from __future__ import annotations

import json

import serial
import serial.tools.list_ports
from PyQt6.QtCore import QObject, QTimer, pyqtSignal


class SerialWorker(QObject):
    """phone_blocky USB 序列客戶端（JSON over Serial）。"""

    connected = pyqtSignal(str)
    disconnected = pyqtSignal()
    error_occurred = pyqtSignal(str)
    data_received = pyqtSignal(dict)
    raw_log = pyqtSignal(str)

    def __init__(self):
        super().__init__()
        self.ser: serial.Serial | None = None
        self._rx = b""
        self.timer = QTimer(self)
        self.timer.setInterval(5)  # 5ms 非阻塞輪詢
        self.timer.timeout.connect(self._poll)

    @staticmethod
    def list_ports() -> list[str]:
        return [p.device for p in SerialWorker.list_port_infos()]

    @staticmethod
    def list_port_infos():
        return list(serial.tools.list_ports.comports())

    @staticmethod
    def auto_detect_port() -> str:
        ports = SerialWorker.list_port_infos()
        if not ports:
            return ""
        if len(ports) == 1:
            return ports[0].device

        keywords = (
            "cp210",
            "ch340",
            "ch341",
            "usb serial",
            "usb-serial",
            "silicon labs",
            "wch",
            "espressif",
            "uart",
        )
        best = ""
        best_score = 0
        for p in ports:
            text = " ".join(
                str(v or "")
                for v in (
                    p.device,
                    p.description,
                    p.hwid,
                    getattr(p, "manufacturer", ""),
                    getattr(p, "product", ""),
                )
            ).lower()
            score = sum(1 for k in keywords if k in text)
            if score > best_score:
                best = p.device
                best_score = score
        return best

    # --- 連線控制 ---

    def connect_serial(self, port: str, baudrate: int = 115200):
        try:
            if self.ser and self.ser.is_open:
                self.ser.close()
            self.ser = serial.Serial(port, baudrate, timeout=0)  # 非阻塞
            self._rx = b""
            self.timer.start()
            self.raw_log.emit(f"✓ 已連線 {port} @ {baudrate}")
            self.connected.emit(port)
        except Exception as e:  # noqa: BLE001
            self.ser = None
            self.timer.stop()
            self.error_occurred.emit(str(e))
            self.raw_log.emit(f"✗ 連線失敗: {e}")

    def disconnect_serial(self):
        self.timer.stop()
        if self.ser and self.ser.is_open:
            try:
                self.ser.close()
            except Exception:  # noqa: BLE001
                pass
        self.ser = None
        self.raw_log.emit("✗ 已斷線")
        self.disconnected.emit()

    def is_connected(self) -> bool:
        return bool(self.ser and self.ser.is_open)

    # --- 送出指令 ---

    def send_command(self, obj: dict):
        if not self.is_connected():
            self.raw_log.emit("✗ 未連線，指令未送出")
            return
        text = json.dumps(obj, ensure_ascii=False, separators=(",", ":"))
        try:
            self.ser.write((text + "\n").encode("utf-8"))
            self.raw_log.emit(f"→ {text}")
        except Exception as e:  # noqa: BLE001
            self.error_occurred.emit(f"寫入失敗: {e}")
            self.disconnect_serial()

    # --- 輪詢接收 ---

    def _poll(self):
        if not self.is_connected():
            self.timer.stop()
            return
        try:
            waiting = self.ser.in_waiting
            if waiting:
                self._rx += self.ser.read(waiting)
                while b"\n" in self._rx:
                    line, self._rx = self._rx.split(b"\n", 1)
                    s = line.decode("utf-8", errors="ignore").strip()
                    if s:
                        self._process_line(s)
        except (serial.SerialException, OSError) as e:
            self.raw_log.emit(f"⚠️ 連線遺失: {e}")
            self.disconnect_serial()

    def _process_line(self, line: str):
        # 韌體在 WiFi 模式仍會印大量非 JSON debug log，只取能解析成物件的行
        if not line.startswith("{"):
            return
        try:
            data = json.loads(line)
        except json.JSONDecodeError:
            return
        if isinstance(data, dict):
            self.data_received.emit(data)
