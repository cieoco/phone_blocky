import importlib.util
import json
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("central_audio_server", Path(__file__).resolve().parents[1] / "tools/central_audio/server.py")
desk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(desk)


class CentralAudioTests(unittest.TestCase):
    def test_only_explicit_lan_targets(self):
        self.assertEqual(desk.board_host(" http://192.168.1.50/ "), "192.168.1.50")
        self.assertEqual(desk.board_host("10.0.0.4"), "10.0.0.4")
        for target in (None, "localhost", "127.0.0.1", "8.8.8.8", "https://192.168.1.1", "192.168.1.1:8080",
                       "http://user:pass@192.168.1.1", "192.168.1.1/stop", "192.168.1.1?key=secret", "[::1]"):
            with self.subTest(target=target), self.assertRaises(desk.DeskError):
                desk.board_host(target)

    def test_confirmed_audio_only_exchange(self):
        calls = []
        def fake(host, path, key, payload=None):
            calls.append((host, path, key, payload))
            if payload:
                return {"queued": True}
            command = calls[0][3]
            return {"results": [{"id": command["request_id"], "result": "ok"}], "audio_route": "CENTRAL",
                    "audio_owner": "1234567890abcdef", "audio_lease_ms": 4900, "key": "must-not-return", "teams": [1]}
        with patch.object(desk, "fetch_board", side_effect=fake):
            result = desk.exchange("192.168.1.50", "fake-test-key", "1234567890abcdef", "heartbeat")
        self.assertEqual(calls[0][3]["action"], "audio_heartbeat")
        self.assertNotIn("operator", calls[0][3])
        self.assertNotIn("key", result)
        self.assertNotIn("teams", result)
        self.assertEqual(result["audio_owner"], "1234567890abcdef")

    def test_unconfirmed_or_old_firmware_rejected(self):
        for status in ("unknown_action", "another_audio_station", "wrong_holder"):
            command = {}
            def fake(host, path, key, payload=None):
                if payload:
                    command.update(payload)
                    return {}
                return {"results": [{"id": command["request_id"], "result": "ok" if status == "wrong_holder" else status}],
                        "audio_route": "CENTRAL", "audio_owner": "other"}
            with self.subTest(status=status), patch.object(desk, "fetch_board", side_effect=fake), self.assertRaises(desk.DeskError):
                desk.exchange("192.168.1.50", "fake-test-key", "1234567890abcdef", "heartbeat")

    def test_local_server_boundaries_and_assets(self):
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, "start.wav").write_bytes(b"RIFF-test-local-audio")
            server = desk.DeskServer(0, directory)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            base = f"http://127.0.0.1:{server.server_port}"
            def request(path, payload=None, headers=None):
                body = None if payload is None else json.dumps(payload).encode()
                req = urllib.request.Request(base + path, data=body, headers=headers or {})
                return urllib.request.urlopen(req, timeout=2)
            try:
                with request("/") as response:
                    self.assertIn("中央音效台", response.read().decode())
                with request("/audio/start.wav") as response:
                    self.assertEqual(response.read(), b"RIFF-test-local-audio")
                for path in ("/audio/missing.wav", "/../include/referee_wifi.local.h"):
                    with self.assertRaises(urllib.error.HTTPError) as error:
                        request(path)
                    self.assertEqual(error.exception.code, 404)
                payload = {"host": "192.168.1.50", "key": "fake-test-key", "audio_id": "1234567890abcdef"}
                with self.assertRaises(urllib.error.HTTPError) as error:
                    request("/api/observe", payload)
                self.assertEqual(error.exception.code, 403)
                headers = {"Content-Type": "application/json", "X-Local-Token": server.token}
                with patch.object(desk, "exchange", return_value={"audio_route": "CENTRAL"}) as exchange:
                    with request("/api/observe", payload, headers) as response:
                        self.assertEqual(json.load(response)["audio_route"], "CENTRAL")
                    self.assertEqual(exchange.call_count, 1)
                    with self.assertRaises(urllib.error.HTTPError) as error:
                        request("/api/observe", payload, {**headers, "Origin": "http://unrelated.invalid"})
                    self.assertEqual(error.exception.code, 403)
                    with self.assertRaises(urllib.error.HTTPError) as error:
                        request("/api/observe", {**payload, "operation": "stop"}, headers)
                    self.assertEqual(error.exception.code, 400)
                    self.assertEqual(exchange.call_count, 1, "audio desk must not forward match actions")
            finally:
                server.shutdown()
                server.server_close()
                thread.join(timeout=2)
