"""Use Espressif's pip distribution with the same Python environment as PlatformIO."""

import os
from pathlib import Path
import sys

Import("env")
tool = Path(sys.executable).parent / ("esptool.exe" if os.name == "nt" else "esptool")
if not tool.is_file():
    raise RuntimeError("Install src/firmware/usb-avatar/requirements.txt in the PlatformIO Python environment.")
env.Replace(OBJCOPY=str(tool), UPLOADER=str(tool))
