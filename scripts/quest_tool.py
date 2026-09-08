#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
#
# Part of SaberStage.
# Distributed under GPL-3.0-only with additional terms under GPLv3
# section 7(b)/(c) and an interoperability permission under section 7;
# see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

# File responsibility:
# - Implements shared Quest discovery, deployment, removal, and log collection.
# - Device and install ownership checks remain identical across Windows and Linux wrappers.

"""Receipt-owned SaberStage source deploy, removal, and support logs.

The workflow is adapted from Big Screen's source-install safety model: exact
paths, hashes, and receipts prevent a local build from silently overwriting or
removing an MBF-managed or user-modified file.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
PACKAGE = "com.beatgames.beatsaber"
MOD_DATA = f"/sdcard/ModData/{PACKAGE}"
REMOTE_LIBRARY = f"{MOD_DATA}/Modloader/early_mods/libsaberstage.so"
REMOTE_FFMPEG_LIBRARIES = (
    f"{MOD_DATA}/Modloader/libs/libavformat-saberstage9.so",
    f"{MOD_DATA}/Modloader/libs/libavcodec-saberstage9.so",
    f"{MOD_DATA}/Modloader/libs/libavutil-saberstage9.so",
)
REMOTE_TTS_LIBRARIES = (
    f"{MOD_DATA}/Modloader/libs/libsabstageort.so",
    f"{MOD_DATA}/Modloader/libs/libss-tts-neural-api.so",
)
RECEIPT_DIR = f"{MOD_DATA}/SaberStage/SourceInstall"
RECEIPT = f"{RECEIPT_DIR}/receipt.json"
REMOTE_RECORDINGS = "/sdcard/Oculus/VideoShots"


class ToolError(RuntimeError):
    pass


def adb_path() -> str:
    configured = os.environ.get("SABERSTAGE_ADB")
    candidates = [configured] if configured else []
    candidates += [shutil.which("adb"), str(pathlib.Path.home() / "AppData/Local/Programs/QPM/platform-tools/adb.exe")]
    for candidate in candidates:
        if candidate and pathlib.Path(candidate).is_file():
            return candidate
    raise ToolError("ADB was not found. Install QPM/Android platform-tools or set SABERSTAGE_ADB.")


class Adb:
    def __init__(self) -> None:
        self.executable = adb_path()
        self.serial = self._select_device()

    def _raw(self, *args: str, check: bool = True, text: bool = True) -> subprocess.CompletedProcess:
        command = [self.executable]
        if getattr(self, "serial", None):
            command += ["-s", self.serial]
        command += list(args)
        return subprocess.run(command, check=check, text=text, capture_output=True)

    def _select_device(self) -> str:
        result = subprocess.run([self.executable, "devices"], check=True, text=True, capture_output=True)
        devices = []
        for line in result.stdout.splitlines()[1:]:
            fields = line.split()
            if len(fields) >= 2 and fields[1] == "device":
                devices.append(fields[0])
        requested = os.environ.get("ANDROID_SERIAL")
        if requested:
            if requested not in devices:
                raise ToolError(f"ANDROID_SERIAL {requested!r} is not an authorized connected device.")
            return requested
        if len(devices) != 1:
            raise ToolError(f"Expected exactly one authorized ADB device, found {len(devices)}. Set ANDROID_SERIAL if needed.")
        return devices[0]

    def shell(self, command: str, check: bool = True) -> str:
        return self._raw("shell", command, check=check).stdout.strip()

    def exists(self, remote: str) -> bool:
        return self.shell(f"if [ -f '{remote}' ]; then echo yes; fi") == "yes"

    def hash(self, remote: str) -> str | None:
        if not self.exists(remote):
            return None
        output = self.shell(f"sha256sum '{remote}'")
        return output.split()[0].lower() if output else None

    def read_json(self, remote: str) -> dict | None:
        if not self.exists(remote):
            return None
        try:
            return json.loads(self.shell(f"cat '{remote}'"))
        except (json.JSONDecodeError, subprocess.CalledProcessError) as exc:
            raise ToolError(f"Source receipt at {remote} is unreadable; no files were changed.") from exc

    def write_json(self, value: dict, remote: str) -> None:
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", delete=False, suffix=".json") as handle:
            json.dump(value, handle, indent=2, sort_keys=True)
            temporary = pathlib.Path(handle.name)
        try:
            self.shell(f"mkdir -p '{RECEIPT_DIR}'")
            self._raw("push", str(temporary), remote)
        finally:
            temporary.unlink(missing_ok=True)

    def push(self, local: pathlib.Path, remote: str) -> None:
        self.shell(f"mkdir -p '{pathlib.PurePosixPath(remote).parent}'")
        self._raw("push", str(local), remote)

    def pull(self, remote: str, local: pathlib.Path) -> None:
        self._raw("pull", remote, str(local))


def local_hash(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def deployment_payloads() -> tuple[tuple[pathlib.Path, str], ...]:
    ffmpeg = ROOT / ".cache" / "dependencies" / "ffmpeg-hardware" / "lib"
    tts = ROOT / ".cache" / "dependencies" / "kitten-tts" / "lib"
    return (
        (ROOT / "build" / "libsaberstage.so", REMOTE_LIBRARY),
        (ffmpeg / "libavformat-saberstage9.so", REMOTE_FFMPEG_LIBRARIES[0]),
        (ffmpeg / "libavcodec-saberstage9.so", REMOTE_FFMPEG_LIBRARIES[1]),
        (ffmpeg / "libavutil-saberstage9.so", REMOTE_FFMPEG_LIBRARIES[2]),
        (tts / "libsabstageort.so", REMOTE_TTS_LIBRARIES[0]),
        (tts / "libss-tts-neural-api.so", REMOTE_TTS_LIBRARIES[1]),
    )


def receipt_files(receipt: dict) -> dict[str, str]:
    if receipt.get("schemaVersion") == 1:
        path = receipt.get("path")
        digest = receipt.get("installedSha256")
        if path != REMOTE_LIBRARY or not isinstance(digest, str):
            raise ToolError("The legacy SaberStage source receipt is unexpected; no files were changed.")
        return {path: digest}
    if receipt.get("schemaVersion") != 2 or not isinstance(receipt.get("files"), list):
        raise ToolError("The SaberStage source receipt has an unsupported schema; no files were changed.")
    files: dict[str, str] = {}
    for item in receipt["files"]:
        if not isinstance(item, dict) or not isinstance(item.get("path"), str) or not isinstance(item.get("installedSha256"), str):
            raise ToolError("The SaberStage source receipt contains an invalid payload; no files were changed.")
        if item["path"] in files:
            raise ToolError("The SaberStage source receipt contains a duplicate payload; no files were changed.")
        files[item["path"]] = item["installedSha256"]
    return files


def deploy(adb: Adb, launch: bool = True) -> None:
    payloads = deployment_payloads()
    missing = [str(local.relative_to(ROOT)) for local, _ in payloads if not local.is_file()]
    if missing:
        raise ToolError("Verified deployment payload is missing: " + ", ".join(missing))
    expected = {remote: local_hash(local) for local, remote in payloads}
    receipt = adb.read_json(RECEIPT)
    owned: dict[str, str] = {}
    if receipt is not None:
        if receipt.get("state") != "complete":
            raise ToolError("The SaberStage source receipt is incomplete; no files were changed.")
        owned = receipt_files(receipt)
        if not set(owned).issubset(expected):
            raise ToolError("The SaberStage source receipt owns an unexpected path; no files were changed.")

    for _, remote in payloads:
        current = adb.hash(remote)
        if remote not in owned and current is not None:
            raise ToolError(
                f"{pathlib.PurePosixPath(remote).name} already exists without a SaberStage source receipt. "
                "It may be MBF-managed; no files were changed.")
        if remote in owned and current != owned[remote]:
            raise ToolError(
                f"{pathlib.PurePosixPath(remote).name} changed since source deployment; no files were overwritten.")

    planned = {
        "schemaVersion": 2,
        "state": "planned",
        "files": [
            {"path": remote, "installedSha256": expected[remote]}
            for _, remote in payloads
        ],
        "deployedAtUtc": dt.datetime.now(dt.timezone.utc).isoformat(),
    }
    adb.write_json(planned, RECEIPT)
    for local, remote in payloads:
        adb.push(local, remote)
    for _, remote in payloads:
        if adb.hash(remote) != expected[remote]:
            raise ToolError(
                f"Quest hash for {pathlib.PurePosixPath(remote).name} did not match the verified payload after push.")
    planned["state"] = "complete"
    adb.write_json(planned, RECEIPT)
    if launch:
        adb.shell(f"am force-stop {PACKAGE}", check=False)
        adb.shell(f"monkey -p {PACKAGE} -c android.intent.category.LAUNCHER 1", check=False)
    print("Verified source deployment:")
    for _, remote in payloads:
        print(f"{remote}\nSHA-256: {expected[remote]}")


def remove(adb: Adb) -> None:
    receipt = adb.read_json(RECEIPT)
    if receipt is None:
        current = {remote: adb.hash(remote) for _, remote in deployment_payloads()}
        if not any(current.values()):
            print("No receipt-owned SaberStage source install was found. Nothing was removed.")
            return
        raise ToolError("SaberStage payload files exist without a source receipt. They may be MBF-managed; nothing was removed.")
    if receipt.get("state") != "complete":
        raise ToolError("The source receipt is unexpected or incomplete; nothing was removed.")
    owned = receipt_files(receipt)
    allowed = {remote for _, remote in deployment_payloads()}
    if not set(owned).issubset(allowed):
        raise ToolError("The source receipt owns an unexpected path; nothing was removed.")
    for remote, digest in owned.items():
        if adb.hash(remote) != digest:
            raise ToolError(
                f"{pathlib.PurePosixPath(remote).name} no longer matches the receipt; nothing was removed.")
    quoted = " ".join(f"'{remote}'" for remote in (*owned, RECEIPT))
    adb.shell(f"rm -f {quoted}")
    adb.shell(f"rmdir '{RECEIPT_DIR}' 2>/dev/null || true", check=False)
    print("Removed only receipt-owned SaberStage source files.")


def redact_settings_credentials(raw: str) -> str:
    """Return support-safe settings JSON without forwarding broadcast secrets.

    Settings can contain one saved stream key for each service plus Twitch
    OAuth access/refresh tokens. Parse and redact by field name recursively so
    future nesting changes remain safe. A malformed file is omitted entirely
    because regex replacement cannot prove that a private value was removed.
    """
    if raw.strip() == "SaberStage settings file is absent.":
        return raw
    try:
        document = json.loads(raw)
    except (json.JSONDecodeError, TypeError):
        return "SaberStage settings were omitted because credential-safe redaction could not parse the file.\n"

    def redact(value):
        if isinstance(value, dict):
            for name, child in value.items():
                normalized = name.lower().replace("_", "")
                if normalized in {
                    "streamkey",
                    "accesstoken",
                    "refreshtoken",
                    "protectedtokenenvelope",
                }:
                    value[name] = "<redacted>" if child else ""
                else:
                    redact(child)
        elif isinstance(value, list):
            for child in value:
                redact(child)

    redact(document)
    return json.dumps(document, indent=2, ensure_ascii=False) + "\n"


def collect_logs(adb: Adb, output_root: pathlib.Path | None) -> None:
    timestamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    root = output_root or (ROOT / "SaberStage Support Logs")
    root.mkdir(parents=True, exist_ok=True)
    work = root / f"SaberStage-{timestamp}"
    work.mkdir()
    try:
        commands = {
            "logcat.txt": ["logcat", "-d", "-v", "threadtime"],
            "devices.txt": ["devices", "-l"],
            "package.txt": ["shell", "dumpsys", "package", PACKAGE],
        }
        for name, arguments in commands.items():
            result = adb._raw(*arguments, check=False)
            (work / name).write_text(result.stdout + result.stderr, encoding="utf-8", errors="replace")

        remote_commands = {
            "saberstage-native.log": (
                f"if [ -f '{MOD_DATA}/Mods/SaberStage/Logs/saberstage-native.log' ]; then "
                f"cat '{MOD_DATA}/Mods/SaberStage/Logs/saberstage-native.log'; "
                "else echo 'SaberStage native log is absent.'; fi"
            ),
            "saberstage-native.previous.log": (
                f"if [ -f '{MOD_DATA}/Mods/SaberStage/Logs/saberstage-native.previous.log' ]; then "
                f"cat '{MOD_DATA}/Mods/SaberStage/Logs/saberstage-native.previous.log'; "
                "else echo 'Previous SaberStage native log is absent.'; fi"
            ),
            # SaberStage itself no longer uses Paper2. Keep this filtered file
            # because transitive dependencies such as BSML may still explain a
            # startup failure in the same support session.
            "saberstage-paperlog.txt": (
                "for log in "
                f"'{MOD_DATA}/logs2/Paperlog.log' '{MOD_DATA}/logs/PaperLog.log'; do "
                "if [ -f \"$log\" ]; then "
                "echo \"===== $log =====\"; grep -i -n -C 8 'SaberStage' \"$log\" || true; "
                "fi; done"
            ),
            "settings.json": (
                f"if [ -f '{MOD_DATA}/Mods/SaberStage/settings.json' ]; then "
                f"cat '{MOD_DATA}/Mods/SaberStage/settings.json'; "
                "else echo 'SaberStage settings file is absent.'; fi"
            ),
            "source-install-receipt.json": (
                f"if [ -f '{RECEIPT}' ]; then cat '{RECEIPT}'; "
                "else echo 'SaberStage source-install receipt is absent.'; fi"
            ),
            "source-install-files.txt": (
                f"ls -l '{REMOTE_LIBRARY}' '{RECEIPT}' 2>&1; "
                f"sha256sum '{REMOTE_LIBRARY}' 2>&1"
            ),
            "available-crash-files.txt": (
                f"ls -lt /sdcard/Android/data/{PACKAGE}/files/tombstone_* 2>&1"
            ),
        }
        for name, command in remote_commands.items():
            result = adb._raw("shell", command, check=False)
            output = result.stdout + result.stderr
            if name == "settings.json":
                output = redact_settings_credentials(result.stdout)
                if result.stderr:
                    output += "\nADB error output:\n" + result.stderr
            (work / name).write_text(output, encoding="utf-8", errors="replace")

        (work / "device.txt").write_text(
            adb.shell("getprop ro.product.model", check=False) + "\n" +
            adb.shell("getprop ro.build.display.id", check=False) + "\n",
            encoding="utf-8",
        )
        archive = root / f"SaberStage-Support-Logs-{timestamp}.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
            for path in work.rglob("*"):
                if path.is_file():
                    output.write(path, path.relative_to(work))
        print(f"Support archive: {archive}")
    finally:
        shutil.rmtree(work, ignore_errors=True)


def pull_recordings(adb: Adb, output_root: pathlib.Path | None) -> None:
    if adb.shell(f"if [ -d '{REMOTE_RECORDINGS}' ]; then echo yes; fi") != "yes":
        raise ToolError("The Quest VideoShots folder does not exist yet.")
    listing = adb.shell(
        f"for file in '{REMOTE_RECORDINGS}'/SaberStage_*.mp4; do "
        '[ -f "$file" ] && printf \'%s\\n\' "$file"; done'
    )
    recordings = [line.strip() for line in listing.splitlines() if line.strip()]
    if not recordings:
        raise ToolError("No completed SaberStage recordings were found in the Quest VideoShots folder.")
    timestamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    root = output_root or (ROOT / "SaberStage Recordings")
    destination = root / f"SaberStage-Recordings-{timestamp}"
    destination.mkdir(parents=True, exist_ok=False)
    for remote in recordings:
        adb.pull(remote, destination / pathlib.PurePosixPath(remote).name)
    print(f"Recordings copied to: {destination}")


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    deploy_parser = subparsers.add_parser("deploy")
    deploy_parser.add_argument(
        "--no-launch",
        action="store_true",
        help="install and hash-verify the payload without stopping or launching Beat Saber",
    )
    subparsers.add_parser("remove")
    logs = subparsers.add_parser("collect-logs")
    logs.add_argument("--output-root", type=pathlib.Path)
    recordings = subparsers.add_parser("pull-recordings")
    recordings.add_argument("--output-root", type=pathlib.Path)
    args = parser.parse_args()
    adb = Adb()
    if args.command == "deploy": deploy(adb, launch=not args.no_launch)
    elif args.command == "remove": remove(adb)
    elif args.command == "collect-logs": collect_logs(adb, args.output_root)
    else: pull_recordings(adb, args.output_root)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ToolError, subprocess.CalledProcessError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
