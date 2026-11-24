"""Bind the reviewed camera archive to borrowed SCCB and the existing AW9523 guard."""
from hashlib import sha256
from pathlib import Path
import runpy

CAMERA_SHA = "2e8030798435a26fdad7f2c472e0db2578f416a37622405f617d636c47323f7b"
SCCB = ("Init", "Use_Port", "Deinit", "Probe", "Install_Device", "Read", "Write",
        "Read16", "Write16", "Read_Addr16_Val16", "Write_Addr16_Val16")
ANCHOR = b"  struct _core_s3_lock_t\n"
PATCH = b"""  extern "C" bool netbot_camera_power_lock()
  {
    auto mutex = _core_s3_mutex();
    return mutex && xSemaphoreTake(mutex, 0) == pdTRUE;
  }
  extern "C" bool netbot_camera_power_unlock()
  {
    auto mutex = _core_s3_mutex();
    return mutex && xSemaphoreGive(mutex) == pdTRUE;
  }

"""
SPI_BEGIN = b"    uint32_t freq_apb = getSpiClockFrequency(_cfg.spi_host);\n"
SPI_BEGIN_PATCH = b"""    // CoreS3 shares LCD D/C with SD MISO; change direction under the existing SPI lock.
    if (_cfg.use_lock && _cfg.pin_dc == 35 && _cfg.pin_miso == 35)
      pinMode(35, pin_mode_t::output);
""" + SPI_BEGIN
SPI_END = b"    if (_cfg.use_lock) spi::endTransaction(_cfg.spi_host);\n  }\n\n  void Bus_SPI::wait(void)\n"
SPI_END_PATCH = b"""    if (_cfg.use_lock && _cfg.pin_dc == 35 && _cfg.pin_miso == 35)
      pinMode(35, pin_mode_t::input);
""" + SPI_END
SD_SINGLE = b"""      if (token == 0x0A) {
        continue;
      } else if (token == 0x0C) {
        return false;
      }
"""
SD_SINGLE_PATCH = b"""      if (token != 0x05) {
        return false;
      }
"""
SD_MULTI = b"""        if (token == 0x0A) {
          sdDeselectCard(pdrv);
          unsigned int writtenBlocks = 0;
          if (card->type != CARD_MMC && sdSelectCard(pdrv)) {
            if (!sdCommand(pdrv, SEND_NUM_WR_BLOCKS, 0, NULL)) {
              char acmdData[4];
              if (sdReadBytes(pdrv, acmdData, 4)) {
                writtenBlocks = acmdData[0] << 24;
                writtenBlocks |= acmdData[1] << 16;
                writtenBlocks |= acmdData[2] << 8;
                writtenBlocks |= acmdData[3];
              }
            }
            sdDeselectCard(pdrv);
          }
          currentBuffer = buffer + (writtenBlocks << 9);
          currentSector = sector + writtenBlocks;
          currentCount = count - writtenBlocks;
          continue;
        } else {
          break;
        }
"""
SD_MULTI_PATCH = b"""        // Do not guess zero written blocks and resend after an unconfirmed query.
        break;
"""


def patch_power(data):
    for newline in (b"\n", b"\r\n"):
        anchor = ANCHOR.replace(b"\n", newline)
        patched = PATCH.replace(b"\n", newline) + anchor
        if data.count(patched) == 1 and data.count(anchor) == 1:
            return data
        if data.count(anchor) == 1 and b"netbot_camera_power_lock" not in data:
            return data.replace(anchor, patched, 1)
    raise RuntimeError("Camera AW9523 guard anchor changed or duplicated")


def patch_shared_spi(data):
    for newline in (b"\n", b"\r\n"):
        result = data
        for original, patched in ((SPI_BEGIN, SPI_BEGIN_PATCH), (SPI_END, SPI_END_PATCH)):
            original = original.replace(b"\n", newline)
            patched = patched.replace(b"\n", newline)
            if result.count(patched) == 1 and result.count(original) == 1:
                continue
            if result.count(original) != 1:
                break
            result = result.replace(original, patched, 1)
        else:
            return result
    raise RuntimeError("Camera shared SPI direction anchors changed or duplicated")


def patch_sd_writes(data):
    for newline in (b"\n", b"\r\n"):
        result = data
        for original, patched in ((SD_SINGLE, SD_SINGLE_PATCH), (SD_MULTI, SD_MULTI_PATCH)):
            original = original.replace(b"\n", newline)
            patched = patched.replace(b"\n", newline)
            if result.count(patched) == 1 and result.count(original) == 0: continue
            if result.count(original) != 1: break
            result = result.replace(original, patched, 1)
        else: return result
    raise RuntimeError("Camera SD write-response/replay anchors changed or duplicated")


def configure(env):
    sdk = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32-esp32s3-libs"))
    archive = sdk / "lib" / "libespressif__esp32-camera.a"
    if sha256(archive.read_bytes()).hexdigest() != CAMERA_SHA:
        raise RuntimeError("Camera archive changed; SCCB ABI/encoder must be reviewed before building")
    base = Path(env["PROJECT_LIBDEPS_DIR"]) / env["PIOENV"]
    power = base / "M5Unified" / "src" / "utility" / "Power_Class.inl"
    before = power.read_bytes()
    after = patch_power(before)
    root = Path(env["PROJECT_DIR"]).parent
    selected = runpy.run_path(str(root / "scripts" / "firmware" / "usb-avatar" / "patch_nfc_bus.py"))["selected_library"](base)
    spi = selected / "src" / "lgfx" / "v1" / "platforms" / "esp32" / "Bus_SPI.inl"
    spi_before = spi.read_bytes()
    spi_after = patch_shared_spi(spi_before)
    core = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32"))
    sd = core / "libraries" / "SD" / "src" / "sd_diskio.cpp"
    sd_before = sd.read_bytes()
    sd_after = patch_sd_writes(sd_before)
    if after != before: power.write_bytes(after)
    if spi_after != spi_before: spi.write_bytes(spi_after)
    if sd_after != sd_before: sd.write_bytes(sd_after)
    env.Append(LINKFLAGS=[f"-Wl,--wrap=SCCB_{name}" for name in SCCB])


if "Import" in globals():
    Import("env")
    configure(env)
