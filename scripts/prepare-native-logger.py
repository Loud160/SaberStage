#!/usr/bin/env python3
"""Prepare SaberStage's pinned Native Logger Quest source dependency safely."""

from __future__ import annotations

import hashlib
import json
import os
import pathlib
import shutil
import sys
import urllib.error
import urllib.request
import uuid
import zipfile


ROOT = pathlib.Path(__file__).resolve().parent.parent
LOCK = ROOT / "dependencies" / "native-logger.json"
CACHE = ROOT / ".cache" / "dependencies" / "native-logger-quest"
MAX_ARCHIVE_FILES = 512
MAX_EXTRACTED_BYTES = 16 * 1024 * 1024


class PreparationError(RuntimeError):
    pass


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_lock(value: object) -> dict:
    if not isinstance(value, dict) or value.get("schemaVersion") != 1:
        raise PreparationError("Native Logger Quest lock file has an unknown schema.")
    revision = value.get("revision")
    archive_url = value.get("archiveUrl")
    archive_hash = value.get("archiveSha256")
    source_directory = value.get("sourceDirectory")
    if not isinstance(revision, str) or len(revision) != 40 or any(c not in "0123456789abcdef" for c in revision):
        raise PreparationError("Native Logger Quest revision is invalid.")
    if archive_url != f"https://github.com/Loud160/NativeLoggerQuest/archive/{revision}.zip":
        raise PreparationError("Native Logger Quest must use its immutable official commit archive.")
    if not isinstance(archive_hash, str) or len(archive_hash) != 64 or any(c not in "0123456789abcdef" for c in archive_hash):
        raise PreparationError("Native Logger Quest archive SHA-256 is invalid.")
    if source_directory != f"NativeLoggerQuest-{revision}":
        raise PreparationError("Native Logger Quest archive root does not match its revision.")
    return value


def download(url: str, destination: pathlib.Path, expected_hash: str) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_file() and sha256(destination) == expected_hash:
        print("Using cached, SHA-256-verified Native Logger Quest archive.")
        return
    destination.unlink(missing_ok=True)
    temporary = destination.with_name(destination.name + f".download.{os.getpid()}")
    temporary.unlink(missing_ok=True)
    print(f"Downloading Native Logger Quest from {url}")
    try:
        urllib.request.urlretrieve(url, temporary)
        actual = sha256(temporary)
        if actual != expected_hash:
            raise PreparationError(
                f"Native Logger Quest SHA-256 mismatch: expected {expected_hash}, received {actual}.")
        os.replace(temporary, destination)
    except (urllib.error.URLError, OSError) as error:
        raise PreparationError(
            "Native Logger Quest could not be downloaded and no verified cached archive is available.") from error
    finally:
        temporary.unlink(missing_ok=True)


def extract(archive_path: pathlib.Path, staging: pathlib.Path, expected_root: str) -> None:
    extracted_bytes = 0
    names: set[str] = set()
    with zipfile.ZipFile(archive_path) as archive:
        entries = archive.infolist()
        if len(entries) > MAX_ARCHIVE_FILES:
            raise PreparationError("Native Logger Quest archive contains too many entries.")
        for entry in entries:
            name = entry.filename
            path = pathlib.PurePosixPath(name)
            mode = (entry.external_attr >> 16) & 0xFFFF
            if (
                not name
                or "\\" in name
                or name.startswith("/")
                or ".." in path.parts
                or not path.parts
                or path.parts[0] != expected_root
                or name in names
                or (mode & 0o170000) == 0o120000
            ):
                raise PreparationError(f"Unsafe Native Logger Quest archive entry: {name!r}")
            names.add(name)
            extracted_bytes += entry.file_size
            if extracted_bytes > MAX_EXTRACTED_BYTES:
                raise PreparationError("Native Logger Quest archive is unexpectedly large.")
            target = staging.joinpath(*path.parts)
            if entry.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(entry) as source, target.open("wb") as output:
                    shutil.copyfileobj(source, output)


def main() -> int:
    try:
        manifest = validate_lock(json.loads(LOCK.read_text(encoding="utf-8")))
        CACHE.mkdir(parents=True, exist_ok=True)
        archive = CACHE / f"NativeLoggerQuest-{manifest['revision']}.zip"
        source = CACHE / "source"
        ready = CACHE / "resolved.json"
        download(manifest["archiveUrl"], archive, manifest["archiveSha256"])

        required = (
            pathlib.Path("CMakeLists.txt"),
            pathlib.Path("include/NativeLoggerQuest/NativeLogger.hpp"),
            pathlib.Path("src/NativeLogger.cpp"),
            pathlib.Path("src/Paper2AbortBridge.cpp"),
        )
        cached_manifest = None
        if ready.is_file():
            try:
                cached_manifest = json.loads(ready.read_text(encoding="utf-8"))
            except json.JSONDecodeError:
                pass
        complete = cached_manifest == manifest and all((source / item).is_file() for item in required)
        if not complete:
            if source.exists():
                shutil.rmtree(source)
            staging = CACHE / f"extract-{uuid.uuid4().hex}"
            staging.mkdir()
            try:
                extract(archive, staging, manifest["sourceDirectory"])
                extracted = staging / manifest["sourceDirectory"]
                for item in required:
                    if not (extracted / item).is_file():
                        raise PreparationError(f"Native Logger Quest archive is missing {item.as_posix()}.")
                shutil.move(str(extracted), str(source))
            finally:
                shutil.rmtree(staging, ignore_errors=True)
            ready.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

        print(
            f"Prepared Native Logger Quest {manifest['version']} "
            f"at revision {manifest['revision'][:12]} in {source}")
        return 0
    except (PreparationError, OSError, ValueError, zipfile.BadZipFile, json.JSONDecodeError) as error:
        print(f"Native Logger Quest preparation failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
