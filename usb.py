import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import serial
import serial.tools.list_ports
import threading

class SerialControlApp:
    def __init__(self, root):
        self.root = root
        self.root.title("ESP32 串列通訊控制")
        self.serial_port = None

        # 連接區
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

        # 指令區
        frame_cmd = ttk.Frame(root)
        frame_cmd.pack(pady=8)
        self.cmd_entry = ttk.Entry(frame_cmd, width=40)
        self.cmd_entry.pack(side=tk.LEFT, padx=4)
        ttk.Button(frame_cmd, text="送出", command=self.send_command).pack(side=tk.LEFT)

        # 回應顯示
        self.text_area = scrolledtext.ScrolledText(root, width=60, height=12, font=("Consolas", 11))
        self.text_area.pack(padx=8, pady=8)

        # 指令說明
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

        self.stop_thread = False
        self.read_thread = None

    def get_serial_ports(self):
        return [port.device for port in serial.tools.list_ports.comports()]

    def refresh_ports(self):
        self.port_combo['values'] = self.get_serial_ports()

    def connect_serial(self):
        port = self.port_var.get()
        if not port:
            messagebox.showwarning("提示", "請選擇序列埠")
            return
        try:
            self.serial_port = serial.Serial(port, 115200, timeout=0.1)
            self.text_area.insert(tk.END, f"已連線到 {port}\n")
            self.stop_thread = False
            self.read_thread = threading.Thread(target=self.read_serial, daemon=True)
            self.read_thread.start()
        except Exception as e:
            messagebox.showerror("連線失敗", str(e))

    def disconnect_serial(self):
        self.stop_thread = True
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()
            self.text_area.insert(tk.END, "已斷線\n")

    def send_command(self):
        cmd = self.cmd_entry.get().strip()
        if self.serial_port and self.serial_port.is_open and cmd:
            self.serial_port.write((cmd + '\n').encode())
            self.text_area.insert(tk.END, f"> {cmd}\n")
            self.cmd_entry.delete(0, tk.END)
        else:
            messagebox.showwarning("提示", "請先連線並輸入指令")

    def read_serial(self):
        while not self.stop_thread and self.serial_port and self.serial_port.is_open:
            try:
                line = self.serial_port.readline().decode(errors='ignore')
                if line:
                    self.text_area.insert(tk.END, line)
                    self.text_area.see(tk.END)
            except Exception:
                break

    def on_close(self):
        self.stop_thread = True
        self.disconnect_serial()
        self.root.destroy()

if __name__ == "__main__":
    root = tk.Tk()
    app = SerialControlApp(root)
    root.protocol("WM_DELETE_WINDOW", app.on_close)
    root.mainloop()
