import importlib.util
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("competition_pair", Path(__file__).resolve().parents[1] / "tools/competition_pair.py")
pair = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pair)


class PairingChannelTests(unittest.TestCase):
    def test_sta_uses_actual_router_channel(self):
        ref = {"wifi_mode": "STA", "wifi_connected": True, "channel": 6}
        self.assertEqual(pair.pairing_channel(ref, None), 6)
        self.assertEqual(pair.pairing_channel(ref, 6), 6)
        with self.assertRaises(RuntimeError):
            pair.pairing_channel(ref, 1)

    def test_sta_offline_or_unsupported_channel_rejected(self):
        for connected, channel in [(False, 6), (True, 0), (True, 12), (True, None)]:
            with self.assertRaises(RuntimeError):
                pair.pairing_channel({"wifi_mode": "STA", "wifi_connected": connected, "channel": channel}, None)

    def test_old_ap_fallback(self):
        self.assertEqual(pair.pairing_channel({}, None), 1)
        self.assertEqual(pair.pairing_channel({}, 11), 11)

    def test_ap_sta_still_uses_router_channel(self):
        ref = {"wifi_mode": "AP_STA", "wifi_connected": True, "channel": 11}
        self.assertEqual(pair.pairing_channel(ref, None), 11)
        with self.assertRaises(RuntimeError):
            pair.pairing_channel(ref, 1)
