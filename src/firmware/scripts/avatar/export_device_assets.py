"""Build the 320x240 avatar artwork used by the C++ firmware."""

import argparse
import hashlib
from io import BytesIO
import json
from pathlib import Path
import struct
import sys

from PIL import Image, ImageDraw, ImageFont, __version__ as PILLOW_VERSION

from compose_keyframes import clear_eye_boxes, dark_components, overlay_eye_shapes, verify_only_eyes_changed
from output_paths import export_artifacts

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "assets/avatar/keyframes/provisional"
DEVICE = ROOT / "assets/avatar/stackchan"
HEADER = ROOT / "usb-avatar/include/avatar_assets.h"
EYE_PATCH = (104, 106, 216, 150)
PETTING_ART = {
    "happy": ROOT / "assets/avatar/review/0.3.0-triple-petting-color-experiments/happy-c-combined.png",
    "very_happy": ROOT / "assets/avatar/review/0.3.0-triple-petting-color-experiments/very-happy-c-v2.png",
    "grumpy": ROOT / "assets/avatar/review/0.3.0-triple-petting-color-experiments/grumpy-c-v2.png",
}
PETTING_PATCHES = {
    "face": EYE_PATCH,
    "belt": (88, 155, 232, 181),
    "antenna": (140, 20, 181, 60),
}
GRUMPY_FRAME_BOX = (78, 20, 242, 208)
MOTION_SOURCE = ROOT / "assets/avatar/review/0.3.1-motion-expressions"
MOTION_ART = {
    name: [MOTION_SOURCE / f"{prefix}-{index:02}.png" for index in range(1, 4)]
    for name, prefix in (("nod_yes", "nod-yes"), ("shake_no", "shake-no-v2"), ("dance", "dance"))
}
MOTION_FRAME_SEQUENCE = (0, 1, 2, 1)
MOTION_FRAME_MS = 200
GRUMPY_SHAKE_OFFSETS = (0, -3, 3, -4, 4, -3, 3, 0)
GRUMPY_SHAKE_FRAME_MS = 70
CACHE = ROOT / ".cache/avatar-device-assets.json"


def write_if_changed(path: Path, content: bytes) -> bool:
    if path.exists() and path.read_bytes() == content:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)
    return True


def png_bytes(image: Image.Image) -> bytes:
    output = BytesIO()
    image.save(output, format="PNG", optimize=True)
    return output.getvalue()


def export_outputs() -> list[Path]:
    """Return artwork artifacts, excluding the export cache metadata."""
    return export_artifacts(ROOT)


def export_fingerprint() -> str:
    digest = hashlib.sha256()
    digest.update(f"{sys.version_info[:3]}:{PILLOW_VERSION}".encode("ascii"))
    inputs = [
        Path(__file__).resolve(), Path(__file__).resolve().with_name("compose_keyframes.py"),
        Path(__file__).resolve().with_name("output_paths.py"),
        Path(__file__).resolve().with_name("preview_idle_motion.py"),
        SOURCE / "idle-neutral.png", SOURCE / "idle-blink.png", SOURCE / "complete.png",
        *PETTING_ART.values(), *(path for paths in MOTION_ART.values() for path in paths),
    ]
    for path in inputs:
        digest.update(path.relative_to(ROOT).as_posix().encode("ascii"))
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()


def output_hashes() -> dict[str, str]:
    return {
        path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in export_outputs()
    }


def exports_are_current(fingerprint: str) -> bool:
    if not CACHE.exists():
        return False
    cached = json.loads(CACHE.read_text(encoding="utf-8"))
    return (
        cached.get("inputs") == fingerprint
        and all(path.exists() for path in export_outputs())
        and cached.get("outputs") == output_hashes()
    )


def fit_display(image: Image.Image) -> Image.Image:
    image = image.copy()
    image.thumbnail((320, 240), Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", (320, 240), "black")
    canvas.paste(image, ((320 - image.width) // 2, (240 - image.height) // 2))
    return canvas


def make_frames() -> dict[str, Image.Image]:
    neutral = Image.open(SOURCE / "idle-neutral.png").convert("RGB")
    blink = Image.open(SOURCE / "idle-blink.png").convert("RGB")
    smile = Image.open(SOURCE / "complete.png").convert("RGB")
    if neutral.size != (1536, 1024) or blink.size != neutral.size or smile.size != neutral.size:
        raise ValueError("This exporter requires the current 1536x1024 provisional master.")

    wink = neutral.copy()
    eye_box = (560, 480, 770, 625)
    wink.paste(blink.crop(eye_box), eye_box[:2])

    eyes = dark_components(neutral)
    if len(eyes) != 2:
        raise ValueError("Expected two eyes in the provisional master.")
    look_up = neutral.copy()
    clear_eye_boxes(look_up, eyes)
    shifted = Image.new("RGB", neutral.size, "white")
    shifted_bounds = []
    for area, (x0, y0, x1, y1) in eyes:
        box = (x0 - 3, y0 - 3, x1 + 3, y1 + 3)
        shifted.paste(neutral.crop(box), (box[0], box[1] - 30))
        shifted_bounds.append((area, (x0, y0 - 30, x1, y1 - 30)))
    overlay_eye_shapes(look_up, shifted, shifted_bounds)

    look_left, look_right = {}, {}
    for name, dx in (("look_left", -30), ("look_right", 30)):
        shifted = Image.new("RGB", neutral.size, "white")
        shifted_bounds = []
        for area, (x0, y0, x1, y1) in eyes:
            box = (x0 - 3, y0 - 3, x1 + 3, y1 + 3)
            shifted.paste(neutral.crop(box), (box[0] + dx, box[1]))
            shifted_bounds.append((area, (x0 + dx, y0, x1 + dx, y1)))
        lateral = neutral.copy()
        clear_eye_boxes(lateral, eyes)
        overlay_eye_shapes(lateral, shifted, shifted_bounds)
        verify_only_eyes_changed(
            neutral, lateral, [bounds for _, bounds in eyes + shifted_bounds]
        )
        if name == "look_left":
            look_left = lateral
        else:
            look_right = lateral

    sleepy = neutral.copy()
    cleared = neutral.copy()
    clear_eye_boxes(cleared, eyes)
    for _, (x0, y0, x1, y1) in eyes:
        lid = (x0 - 4, y0 - 4, x1 + 4, (y0 + y1) // 2)
        sleepy.paste(cleared.crop(lid), lid[:2])
    verify_only_eyes_changed(neutral, sleepy, [bounds for _, bounds in eyes])

    return {
        name: fit_display(image)
        for name, image in {
            "neutral": neutral, "blink": blink, "wink": wink,
            "smile": smile, "look_up": look_up, "look_left": look_left,
            "look_right": look_right, "sleep": blink, "sleepy": sleepy,
        }.items()
    }

def make_idle_patches(frames: dict[str, Image.Image]) -> dict[str, list[Image.Image]]:
    from preview_idle_motion import (
        ANTENNA_BOX,
        LED_BOX,
        MOTION_FRAMES,
        render_frame,
    )

    patches = {
        "eyes": [frames[name].crop(EYE_PATCH) for name in
                 ("neutral", "blink", "look_up", "look_left", "look_right")],
        "antenna": [],
        "led": [],
    }
    for index in range(MOTION_FRAMES):
        frame = render_frame(frames, index, state="neutral")
        patches["antenna"].append(frame.crop(ANTENNA_BOX))
        patches["led"].append(frame.crop(LED_BOX))
    return patches


def make_petting_patches() -> dict[str, dict[str, Image.Image]]:
    patches = {}
    for expression, path in PETTING_ART.items():
        image = Image.open(path).convert("RGB")
        if image.size != (320, 240):
            raise ValueError(f"Approved {expression} artwork must be 320x240.")
        patches[expression] = {
            region: image.crop(box) for region, box in PETTING_PATCHES.items()
        }
    return patches


def make_grumpy_frame(neutral: Image.Image, patches: dict[str, Image.Image]) -> Image.Image:
    composed = neutral.copy()
    for region, box in PETTING_PATCHES.items():
        composed.paste(patches[region], box[:2])
    return composed.crop(GRUMPY_FRAME_BOX)


def make_motion_frames() -> dict[str, list[Image.Image]]:
    frames = {}
    for name, paths in MOTION_ART.items():
        frames[name] = []
        for path in paths:
            image = Image.open(path).convert("RGB")
            if image.size != (320, 240):
                raise ValueError(f"Approved motion artwork must be 320x240: {path.name}")
            outside = image.copy()
            outside.paste((0, 0, 0), GRUMPY_FRAME_BOX)
            if any(rgb565(outside)):
                raise ValueError(f"Motion artwork exceeds the avatar region: {path.name}")
            frames[name].append(image.crop(GRUMPY_FRAME_BOX))
    return frames


def append_image_array(header: list[str], name: str, images: list[Image.Image]) -> None:
    width, height = images[0].size
    header.append(f"static const uint16_t {name}[{len(images)}][{width * height}] PROGMEM = {{")
    for image in images:
        values = rgb565(image)
        header.append("    {")
        header.extend(
            "        " + ",".join(f"0x{value:04x}" for value in values[start:start + 16]) + ","
            for start in range(0, len(values), 16)
        )
        header.append("    },")
    header.extend(["};", ""])


def rgb565(image: Image.Image) -> list[int]:
    return [
        ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        for red, green, blue in image.convert("RGB").get_flattened_data()
    ]


def main(force: bool = False) -> None:
    fingerprint = export_fingerprint()
    if not force and exports_are_current(fingerprint):
        print("Device assets are up to date (input and output hashes verified).")
        return
    from preview_idle_motion import ANTENNA_TILT_DEGREES

    DEVICE.mkdir(parents=True, exist_ok=True)
    HEADER.parent.mkdir(parents=True, exist_ok=True)
    frames = make_frames()
    header = ["#pragma once", "#include <stdint.h>", "#include <pgmspace.h>", ""]
    for name, image in frames.items():
        encoded = png_bytes(image)
        write_if_changed(DEVICE / f"{name}.png", encoded)
        if name == "sleep":
            continue  # Sleep and blink share the same flash data.
        values = rgb565(image)
        write_if_changed(
            DEVICE / f"{name}.rgb565", struct.pack(f"<{len(values)}H", *values)
        )
        header.append(f"static const uint16_t avatar_{name}[320 * 240] PROGMEM = {{")
        header.extend(
            "    " + ",".join(f"0x{value:04x}" for value in values[start:start + 16]) + ","
            for start in range(0, len(values), 16)
        )
        header.extend(["};", ""])
    patches = make_idle_patches(frames)
    petting = make_petting_patches()
    write_if_changed(DEVICE / "petting-grumpy-preview.png", png_bytes(
        Image.open(PETTING_ART["grumpy"]).convert("RGB")
    ))
    for expression, regions in petting.items():
        for region, image in regions.items():
            append_image_array(
                header, f"avatar_petting_{expression}_{region}", [image]
            )
    append_image_array(header, "avatar_petting_grumpy_composed",
                       [make_grumpy_frame(frames["neutral"], petting["grumpy"])])
    for name, images in make_motion_frames().items():
        append_image_array(header, f"avatar_motion_{name}", images)
    header.extend([
        f"static constexpr uint32_t avatar_motion_frame_ms = {MOTION_FRAME_MS};",
        "static constexpr uint8_t avatar_motion_sequence[] = {"
        + ", ".join(map(str, MOTION_FRAME_SEQUENCE)) + "};",
        f"static constexpr uint32_t avatar_grumpy_frame_ms = {GRUMPY_SHAKE_FRAME_MS};",
        "static constexpr int8_t avatar_grumpy_offsets[] = {"
        + ", ".join(map(str, GRUMPY_SHAKE_OFFSETS)) + "};", "",
    ])
    append_image_array(header, "avatar_idle_eyes", patches["eyes"])
    append_image_array(header, "avatar_idle_antenna", patches["antenna"])
    append_image_array(header, "avatar_idle_led", patches["led"])
    write_if_changed(HEADER, "\n".join(header).encode("ascii"))
    manifest = json.dumps({
        "master": "candidate-01-provisional",
        "width": 320, "height": 240, "pixelFormat": "RGB565",
        "binaryByteOrder": "little-endian", "bytesPerFrame": 153600,
        "frames": list(frames), "physicalValidation": False,
        "idleAnimation": {
            "tickMs": 80, "motionFrames": 24,
            "eyes": ["neutral", "blink", "look_up", "look_left", "look_right"],
            "eyePatch": {"x": 104, "y": 106, "width": 112, "height": 44},
            "antennaPatch": {"x": 140, "y": 20, "width": 41, "height": 33},
            "ledPatch": {"x": 147, "y": 158, "width": 26, "height": 20},
            "bodyVerticalMotion": False,
            "antennaTiltDegrees": ANTENNA_TILT_DEGREES,
            "patchBytes": {
                "eyes": 5 * 112 * 44 * 2,
                "antenna": 24 * 41 * 33 * 2,
                "led": 24 * 26 * 20 * 2,
            },
        },
        "pettingExpressions": {
            expression: {
                "source": path.relative_to(ROOT).as_posix(),
                "patches": {
                    region: {
                        "x": box[0], "y": box[1],
                        "width": box[2] - box[0], "height": box[3] - box[1],
                    }
                    for region, box in PETTING_PATCHES.items()
                },
            }
            for expression, path in PETTING_ART.items()
        },
    }, indent=2) + "\n"
    write_if_changed(DEVICE / "manifest.json", manifest.encode("ascii"))
    sheet = Image.new("RGB", (320 * 4, 268), "#161616")
    labels = ("Neutral", "Sleepy (provisional)", "! WARNING !", "* IMPORTANT *")
    for index, label in enumerate(labels):
        frame = frames["sleepy" if index == 1 else "neutral"].copy()
        if index >= 2:
            draw = ImageDraw.Draw(frame)
            draw.rounded_rectangle((5, 207, 314, 234), radius=5, fill="yellow" if index == 2 else "red")
            draw.text((160, 221), label, fill="black", anchor="mm", font=ImageFont.load_default(size=16))
        sheet.paste(frame, (index * 320, 0))
        ImageDraw.Draw(sheet).text((index * 320 + 160, 254), label, fill="white", anchor="mm")
    write_if_changed(DEVICE / "lab-contact-sheet.png", png_bytes(sheet))
    write_if_changed(CACHE, (json.dumps({
        "inputs": fingerprint, "outputs": output_hashes(),
    }, indent=2) + "\n").encode("ascii"))
    patch_bytes = sum(len(rgb565(image)) * 2 for images in patches.values() for image in images)
    print(f"Exported nine PNG expressions and eight RGB565 flash frames; idle patches {patch_bytes} bytes.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--force", action="store_true", help="Regenerate even when hashes match.")
    main(parser.parse_args().force)
