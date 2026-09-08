#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
#
# Part of SaberStage.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# File responsibility:
# - Validates a staged or packaged qmod against its manifest and required files.
# - It rejects missing, unexpected, or incorrectly placed runtime assets.

"""Validate the generated SaberStage QMOD without installing it."""

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
FFMPEG_LIBRARY_DIR = ROOT / ".cache/dependencies/ffmpeg-hardware/lib"
FFMPEG_LIBRARIES = {
    "libavformat-saberstage9.so",
    "libavcodec-saberstage9.so",
    "libavutil-saberstage9.so",
}
TTS_LIBRARY_DIR = ROOT / ".cache/dependencies/kitten-tts/lib"
TTS_LIBRARIES = {"libsabstageort.so", "libss-tts-neural-api.so"}
RUNTIME_LIBRARIES = FFMPEG_LIBRARIES | TTS_LIBRARIES

# The filename contains a hyphen, so load the canonical ELF verifier by path.
import importlib.util

_VERIFY_SPEC = importlib.util.spec_from_file_location(
    "saberstage_verify_native_library", ROOT / "scripts/verify-native-library.py"
)
if _VERIFY_SPEC is None or _VERIFY_SPEC.loader is None:
    raise RuntimeError("could not load the native library verifier")
_VERIFY_MODULE = importlib.util.module_from_spec(_VERIFY_SPEC)
_VERIFY_SPEC.loader.exec_module(_VERIFY_MODULE)


def fail(message: str) -> None:
    raise RuntimeError(message)


def main() -> int:
    if not ARCHIVE.is_file() or not BUILT_LIBRARY.is_file():
        fail("QMOD or built SaberStage library is missing")

    with zipfile.ZipFile(ARCHIVE) as qmod:
        names = set(qmod.namelist())
        expected_names = {"mod.json", "libsaberstage.so"} | RUNTIME_LIBRARIES
        if names != expected_names:
            fail(f"unexpected QMOD entries: {sorted(names)}")
        manifest = json.loads(qmod.read("mod.json"))
        packaged_library = qmod.read("libsaberstage.so")
        packaged_runtimes = {name: qmod.read(name) for name in RUNTIME_LIBRARIES}

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
    if manifest.get("modFiles") != ["libsaberstage.so"]:
        fail("SaberStage must package exactly one early-mod library")
    if set(manifest.get("libraryFiles", [])) != RUNTIME_LIBRARIES:
        fail("private FFmpeg/KittenTTS runtime library list is missing or unexpected")
    if any(manifest.get(key) for key in ("lateModFiles", "fileCopies", "copyExtensions")):
        fail("QMOD contains an unexpected payload category")

    dependency_ids = {item.get("id") for item in manifest.get("dependencies", [])}
    required = {"beatsaber-hook", "bsml", "custom-types", "hollywood", "songcore"}
    if not required <= dependency_ids:
        fail(f"QMOD dependencies are missing: {sorted(required - dependency_ids)}")
    songcore = next(item for item in manifest["dependencies"] if item.get("id") == "songcore")
    if songcore.get("version") != "=1.1.26":
        fail("SongCore must remain pinned to 1.1.26 for the 1.40.8 request navigation bindings")
    if "paper2_scotland2" in dependency_ids:
        fail("SaberStage must not declare Paper2 as a QMOD dependency")

    built_library = BUILT_LIBRARY.read_bytes()
    if packaged_library != built_library:
        fail("packaged native library does not byte-match build/libsaberstage.so")
    if packaged_library[:4] != b"\x7fELF" or packaged_library[4] != 2 or packaged_library[5] != 1:
        fail("packaged library is not a little-endian ELF64 binary")
    if struct.unpack_from("<H", packaged_library, 18)[0] != 183:
        fail("packaged ELF machine is not AArch64")
    if b"setup\x00" not in packaged_library or b"late_load\x00" not in packaged_library:
        fail("packaged library is missing Scotland2 entry-point names")
    _VERIFY_MODULE.verify(BUILT_LIBRARY)
    for name, packaged in packaged_runtimes.items():
        source = FFMPEG_LIBRARY_DIR if name in FFMPEG_LIBRARIES else TTS_LIBRARY_DIR
        built = (source / name).read_bytes()
        if packaged != built:
            fail(f"packaged {name} does not byte-match the staged private runtime")
        if packaged[:4] != b"\x7fELF" or packaged[4] != 2 or packaged[5] != 1:
            fail(f"packaged {name} is not a little-endian ELF64 binary")
        if struct.unpack_from("<H", packaged, 18)[0] != 183:
            fail(f"packaged {name} is not AArch64")

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
