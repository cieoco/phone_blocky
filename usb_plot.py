import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import serial
import serial.tools.list_ports
import threading
import time
from collections import deque

import matplotlib
matplotlib.use('TkAgg')
# 設定中文字體以避免方框
matplotlib.rcParams['font.sans-serif'] = ['Microsoft YaHei']
matplotlib.rcParams['axes.unicode_minus'] = False

from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk

class SerialControlApp:
    def __init__(self, root):
        self.root = root
        self.root.title("ESP32 串列通訊控制")
        self.serial_port = None

        # --- 連接區 ---
        frame_conn = ttk.Frame(root)
        frame_conn.pack(pady=8)
        ttk.Label(frame_conn, text="選擇序列埠:").pack(side=tk.LEFT)
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(frame_conn, textvariable=self.port_var, width=15)
        self.port_combo['values'] = self.get_serial_ports()
        self.port_combo.pack(side=tk.LEFT, padx=4)
        ttk.Button(frame_conn, text="重新整理", command=self.refresh_ports).pack(side=tk.LEFT)
        ttk.Button(frame_conn, text="連線", command=self.connect_serial).pack(side=tk.LEFT, padx=4)
        ttk.Button(frame_conn, text="斷線", command=self.disconnect_serial).pack(side=tk.LEFT)

        # --- 曲線參數設定 ---
        frame_curve = ttk.Frame(root)
        frame_curve.pack(pady=4)
        ttk.Label(frame_curve, text="讀值指令:").pack(side=tk.LEFT)
        self.curve_cmd_var = tk.StringVar(value="READ,US,2,39")
        ttk.Entry(frame_curve, textvariable=self.curve_cmd_var, width=15).pack(side=tk.LEFT, padx=2)
        ttk.Label(frame_curve, text="間隔(秒):").pack(side=tk.LEFT)
        self.interval_var = tk.DoubleVar(value=1.0)
        ttk.Entry(frame_curve, textvariable=self.interval_var, width=6).pack(side=tk.LEFT, padx=2)
        ttk.Label(frame_curve, text="持續(秒):").pack(side=tk.LEFT)
        self.duration_var = tk.DoubleVar(value=10.0)
        ttk.Entry(frame_curve, textvariable=self.duration_var, width=6).pack(side=tk.LEFT, padx=2)
        self.btn_curve = ttk.Button(frame_curve, text="開始生成曲線", command=self.start_curve)
        self.btn_curve.pack(side=tk.LEFT, padx=4)

        # --- 指令區 ---
        frame_cmd = ttk.Frame(root)
        frame_cmd.pack(pady=8)
        self.cmd_entry = ttk.Entry(frame_cmd, width=40)
        self.cmd_entry.pack(side=tk.LEFT, padx=4)
        self.cmd_entry.bind('<Return>', lambda e: self.send_command())
        ttk.Button(frame_cmd, text="送出", command=self.send_command).pack(side=tk.LEFT)

        # --- 回應顯示 ---
        self.text_area = scrolledtext.ScrolledText(root, width=60, height=12, font=("Consolas", 11))
        self.text_area.pack(padx=8, pady=8)

        # --- 指令說明 ---
        help_text = (
            "指令格式說明：\n"
            "  數位輸入：        READ,D,35    例：READ,D,35\n"
            "  數位輸入(上拉)：  READ,DP,4    例：READ,DP,4\n"
            "  類比輸入：        READ,A,34    例：READ,A,34\n"
            "  超音波感測器：    READ,US,2,39  例：READ,US,2,39\n"
            "  馬達控制：        M1,F,150     例：M1,F,150\n"
            "  伺服馬達控制：    S1,90        例：S1,90\n"
        )
        ttk.Label(root, text=help_text, justify=tk.LEFT, foreground="#2d5be3").pack(padx=8, pady=4)

        # --- 即時曲線緩衝與繪圖設定 ---
        self.max_len = 100
        self.times = deque(maxlen=self.max_len)
        self.values = deque(maxlen=self.max_len)
        self.generating = False

        frame_plot = ttk.Frame(root)
        frame_plot.pack(fill=tk.BOTH, expand=True, padx=8, pady=8)
        self.fig = Figure(figsize=(6, 3), dpi=100)
        self.ax = self.fig.add_subplot(111)
        self.ax.set_title("感測值即時曲線")
        self.ax.set_xlabel("時間 (秒)")
        self.ax.set_ylabel("數值")

        self.canvas = FigureCanvasTkAgg(self.fig, master=frame_plot)
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)
        self.toolbar = NavigationToolbar2Tk(self.canvas, frame_plot)
        self.toolbar.update()
        self.canvas._tkcanvas.pack(fill=tk.BOTH, expand=True)
        ttk.Button(frame_plot, text="儲存圖檔", command=self.save_chart).pack(side=tk.RIGHT, padx=4)

        # 啟動定時更新
        self.update_plot()

    def get_serial_ports(self):
        return [port.device for port in serial.tools.list_ports.comports()]

    def refresh_ports(self):
        self.port_combo['values'] = self.get_serial_ports()

    def connect_serial(self):
        port = self.port_var.get()
        if not port:
            messagebox.showwarning("提示", "請先選擇序列埠")
            return
        try:
            self.serial_port = serial.Serial(port, 115200, timeout=0.5)
            self.text_area.insert(tk.END, f"已連線到 {port}\n")
        except Exception as e:
            messagebox.showerror("連線失敗", str(e))

    def disconnect_serial(self):
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()
            self.text_area.insert(tk.END, "已斷線\n")

    def send_command(self):
        cmd = self.cmd_entry.get().strip()
        if not (self.serial_port and self.serial_port.is_open and cmd):
            messagebox.showwarning("提示", "請先連線並輸入指令")
            return
        self.serial_port.write((cmd + '\n').encode())
        self.text_area.insert(tk.END, f"> {cmd}\n")
        try:
            raw = self.serial_port.readline().decode(errors='ignore').strip()
            if raw:
                self.text_area.insert(tk.END, raw + "\n")
        except Exception:
            pass
        self.text_area.see(tk.END)

    def start_curve(self, event=None):
        # 移除連線檢查，允許無序列埠下也能啟動
        interval = self.interval_var.get()
        duration = self.duration_var.get()
        cmd = self.curve_cmd_var.get().strip()
        # 基本檢查
        if interval <= 0 or duration <= 0:
            messagebox.showerror("參數錯誤", "間隔與持續時間需大於 0")
            return
        if not cmd:
            messagebox.showwarning("提示", "請輸入讀值指令")
            return
        # 清除舊資料並顯示啟動訊息
        self.times.clear(); self.values.clear()
        self.text_area.insert(tk.END, f"開始生成曲線: {cmd}，間隔 {interval}s，持續 {duration}s\n")
        self.start_time = time.time()
        self.generating = True
        self.btn_curve.config(state=tk.DISABLED)
        threading.Thread(target=self.generate_curve, args=(cmd, interval, duration), daemon=True).start()

    def generate_curve(self, cmd, interval, duration):
        parts = cmd.split(',')
        mode = parts[1] if len(parts) > 1 else parts[-1]
        end_time = self.start_time + duration
        while time.time() < end_time and self.generating:
            val = None  # <--- 新增這一行，預設為 None
            # 送出命令僅在連線時執行
            if self.serial_port and self.serial_port.is_open:
                self.serial_port.write((cmd + '\n').encode())
            time.sleep(interval)
            # 讀取並解析
            if self.serial_port and self.serial_port.is_open:
                while True:
                    raw = self.serial_port.readline().decode(errors='ignore').strip()
                    if not raw:
                        break
                    self.text_area.insert(tk.END, raw + "\n")
                    if raw.startswith(mode) and '=' in raw:
                        try:
                            val = float(raw.split('=', 1)[1])
                        except ValueError:
                            continue
            else:
                # 無連線時，產生預設值 0
                raw = f"{mode}=0"
                self.text_area.insert(tk.END, raw + "\n")
                val = 0.0
            # 只有 val 有值時才 append
            if val is not None:
                t = time.time() - self.start_time
                self.times.append(t)
                self.values.append(val)
        # 結束
        self.generating = False
        self.btn_curve.config(state=tk.NORMAL)
        self.text_area.insert(tk.END, f"曲線生成完成，共 {len(self.times)} 點\n")
        self.text_area.see(tk.END)

    def update_plot(self):
        if self.times and self.values:
            self.ax.clear()
            self.ax.plot(self.times, self.values, marker='o')
            self.ax.set_title(f"{self.curve_cmd_var.get()} 即時曲線")
            self.ax.set_xlabel("時間 (秒)")
            self.ax.set_ylabel("數值")
            self.ax.grid(True)
            self.canvas.draw()
        self.root.after(200, self.update_plot)

    def save_chart(self):
        filename = f"{self.curve_cmd_var.get()}_{int(time.time())}.png"
        self.fig.savefig(filename, dpi=300)
        messagebox.showinfo("儲存完成", f"已儲存成 {filename}")

    def on_close(self):
        self.generating = False
        self.disconnect_serial()
        self.root.destroy()

if __name__ == '__main__':
    root = tk.Tk()
    app = SerialControlApp(root)
    root.protocol("WM_DELETE_WINDOW", app.on_close)
    root.mainloop()
