"""WebSocket 傳輸層 — 對齊 phone_blocky 韌體的 ws://<ip>/ws 介面.

phone_blocky 的 cmd: JSON 協定只走 WebSocket（序列埠僅吃舊式 M../S../READ,..）。
韌體端 processCommands() 以 StaticJsonDocument<4096> 解析「整個訊息為單一 JSON
物件」，因此一個 WebSocket frame = 一個 JSON，不需換行、不需批次。

結構仿 motor_control_v4 的 SerialWorker/NetworkWorker，但改用 Qt 內建的
QWebSocket（隨 PyQt6 附帶），全程非阻塞、由 Qt event loop 驅動，免額外執行緒。
"""
from __future__ import annotations

import json

from PyQt6.QtCore import QObject, QUrl, pyqtSignal
from PyQt6.QtNetwork import QAbstractSocket
from PyQt6.QtWebSockets import QWebSocket


class WsWorker(QObject):
    """phone_blocky WebSocket 客戶端。"""

    connected = pyqtSignal(str)        # 連線成功 (url)
    disconnected = pyqtSignal()        # 斷線
    error_occurred = pyqtSignal(str)   # 發生錯誤 (error_msg)
    data_received = pyqtSignal(dict)   # 收到並解析成功的 JSON 物件
    raw_log = pyqtSignal(str)          # 原始日誌

    def __init__(self):
        super().__init__()
        self.ws = QWebSocket()
        self.ws.connected.connect(self._on_connected)
        self.ws.disconnected.connect(self._on_disconnected)
        self.ws.textMessageReceived.connect(self._on_text)
        self.ws.errorOccurred.connect(self._on_error)
        self._url = ""

    # --- 連線控制 ---

    def connect_ws(self, ip: str, port: int = 80, path: str = "/ws"):
        self._url = f"ws://{ip}:{port}{path}"
        self.raw_log.emit(f"連線中… {self._url}")
        self.ws.open(QUrl(self._url))

    def disconnect_ws(self):
        self.ws.close()

    def is_connected(self) -> bool:
        return self.ws.state() == QAbstractSocket.SocketState.ConnectedState

    # --- 送出指令 ---

    def send_command(self, obj: dict):
        """送出單一 cmd JSON 物件（一個 frame 一個物件）。"""
        if not self.is_connected():
            self.raw_log.emit("✗ 未連線，指令未送出")
            return
        text = json.dumps(obj, ensure_ascii=False, separators=(",", ":"))
        self.ws.sendTextMessage(text)
        self.raw_log.emit(f"→ {text}")

    # --- QWebSocket 訊號處理 ---

    def _on_connected(self):
        self.raw_log.emit(f"✓ 已連線 {self._url}")
        self.connected.emit(self._url)

    def _on_disconnected(self):
        self.raw_log.emit("✗ 已斷線")
        self.disconnected.emit()

    def _on_error(self, _code):
        msg = self.ws.errorString()
        self.error_occurred.emit(msg)
        self.raw_log.emit(f"⚠️ {msg}")

    def _on_text(self, msg: str):
        try:
            data = json.loads(msg)
        except json.JSONDecodeError:
            self.raw_log.emit(f"[raw] {msg}")
            return
        if isinstance(data, dict):
            self.data_received.emit(data)
        else:
            self.raw_log.emit(f"[raw] {msg}")
