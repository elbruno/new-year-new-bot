"""Poll both pinned BSP servo axes without adding a second 20 ms delay."""

from pathlib import Path
import subprocess
import sys

LIBRARY = Path("M5StackChan")
SOURCE = Path("src/utils/motion/motion.cpp")
PINNED_BSP_REVISION = "fb5e9f5697803ca64e45fe7ad8eaea3e4100f766"
ORIGINAL = b"        vTaskDelay(pdMS_TO_TICKS(20));"
PATCHED = b"        vTaskDelay(pdMS_TO_TICKS(1) ? pdMS_TO_TICKS(1) : 1);"


def normalized(data):
    return data.replace(b"\r\n", b"\n")


def patch_bytes(data):
    newline = b"\r\n" if b"\r\n" in data else b"\n"
    original = ORIGINAL.replace(b"\n", newline)
    patched = PATCHED.replace(b"\n", newline)
    if data.count(patched) == 1 and not data.count(original):
        return data
    if data.count(original) != 1 or data.count(patched):
        raise ValueError("unexpected pinned BSP motion scheduler source")
    return data.replace(original, patched, 1)


def patch_library(libdeps):
    root = Path(libdeps) / LIBRARY
    revision = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True
    ).strip()
    if revision != PINNED_BSP_REVISION:
        raise ValueError("StackChan BSP revision does not match the reviewed pin")

    pristine = subprocess.check_output(
        ["git", "-C", str(root), "show", f"{PINNED_BSP_REVISION}:{SOURCE.as_posix()}"]
    )
    expected = patch_bytes(pristine)
    path = root / SOURCE
    current = path.read_bytes()
    if normalized(current) == normalized(expected):
        return 0
    if normalized(current) != normalized(pristine):
        raise ValueError("unexpected or partially patched BSP motion source")

    if b"\r\n" in current and b"\r\n" not in expected:
        expected = expected.replace(b"\n", b"\r\n")
    path.write_bytes(expected)
    return 1


if "Import" in globals():
    Import("env")
    try:
        count = patch_library(Path(env["PROJECT_LIBDEPS_DIR"]) / env["PIOENV"])
        print(f"StackChan BSP motion scheduler: {count} file(s) patched")
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        sys.stderr.write(f"StackChan BSP motion scheduler patch failed: {error}\n")
        env.Exit(1)
