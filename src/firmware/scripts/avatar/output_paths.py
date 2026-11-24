"""Exported artwork artifacts; the exporter's cache metadata is separate."""

from pathlib import Path

FRAME_NAMES = (
    "neutral", "blink", "wink", "smile", "look_up", "look_left",
    "look_right", "sleep", "sleepy",
)


def export_artifacts(root: Path) -> list[Path]:
    device = root / "assets" / "avatar" / "stackchan"
    header = root / "usb-avatar" / "include" / "avatar_assets.h"
    return (
        [device / f"{name}.png" for name in FRAME_NAMES]
        + [device / f"{name}.rgb565" for name in FRAME_NAMES if name != "sleep"]
        + [header, device / "manifest.json", device / "lab-contact-sheet.png",
           device / "petting-grumpy-preview.png"]
    )
