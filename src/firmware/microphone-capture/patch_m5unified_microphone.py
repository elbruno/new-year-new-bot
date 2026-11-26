"""Expose checked CoreS3 microphone codec writes for the pinned example.

Goal: Preserve M5Unified's capture flow while reporting codec I2C failures.
Responsibilities: Patch only the pinned M5Unified 0.2.27 microphone callback.
How it works: PlatformIO applies exact idempotent substitutions; any mismatch
or partially patched dependency fails the build.
"""
from pathlib import Path
import sys

HEADER = Path("M5Unified/src/M5Unified.hpp")
SOURCE = Path("M5Unified/src/M5Unified.inl")

REPLACEMENTS = {
    HEADER: [
        (b"    board_t getBoard(void) const { return _board; }",
         b"    board_t getBoard(void) const { return _board; }\n"
         b"    bool netbotMicEnableOk() const { return _netbotMicEnableOk; }\n"
         b"    bool netbotMicDisableOk() const { return _netbotMicDisableOk; }\n"
         b"    void netbotResetMicResults() { _netbotMicEnableOk = _netbotMicDisableOk = true; }"),
        (b"    static bool _microphone_enabled_cb_cores3(void* args, bool enabled);",
         b"    bool _netbotMicEnableOk = true;\n"
         b"    bool _netbotMicDisableOk = true;\n"
         b"    static bool _microphone_enabled_cb_cores3(void* args, bool enabled);"),
    ],
    SOURCE: [
        (b"  static void es7210_write_reg(uint8_t reg, uint8_t value)\n"
         b"  {\n    M5.In_I2C.writeRegister(es7210_i2c_addr, reg, &value, 1, 400000);\n  }",
         b"  static bool es7210_write_reg(uint8_t reg, uint8_t value)\n"
         b"  {\n    return M5.In_I2C.writeRegister(es7210_i2c_addr, reg, &value, 1, 400000);\n  }"),
        (b"    if (cfg.pin_bck == GPIO_NUM_34)\n    {\n"
         b"      es7210_write_reg(0x00, 0xFF); // RESET_CTL\n"
         b"      struct __attribute__((packed)) reg_data_t",
         b"    if (cfg.pin_bck == GPIO_NUM_34)\n    {\n"
         b"      bool reset_ok = es7210_write_reg(0x00, 0xFF); // RESET_CTL\n"
         b"      if (!enabled) { self->_netbotMicDisableOk = reset_ok; return reset_ok; }\n"
         b"      self->_netbotMicEnableOk = reset_ok;\n"
         b"      if (!reset_ok) { return false; }\n"
         b"      struct __attribute__((packed)) reg_data_t"),
        (b"          es7210_write_reg(d.reg, d.value);\n"
         b"        }\n      }\n    }\n#endif\n    return true;\n  }\n\n"
         b"  bool M5Unified::_speaker_enabled_cb_cardputer_adv",
         b"          if (!es7210_write_reg(d.reg, d.value))\n"
         b"          { self->_netbotMicEnableOk = false; return false; }\n"
         b"        }\n      }\n    }\n#endif\n    return true;\n  }\n\n"
         b"  bool M5Unified::_speaker_enabled_cb_cardputer_adv"),
    ],
}


class PatchError(RuntimeError):
    pass


def patch_library(libdeps: Path) -> bool:
    changes = []
    for relative, replacements in REPLACEMENTS.items():
        path = libdeps / relative
        if not path.is_file():
            raise PatchError(f"missing pinned library file: {relative}")
        data = path.read_bytes()
        original_data = data
        states = []
        for original, patched in replacements:
            patched_count = data.count(patched)
            original_count = data.count(original) - patched_count * patched.count(original)
            if (original_count, patched_count) == (1, 0):
                data = data.replace(original, patched, 1)
                states.append("original")
            elif (original_count, patched_count) == (0, 1):
                states.append("patched")
            else:
                raise PatchError(
                    f"unexpected pinned library content in {relative}: "
                    f"original={original_count}, patched={patched_count}"
                )
        changes.append((path, original_data, data, states))

    states = [state for _, _, _, group in changes for state in group]
    if len(set(states)) != 1:
        raise PatchError("partially patched M5Unified microphone library")
    for path, before, after, _ in changes:
        if before != after:
            path.write_bytes(after)
    return states[0] == "original"


Import("env")
try:
    changed = patch_library(Path(env["PROJECT_LIBDEPS_DIR"]) / env["PIOENV"])
    print("M5Unified microphone I2C checks: " + ("patched" if changed else "already patched"))
except PatchError as error:
    sys.stderr.write(f"M5Unified microphone patch failed: {error}\n")
    env.Exit(1)
