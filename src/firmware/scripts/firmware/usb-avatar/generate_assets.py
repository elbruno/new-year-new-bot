"""PlatformIO pre-build: apply the source snapshot version and export avatar assets."""

from pathlib import Path
import subprocess
import sys

Import("env")
project = Path(env["PROJECT_DIR"])
root = project.parent
version = (project / "firmware-version.txt").read_text(encoding="ascii").strip()
if version != "0.5.1":
    raise ValueError("This source snapshot is pinned to firmware 0.5.1.")
env.Append(CPPDEFINES=[("NETBOT_FIRMWARE_VERSION", '\\"' + version + '\\"')])
subprocess.run([sys.executable, str(root / "scripts/avatar/export_device_assets.py")], check=True)
