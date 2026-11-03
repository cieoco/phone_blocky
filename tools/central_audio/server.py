"""Local-only audio desk; Python standard library, no cloud or stored passwords."""
import argparse
import ipaddress
import json
import secrets
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WWW = Path(__file__).resolve().parent / "www"
SOUNDS = {"start", "resume", "end", "abort"}
PRIVATE_NETS = tuple(ipaddress.ip_network(net) for net in
                     ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16"))


class DeskError(Exception):
    pass


def board_host(value):
    """Only an explicit LAN IPv4, HTTP port 80; never DNS, redirects or proxies."""
    if not isinstance(value, str):
        raise DeskError("請填入裁判板的區網 IPv4 位址。")
    try:
        url = urllib.parse.urlsplit(value.strip() if "://" in value else "http://" + value.strip())
        ip = ipaddress.IPv4Address(url.hostname or "")
        valid = (url.scheme == "http" and url.port in (None, 80) and not url.username
                 and not url.password and url.path in ("", "/") and not url.query and not url.fragment
                 and any(ip in net for net in PRIVATE_NETS))
    except (ValueError, TypeError):
        valid = False
    if not valid:
        raise DeskError("請使用裁判板的區網 IPv4（例如 192.168.1.50），不要填帳密或其他網址。")
    return str(ip)


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def fetch_board(host, path, key, payload=None):
    body = None if payload is None else json.dumps(payload).encode("utf-8")
    request = urllib.request.Request("http://" + host + path, data=body,
                                     headers={"Content-Type": "application/json", "X-Competition-Key": key})
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
    try:
        with opener.open(request, timeout=0.7) as response:
            raw = response.read(65537)
            if len(raw) > 65536:
                raise DeskError("裁判板回覆過大。")
            result = json.loads(raw)
            if not isinstance(result, dict):
                raise ValueError("invalid response")
            return result
    except urllib.error.HTTPError as exc:
        raise DeskError("裁判密碼不正確。" if exc.code == 403 else "裁判板拒絕音效連線。") from None
    except (OSError, ValueError, urllib.error.URLError):
        raise DeskError("無法連上裁判板，請確認 IP 與區域網路。") from None


def exchange(host, key, audio_id, operation):
    request_id = secrets.token_hex(16)
    fetch_board(host, "/api/competition/action", key,
                {"action": "audio_" + operation, "audio_id": audio_id, "request_id": request_id})
    deadline = time.monotonic() + 1.6
    while time.monotonic() < deadline:
        state = fetch_board(host, "/api/competition/state", key)
        result = next((r.get("result") for r in state.get("results", []) if r.get("id") == request_id), None)
        if result is not None:
            if result == "unknown_action" or "audio_route" not in state:
                raise DeskError("請先更新裁判板韌體，這塊板尚未支援中央音效。")
            if result != "ok":
                raise DeskError("另一個中央音效台持有播放權，請關閉另一頁後稍候。")
            if operation == "heartbeat" and (state.get("audio_route") != "CENTRAL" or state.get("audio_owner") != audio_id):
                raise DeskError("尚未確認中央播放權。")
            # Deliberately expose no key, team registry or general-purpose command proxy.
            fields = ("boot_id", "match_id", "event_id", "sound", "state", "remaining_ms", "mac",
                      "audio_route", "audio_owner", "audio_lease_ms")
            return {name: state.get(name) for name in fields}
        time.sleep(0.04)
    raise DeskError("裁判板未確認音效連線，暫停播放。")


class DeskServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, port, audio_dir):
        self.token = secrets.token_urlsafe(32)
        self.audio_dir = Path(audio_dir).resolve()
        # Two fields, bounded parallel requests. A slow board does not block the other.
        self.slots = threading.BoundedSemaphore(8)
        super().__init__(("127.0.0.1", port), DeskHandler)


class DeskHandler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass  # Never log submitted credentials or bodies.

    def local_request(self):
        port = self.server.server_port
        host = self.headers.get("Host", "")
        return host in (f"127.0.0.1:{port}", f"localhost:{port}") and \
            self.headers.get("Origin", "http://" + host) == "http://" + host

    def send_bytes(self, status, body, content_type="application/json; charset=utf-8"):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; media-src 'self'; frame-ancestors 'none'")
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def send_json(self, status, value):
        self.send_bytes(status, json.dumps(value, ensure_ascii=False).encode("utf-8"))

    def do_GET(self):
        if not self.local_request():
            self.send_json(403, {"error": "僅允許本機網頁存取。"})
            return
        path = urllib.parse.urlsplit(self.path).path
        if path == "/api/bootstrap":
            self.send_json(200, {"token": self.server.token})
            return
        assets = {"/": (WWW / "index.html", "text/html; charset=utf-8"),
                  "/desk.js": (WWW / "desk.js", "text/javascript; charset=utf-8"),
                  "/desk.css": (WWW / "desk.css", "text/css; charset=utf-8"),
                  "/competition_audio.js": (ROOT / "referee_data/competition_audio.js", "text/javascript; charset=utf-8")}
        for name in SOUNDS:
            assets[f"/audio/{name}.wav"] = (self.server.audio_dir / f"{name}.wav", "audio/wav")
        asset = assets.get(path)
        if not asset or not asset[0].is_file():
            self.send_json(404, {"error": "找不到檔案；請確認本機四個 WAV 已備妥。"})
            return
        self.send_bytes(200, asset[0].read_bytes(), asset[1])

    def do_POST(self):
        if not self.local_request() or not secrets.compare_digest(self.headers.get("X-Local-Token", ""), self.server.token):
            self.send_json(403, {"error": "請從本機中央音效頁操作。"})
            return
        if self.path != "/api/observe" or self.headers.get_content_type() != "application/json":
            self.send_json(400, {"error": "不支援的請求。"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 1 <= length <= 2048:
                raise ValueError()
            data = json.loads(self.rfile.read(length))
            if not isinstance(data, dict):
                raise ValueError()
            host = board_host(data.get("host"))
            key, audio_id, op = data.get("key"), data.get("audio_id"), data.get("operation", "heartbeat")
            if not isinstance(key, str) or not 8 <= len(key) <= 64 or any(ord(c) < 32 or ord(c) > 126 for c in key):
                raise DeskError("請填入裁判操作密碼。")
            if not isinstance(audio_id, str) or len(audio_id) != 16 or any(c not in "0123456789abcdef" for c in audio_id) or int(audio_id, 16) == 0:
                raise ValueError()
            if op not in ("heartbeat", "release"):
                raise ValueError()
        except (ValueError, TypeError, UnicodeError):
            self.send_json(400, {"error": "請求格式不正確。"})
            return
        except DeskError as exc:
            self.send_json(400, {"error": str(exc)})
            return
        if not self.server.slots.acquire(blocking=False):
            self.send_json(503, {"error": "中央台忙碌，稍後重試。"})
            return
        try:
            self.send_json(200, exchange(host, key, audio_id, op))
        except DeskError as exc:
            self.send_json(502, {"error": str(exc)})
        finally:
            self.server.slots.release()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--audio-dir", type=Path, default=ROOT / "referee_data/audio")
    parser.add_argument("--open", action="store_true", help="Open the local desk in the default browser")
    args = parser.parse_args()
    server = DeskServer(args.port, args.audio_dir)
    url = f"http://127.0.0.1:{server.server_port}/"
    print(f"Central audio desk: {url}\nKeep this process open. Ctrl+C to stop.", flush=True)
    if args.open:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
