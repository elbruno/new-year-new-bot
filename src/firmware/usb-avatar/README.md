# USB-avatar firmware source

This is the complete PlatformIO firmware project for the **0.5.1 draft source
snapshot**. It is source code, not a coordinated product release or a claim
that these exact committed files produced an installed firmware image.

## Build

Install Python 3 and use the pinned versions in `requirements.txt`:

```powershell
Set-Location .\src\firmware\usb-avatar
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
```

For the Wi-Fi firmware profile, copy
`include\wifi_setup_private.example.h` to
`include\wifi_setup_private.h`, replace the placeholder with a private
eight-character development setup password, then build:

```powershell
python -m platformio run -e cores3-wifi-lab
```

The default `cores3` environment builds the USB-avatar profile. Select
`cores3-lesson-examples` or `cores3-wifi-manual-diagnostic` only when those
specific source profiles are needed. Build commands do not install firmware.

`firmware-version.txt` pins the displayed firmware version to `0.5.1`.
Build-time avatar generation uses the project inputs under
`..\assets\avatar\`; generated headers, images, caches, and PlatformIO output
are ignored and must not be committed.

## Pinned dependencies and attribution

PlatformIO dependencies are pinned in `platformio.ini`, including
Espressif32 8.0.0, M5Unified 0.2.27, M5GFX 0.2.34, StackChan-BSP commit
`fb5e9f5697803ca64e45fe7ad8eaea3e4100f766`, and the listed M5Stack libraries.
Python build tools are pinned in `requirements.txt`. Dependencies are fetched
from their upstream sources and remain subject to their own licenses:
[M5Stack](https://github.com/m5stack),
[StackChan-BSP](https://github.com/m5stack/StackChan-BSP),
[PlatformIO](https://github.com/platformio/platformio-core),
[Pillow](https://github.com/python-pillow/Pillow), and
[esptool](https://github.com/espressif/esptool).

The avatar artwork inputs are project-created provisional assets, not official
M5Stack or StackChan artwork. No separate license has been assigned to this
project's source or artwork.
