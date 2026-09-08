#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
#
# Part of SaberStage.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

"""Prepare pinned KittenTTS model data and private Android inference libraries."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import struct
import sys
import tarfile
import tempfile
import urllib.request
import zipfile


REPOSITORY = Path(__file__).resolve().parents[1]
LOCK_PATH = REPOSITORY / "dependencies" / "kitten-tts.json"
CACHE_ROOT = REPOSITORY / ".cache" / "dependencies" / "kitten-tts"
INCLUDE_PATH = CACHE_ROOT / "include" / "sherpa-onnx" / "c-api" / "c-api.h"
LIBRARY_PATH = CACHE_ROOT / "lib"
MODEL_ARCHIVE_PATH = CACHE_ROOT / "kitten-data.zip"
READY_PATH = CACHE_ROOT / "saberstage-kitten-tts.ready"
MODEL_ROOT = "kitten-nano-en-v0_2-fp16"

# Only the English phonemizer data used by the model is retained. The official
# model archive contains dictionaries for many unrelated languages; shipping
# those would add roughly 17 MB without changing this English-only model.
MODEL_FILES = {
    "LICENSE",
    "README.md",
    "model.fp16.onnx",
    "tokens.txt",
    "voices.bin",
    "espeak-ng-data/en_dict",
    "espeak-ng-data/intonations",
    "espeak-ng-data/phondata",
    "espeak-ng-data/phondata-manifest",
    "espeak-ng-data/phonindex",
    "espeak-ng-data/phontab",
    "espeak-ng-data/lang/gmw/en",
    "espeak-ng-data/lang/gmw/en-GB-scotland",
    "espeak-ng-data/lang/gmw/en-GB-x-rp",
    "espeak-ng-data/lang/gmw/en-US",
    "espeak-ng-data/lang/gmw/en-US-nyc",
}
ANDROID_MEMBERS = {
    "./jniLibs/arm64-v8a/libonnxruntime.so": "libsabstageort.so",
    "./jniLibs/arm64-v8a/libsherpa-onnx-c-api.so": "libss-tts-neural-api.so",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def download(url: str, destination: Path) -> None:
    request = urllib.request.Request(url, headers={"User-Agent": "SaberStage-build"})
    with urllib.request.urlopen(request, timeout=180) as response, destination.open("wb") as output:
        shutil.copyfileobj(response, output)


def verify(path: Path, expected: str, description: str) -> None:
    actual = sha256(path)
    if actual.lower() != expected.lower():
        raise RuntimeError(
            f"{description} SHA-256 mismatch: expected {expected.lower()}, got {actual}")


def read_regular_member(archive: tarfile.TarFile, name: str, maximum: int) -> bytes:
    try:
        member = archive.getmember(name)
    except KeyError as error:
        raise RuntimeError(f"Pinned archive is missing {name}") from error
    normalized = PurePosixPath(member.name)
    if normalized.is_absolute() or ".." in normalized.parts or not member.isfile():
        raise RuntimeError(f"Pinned archive member is unsafe: {name}")
    if member.size < 1 or member.size > maximum:
        raise RuntimeError(f"Pinned archive member has an invalid size: {name}")
    stream = archive.extractfile(member)
    if stream is None:
        raise RuntimeError(f"Pinned archive member could not be read: {name}")
    data = stream.read(maximum + 1)
    if len(data) != member.size or len(data) > maximum:
        raise RuntimeError(f"Pinned archive member did not match its declared size: {name}")
    return data


def patch_equal_length(data: bytes, old: bytes, new: bytes, description: str) -> bytes:
    if len(old) != len(new):
        raise RuntimeError(f"Internal private-library rename is not length preserving: {description}")
    occurrences = data.count(old)
    if occurrences != 1:
        raise RuntimeError(
            f"Expected one {description} ELF dynamic string, found {occurrences}")
    return data.replace(old, new)


def validate_aarch64_elf(data: bytes, description: str) -> None:
    if len(data) < 64 or data[:6] != b"\x7fELF\x02\x01":
        raise RuntimeError(f"{description} is not a little-endian ELF64 library")
    if struct.unpack_from("<H", data, 18)[0] != 183:
        raise RuntimeError(f"{description} is not built for AArch64")


def prepare_runtime(archive_path: Path, lock: dict) -> None:
    LIBRARY_PATH.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive_path, "r:bz2") as archive:
        runtime = read_regular_member(
            archive, "./jniLibs/arm64-v8a/libonnxruntime.so", 64 * 1024 * 1024)
        api = read_regular_member(
            archive, "./jniLibs/arm64-v8a/libsherpa-onnx-c-api.so", 16 * 1024 * 1024)

    validate_aarch64_elf(runtime, "ONNX Runtime")
    validate_aarch64_elf(api, "sherpa-onnx C API")
    # Android's process-wide linker namespace keys libraries by SONAME. Give
    # both runtimes private, length-preserving names so another Quest mod's
    # ONNX Runtime cannot satisfy SaberStage's dependency with an incompatible
    # version (or vice versa).
    runtime = patch_equal_length(
        runtime, b"libonnxruntime.so", b"libsabstageort.so", "ONNX Runtime SONAME")
    api = patch_equal_length(
        api, b"libonnxruntime.so", b"libsabstageort.so", "sherpa ONNX dependency")
    api = patch_equal_length(
        api, b"libsherpa-onnx-c-api.so", b"libss-tts-neural-api.so", "sherpa C API SONAME")

    runtime_path = LIBRARY_PATH / ANDROID_MEMBERS["./jniLibs/arm64-v8a/libonnxruntime.so"]
    api_path = LIBRARY_PATH / ANDROID_MEMBERS["./jniLibs/arm64-v8a/libsherpa-onnx-c-api.so"]
    runtime_path.write_bytes(runtime)
    api_path.write_bytes(api)
    verify(runtime_path, lock["preparedOnnxRuntimeSha256"], "Prepared ONNX Runtime")
    verify(api_path, lock["preparedSherpaApiSha256"], "Prepared sherpa-onnx C API")


def prepare_model(archive_path: Path, lock: dict) -> None:
    selected: dict[str, bytes] = {}
    with tarfile.open(archive_path, "r:bz2") as archive:
        for relative in MODEL_FILES:
            selected[relative] = read_regular_member(
                archive, f"{MODEL_ROOT}/{relative}", 32 * 1024 * 1024)
    CACHE_ROOT.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(MODEL_ARCHIVE_PATH, "w", zipfile.ZIP_STORED) as output:
        for relative in sorted(selected):
            info = zipfile.ZipInfo(f"{MODEL_ROOT}/{relative}", (1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_STORED
            info.external_attr = 0o100644 << 16
            output.writestr(info, selected[relative])
    verify(MODEL_ARCHIVE_PATH, lock["preparedModelSha256"], "Prepared KittenTTS model")


def outputs_are_ready(lock: dict, identity: str) -> bool:
    if not READY_PATH.is_file() or READY_PATH.read_text(encoding="utf-8") != identity:
        return False
    expected = {
        INCLUDE_PATH: lock["cApiHeaderSha256"],
        MODEL_ARCHIVE_PATH: lock["preparedModelSha256"],
        LIBRARY_PATH / "libsabstageort.so": lock["preparedOnnxRuntimeSha256"],
        LIBRARY_PATH / "libss-tts-neural-api.so": lock["preparedSherpaApiSha256"],
    }
    return all(path.is_file() and sha256(path).lower() == digest.lower()
               for path, digest in expected.items())


def main() -> int:
    lock = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
    identity = json.dumps(lock, sort_keys=True, separators=(",", ":")) + "\n"
    if outputs_are_ready(lock, identity):
        print(f"KittenTTS Nano v0.2 and sherpa-onnx {lock['runtimeVersion']} are ready")
        return 0

    CACHE_ROOT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="saberstage-kitten-tts-") as temporary:
        temporary_root = Path(temporary)
        runtime_archive = temporary_root / "android.tar.bz2"
        model_archive = temporary_root / "model.tar.bz2"
        header = temporary_root / "c-api.h"
        print(f"Downloading pinned sherpa-onnx {lock['runtimeVersion']} Android runtime...")
        download(lock["runtimeArchiveUrl"], runtime_archive)
        verify(runtime_archive, lock["runtimeArchiveSha256"], "sherpa-onnx Android archive")
        print("Downloading pinned KittenTTS Nano v0.2 model...")
        download(lock["modelArchiveUrl"], model_archive)
        verify(model_archive, lock["modelArchiveSha256"], "KittenTTS model archive")
        download(lock["cApiHeaderUrl"], header)
        verify(header, lock["cApiHeaderSha256"], "sherpa-onnx C API header")

        prepare_runtime(runtime_archive, lock)
        prepare_model(model_archive, lock)
        INCLUDE_PATH.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(header, INCLUDE_PATH)

    READY_PATH.write_text(identity, encoding="utf-8", newline="\n")
    print(
        "Prepared KittenTTS Nano v0.2 model and private sherpa-onnx Android runtime "
        f"({MODEL_ARCHIVE_PATH.stat().st_size} model bytes)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:  # Build tooling must return the actionable root cause.
        print(f"KittenTTS preparation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
