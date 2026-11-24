from collections import deque
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[2]
MASTER_PATH = ROOT / "assets/avatar/master/dotnet-bot-master-provisional.png"
GENERATED_DIR = ROOT / "assets/avatar/keyframes/generated"
OUTPUT_DIR = ROOT / "assets/avatar/keyframes/provisional"
PREVIEW_DIR = ROOT / "assets/avatar/previews/provisional"
EYE_REGION = (560, 480, 980, 625)


def dark_components(image: Image.Image) -> list[tuple[int, tuple[int, int, int, int]]]:
    pixels = image.load()
    x0, y0, x1, y1 = EYE_REGION
    seen: set[tuple[int, int]] = set()
    components = []

    for y in range(y0, y1):
        for x in range(x0, x1):
            if max(pixels[x, y]) > 80 or (x, y) in seen:
                continue

            queue = deque([(x, y)])
            seen.add((x, y))
            bounds = [x, y, x, y]
            area = 0
            while queue:
                cx, cy = queue.popleft()
                area += 1
                bounds[0] = min(bounds[0], cx)
                bounds[1] = min(bounds[1], cy)
                bounds[2] = max(bounds[2], cx)
                bounds[3] = max(bounds[3], cy)

                for ny in range(max(y0, cy - 1), min(y1, cy + 2)):
                    for nx in range(max(x0, cx - 1), min(x1, cx + 2)):
                        if (nx, ny) in seen or max(pixels[nx, ny]) > 80:
                            continue
                        seen.add((nx, ny))
                        queue.append((nx, ny))

            if area > 100:
                components.append(
                    (area, (bounds[0], bounds[1], bounds[2] + 1, bounds[3] + 1))
                )

    return sorted(components, key=lambda component: component[1][0])


def expand(
    bounds: tuple[int, int, int, int], padding: int, size: tuple[int, int]
) -> tuple[int, int, int, int]:
    x0, y0, x1, y1 = bounds
    return (
        max(0, x0 - padding),
        max(0, y0 - padding),
        min(size[0], x1 + padding),
        min(size[1], y1 + padding),
    )


def clear_eye_boxes(image: Image.Image, components: list[tuple[int, tuple[int, int, int, int]]]) -> None:
    pixels = image.load()
    for _, bounds in components:
        x0, y0, x1, y1 = expand(bounds, 4, image.size)
        for y in range(y0, y1):
            left = pixels[x0 - 1, y]
            right = pixels[x1, y]
            span = max(1, x1 - x0 + 1)
            for x in range(x0, x1):
                fraction = (x - x0 + 1) / span
                pixels[x, y] = tuple(
                    round(left[channel] * (1 - fraction) + right[channel] * fraction)
                    for channel in range(3)
                )


def overlay_eye_shapes(
    base: Image.Image,
    generated: Image.Image,
    components: list[tuple[int, tuple[int, int, int, int]]],
) -> set[tuple[int, int]]:
    base_pixels = base.load()
    generated_pixels = generated.load()
    allowed = set()

    for _, bounds in components:
        x0, y0, x1, y1 = expand(bounds, 3, base.size)
        allowed.update((x, y) for y in range(y0, y1) for x in range(x0, x1))
        for y in range(y0, y1):
            for x in range(x0, x1):
                red, green, blue = generated_pixels[x, y]
                luminance = (red + green + blue) // 3
                alpha = max(0, min(255, round((235 - luminance) * 255 / 235)))
                if alpha == 0:
                    continue
                original = base_pixels[x, y]
                base_pixels[x, y] = tuple(
                    round(channel * (255 - alpha) / 255) for channel in original
                )

    return allowed


def verify_only_eyes_changed(
    master: Image.Image,
    result: Image.Image,
    allowed_boxes: list[tuple[int, int, int, int]],
) -> int:
    before = master.load()
    after = result.load()
    width, height = master.size
    allowed = set()
    for bounds in allowed_boxes:
        x0, y0, x1, y1 = expand(bounds, 4, master.size)
        allowed.update((x, y) for y in range(y0, y1) for x in range(x0, x1))

    changed = 0
    for y in range(height):
        for x in range(width):
            if before[x, y] == after[x, y]:
                continue
            changed += 1
            if (x, y) not in allowed:
                raise ValueError(f"Pixel changed outside eye masks at ({x}, {y})")
    return changed


def make_preview(image: Image.Image) -> Image.Image:
    preview = image.copy()
    preview.thumbnail((320, 240), Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", (320, 240), "black")
    canvas.paste(preview, ((320 - preview.width) // 2, (240 - preview.height) // 2))
    return canvas


def main() -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    PREVIEW_DIR.mkdir(parents=True, exist_ok=True)
    master = Image.open(MASTER_PATH).convert("RGB")
    base_components = dark_components(master)
    if len(base_components) != 2:
        raise ValueError(f"Expected two master eyes, found {len(base_components)}")

    frames: dict[str, Image.Image] = {"idle-neutral": master.copy()}
    for name in ("idle-blink", "working", "attention", "complete", "surprise"):
        generated = Image.open(GENERATED_DIR / f"{name}.raw.png").convert("RGB")
        if generated.size != master.size:
            raise ValueError(f"{name} size {generated.size} does not match master {master.size}")
        components = dark_components(generated)
        if len(components) != 2:
            raise ValueError(f"Expected two generated eye shapes for {name}, found {len(components)}")

        composed = master.copy()
        clear_eye_boxes(composed, base_components)
        overlay_eye_shapes(composed, generated, components)
        allowed = [bounds for _, bounds in base_components]
        allowed.extend(bounds for _, bounds in components)
        changed = verify_only_eyes_changed(master, composed, allowed)
        composed.save(OUTPUT_DIR / f"{name}.png", format="PNG", optimize=True)
        print(f"{name}: changed {changed} pixels, all changes within eye masks")
        frames[name] = composed

    frames["idle-neutral"].save(
        OUTPUT_DIR / "idle-neutral.png", format="PNG", optimize=True
    )

    sequence_names = (
        "idle-neutral",
        "idle-blink",
        "idle-neutral",
        "working",
        "attention",
        "complete",
        "idle-neutral",
    )
    previews = {}
    for name, image in frames.items():
        preview = make_preview(image)
        preview.save(PREVIEW_DIR / f"{name}-320x240.png", format="PNG", optimize=True)
        previews[name] = preview

    sheet = Image.new("RGB", (990, 530), (24, 24, 30))
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.load_default()
    for index, name in enumerate(frames):
        column = index % 3
        row = index // 3
        x = 10 + column * 330
        y = 8 + row * 260
        draw.text((x, y), name, fill="white", font=font)
        sheet.paste(previews[name], (x, y + 20))
    sheet.save(PREVIEW_DIR / "keyframes-contact-sheet.png", format="PNG", optimize=True)

    palette_source = Image.new("RGB", (320, 240 * len(sequence_names)))
    for index, name in enumerate(sequence_names):
        palette_source.paste(previews[name], (0, index * 240))
    palette = palette_source.quantize(colors=64).getpalette()
    palette_image = Image.new("P", (1, 1))
    palette_image.putpalette(palette)
    gif_frames = [
        previews[name].quantize(palette=palette_image, dither=Image.Dither.NONE)
        for name in sequence_names
    ]
    gif_frames[0].save(
        PREVIEW_DIR / "avatar-sequence-preview.gif",
        save_all=True,
        append_images=gif_frames[1:],
        duration=(450, 120, 450, 550, 550, 900, 450),
        loop=0,
        optimize=False,
    )

    surprise = previews["surprise"]
    surprise.save(PREVIEW_DIR / "surprise-preview-320x240.png", format="PNG", optimize=True)
    print(f"Created {len(frames)} provisional 320x240 keyframes, contact sheet, and sequence preview.")


if __name__ == "__main__":
    main()
