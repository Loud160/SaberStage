#!/usr/bin/env python3
"""Validate the generated Prompt 2 QMOD without installing it."""

from __future__ import annotations

import hashlib
import json
import pathlib
import struct
import sys
import zipfile


ROOT = pathlib.Path(__file__).resolve().parent.parent
ARCHIVE = ROOT / "SaberStage.qmod"
BUILT_LIBRARY = ROOT / "build/libsaberstage.so"


def fail(message: str) -> None:
    raise RuntimeError(message)


def main() -> int:
    if not ARCHIVE.is_file() or not BUILT_LIBRARY.is_file():
        fail("QMOD or built SaberStage library is missing")

    with zipfile.ZipFile(ARCHIVE) as qmod:
        names = set(qmod.namelist())
        if names != {"mod.json", "libsaberstage.so"}:
            fail(f"unexpected QMOD entries: {sorted(names)}")
        manifest = json.loads(qmod.read("mod.json"))
        packaged_library = qmod.read("libsaberstage.so")

    expected_identity = {
        "name": "SaberStage",
        "id": "saberstage",
        "author": "Loud160 (AKA Whisp)",
        "version": "0.1.0",
        "packageId": "com.beatgames.beatsaber",
        "packageVersion": "1.40.8_7379",
        "modloader": "Scotland2",
    }
    for key, expected in expected_identity.items():
        if manifest.get(key) != expected:
            fail(f"manifest {key} is {manifest.get(key)!r}; expected {expected!r}")
    if manifest.get("lateModFiles") != ["libsaberstage.so"]:
        fail("SaberStage must package exactly one late-mod library")
    if any(manifest.get(key) for key in ("modFiles", "libraryFiles", "fileCopies", "copyExtensions")):
        fail("Prompt 2 QMOD contains an unexpected payload category")

    dependency_ids = {item.get("id") for item in manifest.get("dependencies", [])}
    required = {"beatsaber-hook", "bsml", "custom-types", "hollywood", "paper2_scotland2"}
    if not required <= dependency_ids:
        fail(f"QMOD dependencies are missing: {sorted(required - dependency_ids)}")

    built_library = BUILT_LIBRARY.read_bytes()
    if packaged_library != built_library:
        fail("packaged native library does not byte-match build/libsaberstage.so")
    if packaged_library[:4] != b"\x7fELF" or packaged_library[4] != 2 or packaged_library[5] != 1:
        fail("packaged library is not a little-endian ELF64 binary")
    if struct.unpack_from("<H", packaged_library, 18)[0] != 183:
        fail("packaged ELF machine is not AArch64")
    if b"setup\x00" not in packaged_library or b"late_load\x00" not in packaged_library:
        fail("packaged library is missing Scotland2 entry-point names")

    print(f"Verified QMOD: {ARCHIVE}")
    print(f"Library SHA-256: {hashlib.sha256(packaged_library).hexdigest()}")
    print(f"QMOD SHA-256: {hashlib.sha256(ARCHIVE.read_bytes()).hexdigest()}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, OSError, ValueError, zipfile.BadZipFile) as exc:
        print(f"PACKAGE VERIFICATION FAILED: {exc}", file=sys.stderr)
        raise SystemExit(1)
