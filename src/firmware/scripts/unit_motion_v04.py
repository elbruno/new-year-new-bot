"""Read and bind an optional private reviewed unit-motion record."""

import ctypes
from ctypes import wintypes
from dataclasses import dataclass
from datetime import date
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys


SCHEMA_VERSION = 1
MAX_RECORD_BYTES = 65536
CONFIGURATION_FIELDS = (
    "expectedYawRaw", "expectedPitchRaw", "homeYawRaw", "homePitchRaw",
    "yawMinRaw", "yawMaxRaw", "pitchMinRaw", "pitchMaxRaw",
    "stepRaw", "nodStepRaw", "travelMs",
)
RECORD_FIELDS = {
    "schemaVersion", "reviewReference", "hardwareApplicability", "reviewedOn",
    "configuration",
}
DATE = re.compile(r"[0-9]{4}-[0-9]{2}-[0-9]{2}", re.ASCII)


class MotionRecordError(ValueError):
    pass


@dataclass(frozen=True)
class MotionRecord:
    path: Path
    schema_version: int
    source_sha256: str
    header: bytes
    header_sha256: str
    fingerprint: dict


def _require(condition, message):
    if not condition:
        raise MotionRecordError(message)


def _no_links(path):
    for entry in (*reversed(path.parents), path):
        try:
            info = entry.lstat()
        except FileNotFoundError:
            continue
        _require(not stat.S_ISLNK(info.st_mode) and not (
            getattr(info, "st_file_attributes", 0) & 0x400),
            "Linked or reparse paths are not supported.")


def _local_absolute(path):
    _require(path.is_absolute() and not str(path).startswith("\\\\")
             and path != Path(path.anchor),
             "A private record requires an absolute local path.")
    if os.name == "nt":
        try:
            kernel = ctypes.WinDLL("Kernel32", use_last_error=True)
            kernel.GetDriveTypeW.argtypes = [wintypes.LPCWSTR]
            kernel.GetDriveTypeW.restype = wintypes.UINT
            drive_type = kernel.GetDriveTypeW(str(path.anchor))
        except (AttributeError, OSError):
            raise MotionRecordError("Local storage could not be verified.") from None
        _require(drive_type not in (0, 4), "Network or unknown storage is not supported.")


def _is_below(path, root):
    try:
        return os.path.commonpath((os.path.normcase(str(path)),
                                   os.path.normcase(str(root)))) == os.path.normcase(str(root))
    except ValueError:
        return False


def _sid_text(advapi, kernel, sid):
    value = wintypes.LPWSTR()
    if not advapi.ConvertSidToStringSidW(sid, ctypes.byref(value)):
        raise MotionRecordError("Private ACL could not be verified.")
    try:
        return value.value
    finally:
        kernel.LocalFree(ctypes.cast(value, ctypes.c_void_p))


def _current_user_sid(advapi, kernel):
    class SidAndAttributes(ctypes.Structure):
        _fields_ = [("Sid", ctypes.c_void_p), ("Attributes", wintypes.DWORD)]

    class TokenUser(ctypes.Structure):
        _fields_ = [("User", SidAndAttributes)]

    token = wintypes.HANDLE()
    if not advapi.OpenProcessToken(kernel.GetCurrentProcess(), 0x0008, ctypes.byref(token)):
        raise MotionRecordError("Private ACL could not be verified.")
    try:
        size = wintypes.DWORD()
        advapi.GetTokenInformation(token, 1, None, 0, ctypes.byref(size))
        if not size.value:
            raise MotionRecordError("Private ACL could not be verified.")
        buffer = ctypes.create_string_buffer(size.value)
        if not advapi.GetTokenInformation(token, 1, buffer, size, ctypes.byref(size)):
            raise MotionRecordError("Private ACL could not be verified.")
        user = ctypes.cast(buffer, ctypes.POINTER(TokenUser)).contents
        return _sid_text(advapi, kernel, user.User.Sid)
    finally:
        kernel.CloseHandle(token)


def require_private_acl(path):
    if os.name != "nt":
        raise MotionRecordError("Private ACL could not be verified; record use is blocked.")

    class AceHeader(ctypes.Structure):
        _fields_ = [("AceType", wintypes.BYTE), ("AceFlags", wintypes.BYTE),
                    ("AceSize", wintypes.WORD)]

    class AllowedAce(ctypes.Structure):
        _fields_ = [("Header", AceHeader), ("Mask", wintypes.DWORD),
                    ("SidStart", wintypes.DWORD)]

    class AclSizeInformation(ctypes.Structure):
        _fields_ = [("AceCount", wintypes.DWORD), ("AclBytesInUse", wintypes.DWORD),
                    ("AclBytesFree", wintypes.DWORD)]

    try:
        advapi = ctypes.WinDLL("Advapi32", use_last_error=True)
        kernel = ctypes.WinDLL("Kernel32", use_last_error=True)
        kernel.GetCurrentProcess.restype = wintypes.HANDLE
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        kernel.CloseHandle.restype = wintypes.BOOL
        kernel.LocalFree.argtypes = [ctypes.c_void_p]
        kernel.LocalFree.restype = ctypes.c_void_p
        advapi.OpenProcessToken.argtypes = [
            wintypes.HANDLE, wintypes.DWORD, ctypes.POINTER(wintypes.HANDLE)]
        advapi.OpenProcessToken.restype = wintypes.BOOL
        advapi.GetTokenInformation.argtypes = [
            wintypes.HANDLE, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD,
            ctypes.POINTER(wintypes.DWORD)]
        advapi.GetTokenInformation.restype = wintypes.BOOL
        advapi.ConvertSidToStringSidW.argtypes = [
            ctypes.c_void_p, ctypes.POINTER(wintypes.LPWSTR)]
        advapi.ConvertSidToStringSidW.restype = wintypes.BOOL
        advapi.GetNamedSecurityInfoW.argtypes = [
            wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
            ctypes.POINTER(ctypes.c_void_p), ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_void_p), ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_void_p)]
        advapi.GetNamedSecurityInfoW.restype = wintypes.DWORD
        advapi.GetAclInformation.argtypes = [
            ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD]
        advapi.GetAclInformation.restype = wintypes.BOOL
        advapi.GetAce.argtypes = [
            ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(ctypes.c_void_p)]
        advapi.GetAce.restype = wintypes.BOOL
        user_sid = _current_user_sid(advapi, kernel)
        owner, dacl, descriptor = (ctypes.c_void_p(), ctypes.c_void_p(), ctypes.c_void_p())
        status = advapi.GetNamedSecurityInfoW(
            str(path), 1, 0x00000001 | 0x00000004,
            ctypes.byref(owner), None, ctypes.byref(dacl), None,
            ctypes.byref(descriptor))
        if status or not descriptor.value or not owner.value or not dacl.value:
            raise MotionRecordError("Private ACL could not be verified.")
        try:
            if _sid_text(advapi, kernel, owner) != user_sid:
                raise MotionRecordError("Private ACL owner is not the current user.")
            information = AclSizeInformation()
            if not advapi.GetAclInformation(
                    dacl, ctypes.byref(information), ctypes.sizeof(information), 2):
                raise MotionRecordError("Private ACL could not be verified.")
            allowed = set()
            for index in range(information.AceCount):
                ace_pointer = ctypes.c_void_p()
                if not advapi.GetAce(dacl, index, ctypes.byref(ace_pointer)):
                    raise MotionRecordError("Private ACL could not be verified.")
                ace = ctypes.cast(ace_pointer, ctypes.POINTER(AceHeader)).contents
                if ace.AceType == 0:
                    allowed_ace = ctypes.cast(
                        ace_pointer, ctypes.POINTER(AllowedAce)).contents
                    sid = ctypes.c_void_p(
                        ctypes.addressof(allowed_ace) + AllowedAce.SidStart.offset)
                    allowed.add(_sid_text(advapi, kernel, sid))
                elif ace.AceType != 1:
                    raise MotionRecordError("Private ACL contains an unsupported permission.")
            _require(user_sid in allowed
                     and allowed <= {user_sid, "S-1-5-18", "S-1-5-32-544"},
                     "Private ACL grants access beyond the current user and system administrators.")
        finally:
            kernel.LocalFree(descriptor)
    except MotionRecordError:
        raise
    except Exception:
        raise MotionRecordError("Private ACL could not be verified; record use is blocked.") from None


def require_private_output_root(path, source_root=None):
    path = Path(os.path.abspath(path))
    _local_absolute(path)
    if source_root is not None:
        source_root = Path(os.path.abspath(source_root))
        _require(not _is_below(path, source_root) and not _is_below(source_root, path),
                 "Private output storage must be separate from the source checkout.")
    _no_links(path)
    try:
        info = path.lstat()
    except OSError:
        raise MotionRecordError("Private output storage could not be verified.") from None
    _require(stat.S_ISDIR(info.st_mode), "Private output storage must already exist.")
    require_private_acl(path)


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise MotionRecordError("Duplicate motion record property.")
        result[key] = value
    return result


def _cpp_string(value):
    encoded = value.encode("utf-8", errors="strict")
    _require(b"\0" not in encoded, "Motion record text contains an unsupported character.")
    return '"' + "".join(chr(byte) if 0x20 <= byte <= 0x7E and byte not in (0x22, 0x5C)
                         else f"\\{byte:03o}" for byte in encoded) + '"'


def _header(record):
    config = record["configuration"]
    values = ", ".join(str(config[name]) for name in CONFIGURATION_FIELDS[:-1])
    values += f", {config['travelMs']}u"
    return (
        "#pragma once\n\n"
        "inline constexpr ReviewedUnitMotion kReviewedUnitMotion{\n"
        f"    {_cpp_string(record['reviewReference'])},\n"
        f"    {_cpp_string(record['hardwareApplicability'])},\n"
        f"    {_cpp_string(record['reviewedOn'])},\n"
        f"    MotionConfiguration{{{values}}}\n"
        "};\n"
        "static_assert(configurationError(kReviewedUnitMotion.configuration, "
        "kCheckedMotionBounds) == nullptr, "
        "\"Reviewed unit motion configuration is invalid.\");\n"
    ).encode("ascii")


def absent_fingerprint():
    value = {"present": False, "schemaVersion": None,
             "sourceSha256": None, "headerSha256": None}
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode("ascii")
    return {**value, "fingerprintSha256": hashlib.sha256(encoded).hexdigest()}


def read_record(path, source_root, *, verify_acl=True):
    try:
        path = Path(path)
        _local_absolute(path)
        path = Path(os.path.abspath(path))
        source_root = Path(os.path.abspath(source_root))
        _require(not _is_below(path, source_root),
                 "Motion record must be outside the source checkout.")
        _no_links(path)
        if verify_acl:
            require_private_acl(path)
        before_path = path.lstat()
        _require(stat.S_ISREG(before_path.st_mode)
                 and not (getattr(before_path, "st_file_attributes", 0) & 0x400)
                 and before_path.st_nlink == 1
                 and 0 < before_path.st_size <= MAX_RECORD_BYTES,
                 "Motion record must be a nonempty regular file within the size limit.")
        with path.open("rb") as stream:
            before = os.fstat(stream.fileno())
            _require((before.st_dev, before.st_ino, before.st_size) ==
                     (before_path.st_dev, before_path.st_ino, before_path.st_size),
                     "Motion record identity changed while opening.")
            raw = stream.read(MAX_RECORD_BYTES + 1)
            after = os.fstat(stream.fileno())
        after_path = path.lstat()
        identity = lambda info: (
            info.st_dev, info.st_ino, info.st_nlink, info.st_size, info.st_mtime_ns)
        _require(len(raw) <= MAX_RECORD_BYTES and identity(before) == identity(after)
                 and identity(after) == identity(after_path),
                 "Motion record changed while reading.")
        text = raw.decode("utf-8", errors="strict")
        record = json.loads(text, object_pairs_hook=_unique_object)
        _require(isinstance(record, dict) and set(record) == RECORD_FIELDS,
                 "Motion record schema is not exact.")
        _require(type(record["schemaVersion"]) is int
                 and record["schemaVersion"] == SCHEMA_VERSION,
                 "Unsupported motion record schema version.")
        for name in ("reviewReference", "hardwareApplicability"):
            value = record[name]
            _require(isinstance(value, str) and value.strip() and "\0" not in value,
                     f"Motion record {name} must be nonempty text.")
        reviewed_on = record["reviewedOn"]
        _require(isinstance(reviewed_on, str) and DATE.fullmatch(reviewed_on) is not None,
                 "Motion record reviewedOn must be a valid YYYY-MM-DD date.")
        _require(date.fromisoformat(reviewed_on).isoformat() == reviewed_on,
                 "Motion record reviewedOn must be a valid YYYY-MM-DD date.")
        config = record["configuration"]
        _require(isinstance(config, dict) and set(config) == set(CONFIGURATION_FIELDS),
                 "Motion record configuration fields are not exact.")
        for name in CONFIGURATION_FIELDS:
            value = config[name]
            _require(type(value) is int, f"Motion record {name} must be an integer.")
            if name == "travelMs":
                _require(0 <= value <= 0xFFFFFFFF,
                         "Motion record travelMs is outside the supported integer range.")
            else:
                _require(-0x80000000 <= value <= 0x7FFFFFFF,
                         f"Motion record {name} is outside the supported integer range.")
        header = _header(record)
        source_sha256 = hashlib.sha256(raw).hexdigest()
        header_sha256 = hashlib.sha256(header).hexdigest()
        fingerprint = {
            "present": True,
            "schemaVersion": SCHEMA_VERSION,
            "sourceSha256": source_sha256,
            "headerSha256": header_sha256,
        }
        encoded = json.dumps(fingerprint, sort_keys=True, separators=(",", ":")).encode("ascii")
        fingerprint["fingerprintSha256"] = hashlib.sha256(encoded).hexdigest()
        return MotionRecord(path, SCHEMA_VERSION, source_sha256, header,
                            header_sha256, fingerprint)
    except MotionRecordError:
        raise
    except Exception:
        raise MotionRecordError("Private motion record could not be read or validated.") from None


def validate_offline(path, source_root):
    record = read_record(path, source_root)
    compiler = shutil.which("g++")
    _require(compiler is not None, "A C++17 g++ compiler is required for offline validation.")
    include = Path(source_root) / "src" / "firmware" / "usb-avatar" / "include"
    probe = (b'#include "LabCheckedMotionConfig.h"\n'
             b'namespace lab { namespace offline_check {\n' + record.header +
             b'\n} }\n')
    try:
        result = subprocess.run(
            [compiler, "-std=c++17", "-pipe", "-fsyntax-only", "-x", "c++",
             "-I", str(include), "-"],
            input=probe, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            check=False)
    except OSError:
        raise MotionRecordError("Offline C++ validation could not complete.") from None
    _require(result.returncode == 0,
             "Offline C++ validation failed; the record or validation source was rejected.")
    _require(read_record(path, source_root).fingerprint == record.fingerprint,
             "Motion record changed during offline validation.")
    return record.fingerprint


def main():
    try:
        _require(len(sys.argv) == 1, "Supply the private record path on standard input, not in arguments.")
        _require(not sys.stdin.isatty(), "Supply the private record path on standard input.")
        path = sys.stdin.readline(32769)
        _require(bool(path) and len(path) <= 32768,
                 "A private record path is required on standard input.")
        validate_offline(path.rstrip("\r\n"), Path(__file__).resolve().parents[1])
    except MotionRecordError as error:
        print(str(error), file=sys.stderr)
        return 1
    except (OSError, UnicodeError):
        print("Private record path could not be read from standard input.", file=sys.stderr)
        return 1
    print("Schema and C++ configuration invariants passed; physical review is not verified.")
    return 0


def write_generated_header(record, build_directory):
    try:
        build_directory = Path(os.path.abspath(build_directory))
        _no_links(build_directory)
        build_directory.mkdir(parents=True, exist_ok=True)
        require_private_acl(build_directory)
        header_path = build_directory / "unit_motion_record.h"
        _no_links(header_path)
        if header_path.exists():
            header_info = header_path.lstat()
            _require(stat.S_ISREG(header_info.st_mode) and header_info.st_nlink == 1
                     and not (getattr(header_info, "st_file_attributes", 0) & 0x400)
                     and header_path.read_bytes() == record.header,
                     "Generated motion header drifted within this build.")
        else:
            with header_path.open("xb") as stream:
                _require(stream.write(record.header) == len(record.header),
                         "Generated motion header write was incomplete.")
                stream.flush()
                os.fsync(stream.fileno())
                _require(os.fstat(stream.fileno()).st_nlink == 1,
                         "Generated motion header has an unsupported link.")
        final_info = header_path.lstat()
        _require(stat.S_ISREG(final_info.st_mode) and final_info.st_nlink == 1
                 and not (getattr(final_info, "st_file_attributes", 0) & 0x400)
                 and hashlib.sha256(header_path.read_bytes()).hexdigest()
                 == record.header_sha256,
                 "Generated motion header failed its integrity check.")
        return header_path
    except MotionRecordError:
        raise
    except Exception:
        raise MotionRecordError("Private generated motion header could not be verified.") from None


def write_private_locator(path, record_path):
    try:
        path = Path(path)
        _no_links(path)
        content = str(record_path).encode("utf-8", errors="strict")
        with path.open("xb") as stream:
            _require(stream.write(content) == len(content),
                     "Private motion record locator write was incomplete.")
            stream.flush()
            os.fsync(stream.fileno())
            _require(os.fstat(stream.fileno()).st_nlink == 1,
                     "Private motion record locator has an unsupported link.")
        _require(path.lstat().st_nlink == 1,
                 "Private motion record locator has an unsupported link.")
        return hashlib.sha256(content).hexdigest()
    except MotionRecordError:
        raise
    except Exception:
        raise MotionRecordError("Private motion record locator could not be saved.") from None


if __name__ == "__main__":
    sys.exit(main())
