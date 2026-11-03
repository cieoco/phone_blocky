"""USB provisioning for the isolated referee/competitor firmware. Never stores keys."""
from __future__ import annotations

import argparse
import json
import secrets
import time


def exchange(port, message, timeout=12):
    port.reset_input_buffer()
    port.write((json.dumps(message, ensure_ascii=False) + "\n").encode("utf-8"))
    port.flush()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = port.readline().decode("utf-8", errors="replace").strip()
        if not line.startswith("{"):
            continue
        try:
            result = json.loads(line)
        except json.JSONDecodeError:
            continue
        if "ok" in result:
            if result["ok"] is not True:
                raise RuntimeError("控制板拒絕設定；請確認沒有正在準備或進行中的比賽。")
            return result
    raise RuntimeError("USB 回覆逾時；請確認板子角色、序列埠與韌體。")


def connect(name):
    import serial
    port = serial.Serial()
    port.port, port.baudrate, port.timeout = name, 115200, 0.3
    port.dtr, port.rts = False, False
    port.open()
    time.sleep(2)
    return port


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--referee", help="裁判板 USB，例如 COM5")
    parser.add_argument("--student", help="學生板 USB，例如 COM6")
    parser.add_argument("--name", help="隊伍名稱，UTF-8 最多 48 bytes")
    parser.add_argument("--channel", type=int, choices=range(1, 12), default=1)
    parser.add_argument("--info", metavar="PORT", help="僅查詢指定控制板")
    parser.add_argument("--show-credentials", action="store_true", help="在本機終端顯示 AP／網頁密碼；請勿貼到版本庫或公開日誌")
    args = parser.parse_args()
    if args.info:
        with connect(args.info) as port:
            info = exchange(port, {"cmd": "competition_info"})
        if not args.show_credentials:
            info.pop("key", None)
        print(json.dumps(info, ensure_ascii=False, indent=2))
        return
    if not args.referee or not args.student or not args.name:
        parser.error("配對需要 --referee、--student、--name；或使用 --info PORT")
    if args.referee.lower() == args.student.lower():
        parser.error("裁判板與學生板不能使用相同序列埠")
    if not 1 <= len(args.name.encode("utf-8")) <= 48:
        parser.error("隊伍名稱需為 1–48 個 UTF-8 bytes")
    with connect(args.referee) as referee, connect(args.student) as student:
        ref = exchange(referee, {"cmd": "competition_info"})
        car = exchange(student, {"cmd": "competition_info"})
        if ref.get("role") != "referee" or car.get("role") != "competitor":
            raise RuntimeError("角色不符；請先燒錄對應的競賽韌體。")
        key = secrets.token_hex(16)
        # Write the car first. If the second write fails, rerun the whole operation.
        exchange(student, {"cmd": "competition_pair", "mac": ref["mac"], "key": key, "name": "Referee"})
        exchange(referee, {"cmd": "competition_pair", "mac": car["mac"], "key": key, "name": args.name})
        for port in (referee, student):
            exchange(port, {"cmd": "competition_channel", "channel": args.channel})
    print(f"已配對 {args.name} ({car['mac']})，場地頻道 {args.channel}。請重新啟動兩塊板。")
    print("配對金鑰只寫入控制板 NVS，未儲存至本機檔案。")
    if args.show_credentials:
        for label, info in (("裁判板", ref), ("學生板", car)):
            print(f"{label}: Wi-Fi {info['ssid']}；AP／網頁密碼 {info['key']}")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, ImportError) as exc:
        raise SystemExit(str(exc)) from None
