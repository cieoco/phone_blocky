import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("flash_profiles", Path(__file__).resolve().parents[1] / "motor_tuner/flash_profiles.py")
profiles = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profiles)


class FlashProfileTests(unittest.TestCase):
    def test_each_role_uses_one_complete_image_directory(self):
        project, bundle = Path("project"), Path("bundle")
        for role in ("esp32dev", "esp32dev_competitor"):
            for name, _offset in profiles.FLASH_IMAGES:
                self.assertEqual(profiles.image_path(name, role, project, bundle), project / ".pio/build" / role / name)
                self.assertEqual(profiles.image_path(name, role, project, bundle, True), bundle / "bundled_bins" / role / name)

    def test_packaged_missing_image_never_falls_back_to_developer_firmware(self):
        result = profiles.image_path("firmware.bin", "esp32dev_competitor", Path("project"), Path("missing_bundle"), True)
        self.assertEqual(result, Path("missing_bundle/bundled_bins/esp32dev_competitor/firmware.bin"))
        with self.assertRaises(ValueError):
            profiles.image_path("firmware.bin", "esp32s3_zero_referee", Path("project"), Path("bundle"))
        with self.assertRaises(ValueError):
            profiles.image_path("../../firmware.bin", "esp32dev", Path("project"), Path("bundle"))
