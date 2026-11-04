"""Student-board firmware choices shared by the UI and EXE packaging."""
from pathlib import Path

FLASH_PROFILES = (
    ("esp32dev_competitor", "競賽學生車版（含裁判管制）",
     "保留原 Blockly、搖桿與設定；加入比賽後依裁判授權運作，清場後回一般教學。"),
    ("esp32dev", "一般教學版（無裁判管制）",
     "供一般教學／機構測試使用，不接受裁判板的比賽授權。"),
)
DEFAULT_PROFILE = FLASH_PROFILES[0][0]
FLASH_IMAGES = (
    ("bootloader.bin", "0x1000"),
    ("partitions.bin", "0x8000"),
    ("firmware.bin", "0x10000"),
    ("littlefs.bin", "0x1B9000"),
)


def image_path(name: str, profile: str, project_root: Path, bundle_root: Path,
               frozen: bool = False) -> Path:
    if profile not in {p[0] for p in FLASH_PROFILES}:
        raise ValueError("Unknown student firmware profile")
    if name not in {p[0] for p in FLASH_IMAGES}:
        raise ValueError("Unknown firmware image")
    # A packaged EXE never falls back to another profile or a developer's bins.
    directory = (bundle_root / "bundled_bins" / profile if frozen
                 else project_root / ".pio" / "build" / profile)
    return directory / name
