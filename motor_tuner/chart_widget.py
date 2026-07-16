"""RPM / 位置輸出曲線元件 — 自 motor_control_v4/chart_widget.py 精簡移植.

沿用原機制：只在「記錄期」累積數據，按下停止後延遲 2 秒繼續捕捉減速過程，
再一次性繪製「實際(實線) vs 目標(虛線)」，避免即時重繪卡頓。

差異：phone_blocky telemetry 欄位是 motors[].i / rpm_meas / rpm_cmd / deg(centi)，
與 motorControl 的 id / rpm_target / deg 不同；且 telemetry 不含目標位置，故目標值
（target rpm / target deg）由主視窗在送出指令時透過 set_target_* 注入。
"""
from __future__ import annotations

import time
from collections import deque

import matplotlib
import matplotlib.font_manager as _fm
from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg
from matplotlib.figure import Figure
from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import (
    QButtonGroup,
    QCheckBox,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QRadioButton,
    QVBoxLayout,
    QWidget,
)

# 中文字型（Windows 優先 Microsoft YaHei）
_cjk = ["Microsoft YaHei", "SimHei", "SimSun", "Arial Unicode MS"]
_avail = {f.name for f in _fm.fontManager.ttflist}
for _f in _cjk:
    if _f in _avail:
        matplotlib.rcParams["font.sans-serif"] = [_f] + matplotlib.rcParams.get(
            "font.sans-serif", []
        )
        break
matplotlib.rcParams["axes.unicode_minus"] = False


class MotorChartWidget(QWidget):
    def __init__(self):
        super().__init__()
        layout = QVBoxLayout(self)

        # --- 工具列 ---
        bar = QHBoxLayout()
        self.chk_show = QCheckBox("顯示圖表")
        self.chk_show.setChecked(True)
        bar.addWidget(self.chk_show)

        self.btn_clear = QPushButton("清除數據")
        self.btn_clear.clicked.connect(self.clear_data)
        bar.addWidget(self.btn_clear)

        self.chk_overlay = QCheckBox("疊圖比較")
        self.chk_overlay.setToolTip("同類型測試會保留前一次曲線作比較；切換不同測試類型會自動清除。")
        self.chk_overlay.setChecked(True)
        bar.addWidget(self.chk_overlay)

        bar.addStretch()
        bar.addWidget(QLabel("模式:"))
        self.mode_group = QButtonGroup(self)
        self.rb_rpm = QRadioButton("轉速 (RPM)")
        self.rb_rpm.setChecked(True)
        self.rb_pos = QRadioButton("位置 (角度)")
        self.mode_group.addButton(self.rb_rpm)
        self.mode_group.addButton(self.rb_pos)
        bar.addWidget(self.rb_rpm)
        bar.addWidget(self.rb_pos)
        layout.addLayout(bar)

        # --- 畫布 ---
        self.figure = Figure(figsize=(5, 3.2), dpi=100)
        self.canvas = FigureCanvasQTAgg(self.figure)
        self.ax = self.figure.add_subplot(111)
        self.ax.grid(True)
        layout.addWidget(self.canvas)

        self.metrics_label = QLabel("步階指標：速度/角度測試完成後顯示")
        self.metrics_label.setWordWrap(True)
        self.metrics_label.setStyleSheet(
            "font-family: Consolas; font-size: 11px; color: #333; "
            "padding: 4px; background-color: #f5f5f5;"
        )
        layout.addWidget(self.metrics_label)

        # 數據: motor_id -> deque(time/rpm/pos/target_rpm/target_pos)
        self.data_history: dict[int, dict[str, deque]] = {}
        self.prev_history: dict[int, dict[str, list]] = {}
        self.prev_run_kind: str | None = None
        self.current_run_kind: str | None = None
        self.target_rpm: dict[int, float] = {}
        self.target_deg: dict[int, float] = {}
        self.start_time = None
        self.is_recording = False

        self.stop_timer = QTimer(self)
        self.stop_timer.setSingleShot(True)
        self.stop_timer.timeout.connect(self._finalize_recording)

        self.rb_rpm.toggled.connect(self._redraw)
        self.rb_pos.toggled.connect(self._redraw)
        self.chk_show.toggled.connect(self._redraw)

    # --- 目標值注入（主視窗送指令時呼叫）---

    def set_target_rpm(self, motor: int, rpm: float):
        self.target_rpm[motor] = rpm

    def set_target_deg(self, motor: int, deg: float):
        self.target_deg[motor] = deg

    # --- 記錄生命週期 ---

    def start_recording(self):
        self.is_recording = True
        self.stop_timer.stop()
        if self.start_time is None:
            self.start_time = time.time()
            self.data_history.clear()

    def stop_recording(self):
        # 延遲 2 秒後才真正停止並繪製，完整捕捉減速過程
        if self.is_recording:
            self.stop_timer.start(2000)

    def _finalize_recording(self):
        self.is_recording = False
        self._draw_final()

    def clear_data(self):
        self.data_history.clear()
        self.start_time = None
        self.is_recording = False
        self.stop_timer.stop()
        self.figure.clear()
        self.prev_history.clear()
        self.prev_run_kind = None
        self.current_run_kind = None
        self.metrics_label.setText("步階指標：速度/角度測試完成後顯示")
        self.ax = self.figure.add_subplot(111)
        self.ax.grid(True)
        self.canvas.draw()

    def begin_new_run(self, run_kind: str):
        same_kind = self.current_run_kind == run_kind
        if self.chk_overlay.isChecked() and same_kind and self.data_history:
            self.prev_history = {
                mid: {key: list(values) for key, values in hist.items()}
                for mid, hist in self.data_history.items()
            }
            self.prev_run_kind = run_kind
        else:
            self.prev_history.clear()
            self.prev_run_kind = None
        self.current_run_kind = run_kind
        self.data_history.clear()
        self.start_time = None
        self.is_recording = False
        self.stop_timer.stop()
        self.metrics_label.setText("步階指標：記錄中...")

    # --- 數據累積 ---

    def update_data(self, motors_data: list):
        if not self.chk_show.isChecked() or not self.is_recording:
            return
        if self.start_time is None:
            self.start_time = time.time()
        t = time.time() - self.start_time
        for m in motors_data:
            mid = m.get("i")
            if mid is None:
                continue
            # 無編碼器馬達 rpm_meas/deg 為 null，跳過
            if m.get("rpm_meas") is None and m.get("deg") is None:
                continue
            hist = self.data_history.setdefault(
                mid,
                {
                    "time": deque(maxlen=1000),
                    "rpm": deque(maxlen=1000),
                    "pos": deque(maxlen=1000),
                    "target_rpm": deque(maxlen=1000),
                    "target_pos": deque(maxlen=1000),
                },
            )
            hist["time"].append(t)
            hist["rpm"].append(_num(m.get("rpm_meas")))
            hist["pos"].append(_num(m.get("deg")) / 100.0)  # centi-deg → deg
            hist["target_rpm"].append(self.target_rpm.get(mid, 0.0))
            hist["target_pos"].append(self.target_deg.get(mid, 0.0))

    # --- 繪製 ---

    def _redraw(self):
        if self.chk_show.isChecked() and self.data_history:
            self._draw_final()

    def _draw_final(self):
        if not self.chk_show.isChecked() or not self.data_history:
            return
        self.ax.clear()
        self.ax.grid(True)
        rpm_mode = self.rb_rpm.isChecked()
        if rpm_mode:
            self.ax.set_title("馬達轉速：實際 vs 目標")
            self.ax.set_ylabel("RPM")
        else:
            self.ax.set_title("馬達位置：實際 vs 目標")
            self.ax.set_ylabel("角度 (度)")
        self.ax.set_xlabel("時間 (秒)")

        last_x = []
        mode = "rpm" if rpm_mode else "pos"
        for mid, hist in self.data_history.items():
            if len(hist["time"]) < 2:
                continue
            x = list(hist["time"])
            last_x = x
            key, tkey = ("rpm", "target_rpm") if rpm_mode else ("pos", "target_pos")
            self.ax.plot(x, list(hist[key]), label=f"M{mid} 實際", linewidth=2)
            self.ax.plot(
                x,
                list(hist[tkey]),
                label=f"M{mid} 目標",
                linestyle="--",
                linewidth=1.5,
                alpha=0.7,
            )
        if self.chk_overlay.isChecked() and self.prev_history and self.prev_run_kind == self.current_run_kind:
            for mid, hist in self.prev_history.items():
                if len(hist.get("time", [])) < 2:
                    continue
                x_prev = list(hist["time"])
                y_prev = list(hist["rpm"] if rpm_mode else hist["pos"])
                self.ax.plot(
                    x_prev,
                    y_prev,
                    label=f"M{mid} 前次",
                    linestyle=":",
                    linewidth=1.5,
                    alpha=0.45,
                    color="gray",
                )
        if last_x:
            self.ax.legend(loc="upper right", fontsize=9)
            self._annotate_metrics(mode)
        self.figure.tight_layout()
        self.canvas.draw()

    def compute_step_metrics(self, mode: str) -> dict[int, dict | None]:
        results = {}
        for mid, hist in self.data_history.items():
            t = list(hist["time"])
            if mode == "rpm":
                y = list(hist["rpm"])
                target = list(hist["target_rpm"])
            else:
                y = list(hist["pos"])
                target = list(hist["target_pos"])
            n = len(t)
            if n < 5 or len(y) != n or len(target) != n:
                results[mid] = None
                continue
            target_final = target[-1]
            y0 = y[0]
            step = target_final - y0
            if abs(step) < 1e-6:
                results[mid] = None
                continue
            tail_count = max(1, n // 5)
            tail = y[-tail_count:]
            steady = sum(tail) / len(tail)
            ss_err = target_final - steady
            ripple = (max(tail) - min(tail)) / 2.0 if len(tail) >= 2 else 0.0
            if step > 0:
                peak = max(y)
                overshoot = (peak - target_final) / abs(step) * 100.0
            else:
                peak = min(y)
                overshoot = (target_final - peak) / abs(step) * 100.0
            overshoot = max(0.0, overshoot)

            lo = y0 + 0.1 * step
            hi = y0 + 0.9 * step
            t_lo = None
            t_hi = None
            for i, value in enumerate(y):
                if t_lo is None and ((step > 0 and value >= lo) or (step < 0 and value <= lo)):
                    t_lo = t[i]
                if t_hi is None and ((step > 0 and value >= hi) or (step < 0 and value <= hi)):
                    t_hi = t[i]
                    break
            rise = (t_hi - t_lo) if t_lo is not None and t_hi is not None else None

            band = max(0.02 * abs(step), ripple)
            settle = 0.0
            for i, value in enumerate(y):
                if abs(value - target_final) > band:
                    settle = t[i] - t[0]
            results[mid] = {
                "rise": rise,
                "overshoot": overshoot,
                "settle": settle,
                "ss_err": ss_err,
                "target": target_final,
                "ripple": ripple,
                "n": n,
            }
        return results

    def _annotate_metrics(self, mode: str):
        metrics = self.compute_step_metrics(mode)
        unit = "RPM" if mode == "rpm" else "deg"
        lines = []
        for mid in sorted(metrics):
            item = metrics[mid]
            if item is None:
                lines.append(f"M{mid}: 資料不足或無明顯階躍")
                continue
            rise = f"{item['rise'] * 1000:.0f}ms" if item["rise"] is not None else "--"
            lines.append(
                f"M{mid}: rise {rise}, OS {item['overshoot']:.1f}%, "
                f"settle {item['settle'] * 1000:.0f}ms, err {item['ss_err']:+.1f}{unit}"
            )
        text = "\n".join(lines) if lines else "步階指標：無資料"
        self.metrics_label.setText(text)
        if lines:
            self.ax.text(
                0.01,
                0.02,
                text,
                transform=self.ax.transAxes,
                fontsize=8.5,
                va="bottom",
                ha="left",
                bbox={"boxstyle": "round,pad=0.25", "facecolor": "white", "alpha": 0.78, "edgecolor": "#bbb"},
            )


def _num(v):
    """把 None / 非數字安全轉成 0。"""
    try:
        return float(v)
    except (TypeError, ValueError):
        return 0.0
