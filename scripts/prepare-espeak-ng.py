#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
#
# Part of SaberStage.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

"""Prepare the pinned eSpeak NG source and a deterministic English voice-data archive."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import posixpath
import shutil
import stat
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

REPOSITORY = Path(__file__).resolve().parents[1]
LOCK_PATH = REPOSITORY / "dependencies" / "espeak-ng.json"
CACHE_ROOT = REPOSITORY / ".cache" / "dependencies" / "espeak-ng"
SOURCE_PATH = CACHE_ROOT / "source"
NATIVE_BUILD_PATH = CACHE_ROOT / "native-build"
DATA_ARCHIVE_PATH = CACHE_ROOT / "english-data.zip"
READY_PATH = CACHE_ROOT / "saberstage-espeak-ng.ready"
MAXIMUM_ARCHIVE_FILES = 8_000
MAXIMUM_UNCOMPRESSED_BYTES = 160 * 1024 * 1024


def remove_tree(path: Path) -> None:
    """Remove a generated tree, including read-only files created by Git.

    eSpeak's CMake configure can populate native-build/_deps with Git object
    files marked read-only on Windows.  A normal shutil.rmtree then fails on a
    later deterministic rebuild even though the directory is only a cache.
    Clear that attribute only after deletion fails and retry the exact failed
    operation; source-controlled paths are never passed to this helper.
    """

    def make_writable_and_retry(function, failed_path, _exception_info) -> None:
        Path(failed_path).chmod(stat.S_IREAD | stat.S_IWRITE | stat.S_IEXEC)
        function(failed_path)

    shutil.rmtree(path, onerror=make_writable_and_retry)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def safe_extract(archive: Path, destination: Path) -> Path:
    total = 0
    roots: set[str] = set()
    with zipfile.ZipFile(archive) as source:
        entries = source.infolist()
        if len(entries) > MAXIMUM_ARCHIVE_FILES:
            raise RuntimeError("eSpeak NG archive contains too many entries")
        symbolic_links: list[tuple[zipfile.ZipInfo, str]] = []
        for entry in entries:
            name = PurePosixPath(entry.filename)
            if name.is_absolute() or ".." in name.parts:
                raise RuntimeError(f"Unsafe eSpeak NG archive path: {entry.filename}")
            if entry.external_attr >> 28 == 0xA:
                symbolic_links.append((entry, source.read(entry).decode("utf-8")))
            total += entry.file_size
            if total > MAXIMUM_UNCOMPRESSED_BYTES:
                raise RuntimeError("eSpeak NG archive exceeds the extraction size limit")
            if name.parts:
                roots.add(name.parts[0])
        if len(roots) != 1:
            raise RuntimeError("eSpeak NG archive has an unexpected directory layout")
        for entry in entries:
            if entry.external_attr >> 28 != 0xA:
                source.extract(entry, destination)
        # GitHub preserves a few source-tree symlinks. Recreate each one as a
        # regular copy so preparation is deterministic on Windows and never
        # asks the host for symlink privileges.
        for entry, link_target in symbolic_links:
            link_name = PurePosixPath(entry.filename)
            resolved = PurePosixPath(posixpath.normpath(str(link_name.parent / link_target)))
            if resolved.is_absolute() or ".." in resolved.parts or not resolved.parts:
                raise RuntimeError(f"Unsafe eSpeak NG symbolic-link target: {entry.filename}")
            source_path = destination.joinpath(*resolved.parts)
            target_path = destination.joinpath(*link_name.parts)
            if not source_path.is_file():
                raise RuntimeError(f"Missing eSpeak NG symbolic-link target: {link_target}")
            target_path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source_path, target_path)
    return destination / next(iter(roots))


def to_wsl_path(path: Path) -> str:
    windows = PureWindowsPath(path.resolve())
    if not windows.drive or not windows.root:
        raise RuntimeError(f"Expected an absolute Windows path, got {path}")
    drive = windows.drive.rstrip(":").lower()
    return "/mnt/" + drive + "/" + "/".join(windows.parts[1:])


def build_voice_data() -> Path:
    configure = [
        "cmake", "-S", str(SOURCE_PATH), "-B", str(NATIVE_BUILD_PATH), "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_SHARED_LIBS=OFF",
        "-DUSE_ASYNC=OFF", "-DUSE_LIBPCAUDIO=OFF", "-DUSE_LIBSONIC=OFF",
        "-DUSE_MBROLA=OFF", "-DUSE_SPEECHPLAYER=OFF",
    ]
    build = ["cmake", "--build", str(NATIVE_BUILD_PATH), "--target", "data", "--parallel", "2"]
    if os.name == "nt":
        # eSpeak's generator utilities are POSIX-oriented. Use the repository's
        # established WSL build environment on Windows, while Linux builders
        # run the same CMake targets natively instead of depending on wsl.exe.
        source_wsl = to_wsl_path(SOURCE_PATH)
        build_wsl = to_wsl_path(NATIVE_BUILD_PATH)
        command = (
            f"cmake -S '{source_wsl}' -B '{build_wsl}' -G Ninja "
            "-DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF "
            "-DUSE_ASYNC=OFF -DUSE_LIBPCAUDIO=OFF -DUSE_LIBSONIC=OFF "
            "-DUSE_MBROLA=OFF -DUSE_SPEECHPLAYER=OFF && "
            f"cmake --build '{build_wsl}' --target data --parallel 2"
        )
        subprocess.run(["wsl.exe", "-d", "Ubuntu", "-e", "bash", "-lc", command], check=True)
    else:
        subprocess.run(configure, check=True)
        subprocess.run(build, check=True)
    data = NATIVE_BUILD_PATH / "espeak-ng-data"
    if not (data / "en_dict").is_file():
        raise RuntimeError("eSpeak NG did not generate the English dictionary")
    return data


def write_english_archive(data: Path) -> None:
    required_files = ["intonations", "phondata", "phondata-manifest", "phonindex", "phontab", "en_dict"]
    selected = [data / name for name in required_files]
    selected.extend(path for path in (data / "lang").rglob("*") if path.is_file())
    selected.extend(path for path in (data / "voices").rglob("*") if path.is_file())
    DATA_ARCHIVE_PATH.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(DATA_ARCHIVE_PATH, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(selected, key=lambda item: item.as_posix()):
            relative = path.relative_to(data).as_posix()
            info = zipfile.ZipInfo(f"espeak-ng-data/{relative}", (1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, path.read_bytes())


def main() -> int:
    lock = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
    identity = (
        f"{lock['commit']}\n{lock['archiveSha256'].lower()}\n"
        f"{lock['englishDataSha256'].lower()}\n"
    )
    if READY_PATH.is_file() and READY_PATH.read_text(encoding="utf-8") == identity:
        if ((SOURCE_PATH / "CMakeLists.txt").is_file() and DATA_ARCHIVE_PATH.is_file() and
                sha256(DATA_ARCHIVE_PATH).lower() == lock["englishDataSha256"].lower()):
            print(f"eSpeak NG {lock['version']} is ready")
            return 0

    CACHE_ROOT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="saberstage-espeak-") as temporary:
        archive = Path(temporary) / "source.zip"
        print(f"Downloading pinned eSpeak NG {lock['version']}...")
        request = urllib.request.Request(lock["archiveUrl"], headers={"User-Agent": "SaberStage-build"})
        with urllib.request.urlopen(request, timeout=120) as response, archive.open("wb") as output:
            shutil.copyfileobj(response, output)
        actual = sha256(archive)
        if actual.lower() != lock["archiveSha256"].lower():
            raise RuntimeError(f"eSpeak NG archive SHA-256 mismatch: expected {lock['archiveSha256']}, got {actual}")
        extracted = safe_extract(archive, Path(temporary) / "expanded")
        replacement = CACHE_ROOT / "source.new"
        if replacement.exists():
            remove_tree(replacement)
        shutil.copytree(extracted, replacement)
        if SOURCE_PATH.exists():
            remove_tree(SOURCE_PATH)
        replacement.replace(SOURCE_PATH)

    if NATIVE_BUILD_PATH.exists():
        remove_tree(NATIVE_BUILD_PATH)
    data = build_voice_data()
    write_english_archive(data)
    data_hash = sha256(DATA_ARCHIVE_PATH)
    if data_hash.lower() != lock["englishDataSha256"].lower():
        raise RuntimeError(
            "eSpeak NG English data SHA-256 mismatch: "
            f"expected {lock['englishDataSha256']}, got {data_hash}")
    READY_PATH.write_text(identity, encoding="utf-8", newline="\n")
    print(f"Prepared eSpeak NG source and {DATA_ARCHIVE_PATH.name} ({DATA_ARCHIVE_PATH.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:  # Build tooling must return the actionable root cause.
        print(f"eSpeak NG preparation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
