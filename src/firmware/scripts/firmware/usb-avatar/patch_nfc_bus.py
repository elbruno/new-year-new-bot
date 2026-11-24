"""Expose a nonblocking START through the existing M5GFX internal-bus driver."""

import json
from pathlib import Path

M5GFX_VERSION = "0.2.34"


def selected_library(root):
    """Select only the unique prepared copy of the configured M5GFX pin."""
    def unique_fields(pairs):
        fields = dict(pairs)
        if len(fields) != len(pairs):
            raise RuntimeError("M5GFX selection: duplicated metadata fields")
        return fields

    matches = []
    for path in Path(root).iterdir():
        if path.name != "M5GFX" and not path.name.startswith("M5GFX@"):
            continue
        if path.is_symlink() or getattr(path.lstat(), "st_file_attributes", 0) & 0x400:
            raise RuntimeError("M5GFX selection: linked library")
        metadata = path / "library.json"
        if metadata.is_symlink() or getattr(metadata.lstat(), "st_file_attributes", 0) & 0x400:
            raise RuntimeError("M5GFX selection: linked metadata")
        value = json.loads(metadata.read_bytes(), object_pairs_hook=unique_fields)
        if not isinstance(value, dict) or value.get("name") != "M5GFX":
            raise RuntimeError("M5GFX selection: incorrect library metadata")
        if path.name == f"M5GFX@{M5GFX_VERSION}" and value.get("version") != M5GFX_VERSION:
            raise RuntimeError("M5GFX selection: pinned directory/version mismatch")
        if value.get("version") == M5GFX_VERSION:
            if path.name not in ("M5GFX", f"M5GFX@{M5GFX_VERSION}"):
                raise RuntimeError("M5GFX selection: pinned metadata/directory mismatch")
            matches.append(path)
    if len(matches) != 1:
        raise RuntimeError("M5GFX selection: expected one prepared pinned 0.2.34 library")
    return matches[0]

DECLARATION = b"    cpp::result<void, error_t> beginTransaction(int i2c_port, int i2c_addr, uint32_t freq, bool read = false);"
DECLARATION_PATCHED = DECLARATION + b"\n    cpp::result<void, error_t> beginTransaction(int i2c_port, int i2c_addr, uint32_t freq, bool read, bool try_lock, bool& cleanup_unknown);"
RMW_DECLARATION = b"    cpp::result<void, error_t> writeRegister8(int i2c_port, int addr, uint8_t reg, uint8_t data, uint8_t mask = 0, uint32_t freq = I2C_DEFAULT_FREQ);"
RMW_DECLARATION_PATCHED = RMW_DECLARATION + b"""
    struct try_rmw_result { bool ok; bool cleanup_unknown; bool write_possible; };
    try_rmw_result tryWriteRegister8(int i2c_port, int addr, uint8_t reg, uint8_t set_bits, uint8_t keep_mask, uint32_t freq);
"""
FUNCTION = b"""    cpp::result<void, error_t> beginTransaction(int i2c_port, int i2c_addr, uint32_t freq, bool read)
    {
      if (isSoftPort(i2c_port))"""
FUNCTION_PATCHED = b"""    cpp::result<void, error_t> beginTransaction(int i2c_port, int i2c_addr, uint32_t freq, bool read)
    {
      bool cleanup_unknown = false;
      return beginTransaction(i2c_port, i2c_addr, freq, read, false, cleanup_unknown);
    }

    cpp::result<void, error_t> beginTransaction(int i2c_port, int i2c_addr, uint32_t freq, bool read, bool try_lock, bool& cleanup_unknown)
    {
      cleanup_unknown = false;
      if (try_lock && isSoftPort(i2c_port)) { return cpp::fail(error_t::invalid_arg); }
      if (isSoftPort(i2c_port))"""
LOCK = b"""      auto dev = getDev(i2c_port);
      i2c_context[i2c_port].lock();
      // Recursive locking supports RMW"""
LOCK_PATCHED = b"""      auto dev = getDev(i2c_port);
      if (try_lock)
      {
        auto mutex = i2c_context[i2c_port].mtx;
        if (!mutex || xSemaphoreTakeRecursive(mutex, 0) != pdTRUE)
        { return cpp::fail(error_t::mode_mismatch); }
      }
      else { i2c_context[i2c_port].lock(); }
      // Recursive locking supports RMW"""
TAIL = b"""      return restart(i2c_port, i2c_addr, freq, read);
    }

    cpp::result<void, error_t> endTransaction(int i2c_port)"""
TAIL_PATCHED = b"""      auto result = restart(i2c_port, i2c_addr, freq, read);
      if (try_lock && result.has_error())
      { cleanup_unknown = endTransaction(i2c_port).has_error(); }
      return result;
    }

    cpp::result<void, error_t> endTransaction(int i2c_port)"""

RMW_ANCHOR = b"    cpp::result<void, error_t> writeRegister8(int i2c_port, int addr, uint8_t reg, uint8_t data, uint8_t mask, uint32_t freq)\n"
RMW_ANCHOR_PATCHED = b"""    try_rmw_result tryWriteRegister8(int port, int addr, uint8_t reg, uint8_t set_bits, uint8_t keep_mask, uint32_t freq)
    {
      try_rmw_result result { false, false, false };
      if (port < 0 || port >= LGFX_I2C_PORT_NUM || isSoftPort(port)) { return result; }
      auto mutex = i2c_context[port].mtx;
      if (!mutex || xSemaphoreTakeRecursive(mutex, 0) != pdTRUE) { return result; }
      auto task = xTaskGetCurrentTaskHandle();
      if (i2c_context[port].owner.load() != nullptr)
      {
        result.cleanup_unknown = xSemaphoreGiveRecursive(mutex) != pdTRUE;
        return result;
      }
      auto finish = [&](bool ok) {
        if (i2c_context[port].owner.load() == task)
        {
          if (endTransaction(port).has_error()) { result.cleanup_unknown = true; ok = false; }
        }
        else if (!ok) { result.cleanup_unknown = true; }
        return ok;
      };
      bool unknown = false;
      uint8_t observed = 0;
      bool ok = beginTransaction(port, addr, freq, false, true, unknown).has_value();
      result.cleanup_unknown |= unknown;
      if (ok)
      {
        ok = writeBytes(port, &reg, 1).has_value();
        if (ok) { ok = restart(port, addr, freq, true).has_value(); }
        if (ok) { ok = readBytes(port, &observed, 1, true).has_value(); }
        ok = finish(ok);
      }
      if (ok)
      {
        uint8_t bytes[] = { reg, static_cast<uint8_t>((observed & keep_mask) | set_bits) };
        unknown = false;
        ok = beginTransaction(port, addr, freq, false, true, unknown).has_value();
        result.cleanup_unknown |= unknown;
        if (ok)
        {
          result.write_possible = true;
          ok = writeBytes(port, bytes, 2).has_value();
          ok = finish(ok);
        }
      }
      if (xSemaphoreGiveRecursive(mutex) != pdTRUE) { result.cleanup_unknown = true; ok = false; }
      result.ok = ok && !result.cleanup_unknown;
      return result;
    }

""" + RMW_ANCHOR


def patch_bytes(data, replacements):
    for original, patched in replacements:
        variants = [(original, patched), (original.replace(b"\n", b"\r\n"), patched.replace(b"\n", b"\r\n"))]
        variants = list(dict.fromkeys(variants))
        applied = [(before, after) for before, after in variants if data.count(after)]
        if len(applied) == 1 and data.count(applied[0][1]) == 1:
            before, after = applied[0]
            if data.count(before) != after.count(before):
                raise RuntimeError("NFC bus patch: mixed original and patched source")
            continue
        if b"\n" not in original:
            position = data.find(original) + len(original)
            newline = b"\r\n" if data[position:position + 2] == b"\r\n" else b"\n"
            variants = [(original, patched.replace(b"\n", newline))]
        matches = [(before, after) for before, after in variants if data.count(before)]
        if len(matches) != 1 or data.count(matches[0][0]) != 1:
            raise RuntimeError("NFC bus patch: pinned source anchor changed or duplicated")
        before, after = matches[0]
        data = data.replace(before, after, 1)
    return data


def patch_library(root):
    base = selected_library(root) / "src/lgfx/v1/platforms"
    changes = [
        (base / "common.hpp", [(DECLARATION, DECLARATION_PATCHED), (RMW_DECLARATION, RMW_DECLARATION_PATCHED)]),
        (base / "esp32/common.inl", [(FUNCTION, FUNCTION_PATCHED), (LOCK, LOCK_PATCHED),
                                     (TAIL, TAIL_PATCHED), (RMW_ANCHOR, RMW_ANCHOR_PATCHED)]),
    ]
    # Validate every anchor before writing either file.
    prepared = [(path, path.read_bytes()) for path, _ in changes]
    patched = [patch_bytes(data, replacements) for (_, data), (_, replacements) in zip(prepared, changes)]
    for (path, data), result in zip(prepared, patched):
        if result != data:
            path.write_bytes(result)


if "Import" in globals():
    Import("env")
    patch_library(Path(env["PROJECT_LIBDEPS_DIR"]) / env["PIOENV"])
