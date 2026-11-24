"""Deterministic PC preview of idle motion, blinks and eye movements.

No model is used and nothing is written to device assets or firmware.
"""
import math
from io import BytesIO
from pathlib import Path

from PIL import Image

from compose_keyframes import (
    clear_eye_boxes,
    dark_components,
    overlay_eye_shapes,
    verify_only_eyes_changed,
)
from export_device_assets import SOURCE, fit_display, write_if_changed

ROOT = Path(__file__).resolve().parents[2]
DEVICE = ROOT / "assets" / "avatar" / "stackchan"
OUT = ROOT / "assets" / "avatar" / "previews" / "provisional" / "idle-motion-preview.gif"

ANTENNA_PIVOT = (160, 53)
ANTENNA_BOX = (140, 20, 181, 53)
ANTENNA_TILT_DEGREES = 12
LED_BOX = (147, 158, 173, 178)
EYE_BOX = (104, 106, 216, 150)
MOTION_FRAMES = 24
FRAMES = 240
FRAME_MS = 80
BLINK_STARTS = (30, 92, 146, 213)
LOOK_EVENTS = (
    (48, 66, "look_up"),
    (108, 126, "look_left"),
    (168, 186, "look_right"),
)


def make_eye_frames() -> dict[str, Image.Image]:
    frames = {}
    for name in ("neutral", "blink", "look_up"):
        with Image.open(DEVICE / f"{name}.png") as image:
            frames[name] = image.convert("RGB")
        if frames[name].size != (320, 240):
            raise ValueError(f"Expected a 320x240 {name} frame.")

    with Image.open(SOURCE / "idle-neutral.png") as image:
        master = image.convert("RGB")
    if master.size != (1536, 1024):
        raise ValueError("Expected the current 1536x1024 provisional master.")
    if fit_display(master).tobytes() != frames["neutral"].tobytes():
        raise ValueError("Neutral export does not match the provisional master.")
    eyes = dark_components(master)
    if len(eyes) != 2:
        raise ValueError("Expected two eyes in the provisional master.")

    # Reuse the master pupils and compositor, shifting toward the viewer's left/right.
    for name, dx in (("look_left", -30), ("look_right", 30)):
        shifted = Image.new("RGB", master.size, "white")
        shifted_bounds = []
        for area, (x0, y0, x1, y1) in eyes:
            box = (x0 - 3, y0 - 3, x1 + 3, y1 + 3)
            shifted.paste(master.crop(box), (box[0] + dx, box[1]))
            shifted_bounds.append((area, (x0 + dx, y0, x1 + dx, y1)))
        result = master.copy()
        clear_eye_boxes(result, eyes)
        overlay_eye_shapes(result, shifted, shifted_bounds)
        verify_only_eyes_changed(
            master, result, [bounds for _, bounds in eyes + shifted_bounds]
        )
        frames[name] = fit_display(result)
    return frames


def eye_state(index: int) -> str:
    index %= FRAMES
    if any(start <= index < start + 2 for start in BLINK_STARTS):
        return "blink"
    for start, end, name in LOOK_EVENTS:
        if start <= index < end:
            return name
    return "neutral"


def antenna(base: Image.Image, angle: float) -> Image.Image:
    frame = base.copy()
    patch = base.crop(ANTENNA_BOX).convert("RGBA")
    # Blank the original antenna region, then paste the rotated copy about its base.
    frame.paste((0, 0, 0), ANTENNA_BOX)
    rotated = patch.rotate(
        angle, resample=Image.Resampling.BICUBIC,
        center=(ANTENNA_PIVOT[0] - ANTENNA_BOX[0], ANTENNA_PIVOT[1] - ANTENNA_BOX[1]),
    )
    frame.paste(rotated, ANTENNA_BOX[:2], rotated)
    return frame


def led(frame: Image.Image, level: float) -> Image.Image:
    # Lighten the dark belt square toward the belt colour; level is 0..1.
    region = frame.crop(LED_BOX).convert("RGB")
    bright = Image.new("RGB", region.size, (160, 150, 250))
    frame.paste(Image.blend(region, bright, level * 0.6), LED_BOX[:2])
    return frame


def render_frame(
    eyes: dict[str, Image.Image], index: int, state: str | None = None
) -> Image.Image:
    """Draw tick ``index``; ``state`` overrides the fixed schedule when given."""
    phase = 2 * math.pi * (index % MOTION_FRAMES) / MOTION_FRAMES
    frame = antenna(
        eyes[eye_state(index) if state is None else state],
        ANTENNA_TILT_DEGREES * math.sin(phase),
    )
    return led(frame, (1 + math.sin(phase)) / 2)


def save_preview(frames: list[Image.Image], path: Path) -> None:
    # One palette for the whole loop avoids quantization shimmer between gestures.
    palette_source = Image.new("RGB", (320, 240 * len(frames)))
    for index, frame in enumerate(frames):
        palette_source.paste(frame, (0, index * 240))
    palette = palette_source.quantize(colors=256)
    gif_frames = [
        frame.quantize(palette=palette, dither=Image.Dither.NONE)
        for frame in frames
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    output = BytesIO()
    gif_frames[0].save(
        output, format="GIF", save_all=True, append_images=gif_frames[1:], duration=FRAME_MS,
        loop=0, optimize=False,
    )
    write_if_changed(path, output.getvalue())


def main() -> None:
    eyes = make_eye_frames()
    frames = [render_frame(eyes, index) for index in range(FRAMES)]
    save_preview(frames, OUT)
    print(f"Preview ready: {OUT.relative_to(ROOT)} ({len(frames)} frames, {FRAMES * FRAME_MS} ms)")


if __name__ == "__main__":
    main()
