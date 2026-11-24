"""PlatformIO pre-build: give the M5Unified speaker task enough stack (decision D034)."""

from pathlib import Path
import sys

# M5Unified 0.2.27 sizes the speaker task stack as 1280 + the DMA buffer, but the task
# also allocates that buffer on its own stack, so the first tone overflowed it.
ORIGINAL = b"size_t stack_size = 1280 + (_cfg.dma_buf_len * sizeof(uint32_t));"
PATCHED = b"size_t stack_size = 4096 + (_cfg.dma_buf_len * sizeof(uint32_t));"
LIBRARY_FILE = Path("M5Unified/src/utility/Speaker_Class.inl")


class PatchError(RuntimeError):
    pass


def patch_speaker_stack(data):
    """Return (data, changed); refuse any file that is not exactly as verified."""
    original, patched = data.count(ORIGINAL), data.count(PATCHED)
    if (original, patched) == (1, 0):
        return data.replace(ORIGINAL, PATCHED), True
    if (original, patched) == (0, 1):
        return data, False
    raise PatchError(
        f"expected exactly one original or one patched stack statement in {LIBRARY_FILE.as_posix()}, "
        f"found original={original} patched={patched}. The pinned library probably changed; "
        "re-evaluate docs/DECISIONS.md D034 before building.")


def patch_library(libdeps_directory):
    path = Path(libdeps_directory) / LIBRARY_FILE
    if not path.is_file():
        raise PatchError(f"{LIBRARY_FILE.as_posix()} was not found; install the pinned library dependencies first.")
    data, changed = patch_speaker_stack(path.read_bytes())
    if changed:
        path.write_bytes(data)
    return changed


Import("env")
try:
    changed = patch_library(Path(env["PROJECT_LIBDEPS_DIR"]) / env["PIOENV"])
    print("M5Unified speaker stack: " + ("patched" if changed else "already patched"))
except PatchError as error:
    sys.stderr.write(f"M5Unified speaker stack patch failed: {error}\n")
    env.Exit(1)
