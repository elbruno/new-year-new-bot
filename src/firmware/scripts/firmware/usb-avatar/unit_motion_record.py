"""PlatformIO pre-build hook for the explicit private unit-motion record."""

from pathlib import Path
import os
import sys

Import("env")

record_path = os.environ.get("NETBOT_UNIT_MOTION_RECORD_PATH")
if record_path:
    project = Path(env["PROJECT_DIR"])
    root = project.parent
    sys.path.insert(0, str(root / "scripts"))
    import unit_motion_v04

    try:
        record = unit_motion_v04.read_record(record_path, root)
        build_root = Path(os.environ["PLATFORMIO_BUILD_DIR"])
        build_directory = Path(env.subst("$BUILD_DIR"))
        build_root = Path(os.path.abspath(build_root))
        build_directory = Path(os.path.abspath(build_directory))
        if (not build_root.is_absolute()
                or os.path.commonpath((os.path.normcase(str(build_root)),
                                       os.path.normcase(str(build_directory))))
                != os.path.normcase(str(build_root))):
            raise ValueError("Invalid generated-header destination.")
        unit_motion_v04.write_generated_header(record, build_directory)
        env.Append(CPPPATH=[str(build_directory)])
        env.Append(CPPDEFINES=["NETBOT_UNIT_MOTION_RECORD_PRESENT"])
    except Exception:
        raise RuntimeError("Private unit motion record was refused.") from None
