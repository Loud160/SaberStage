#!/usr/bin/env python3
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
REMOTE_LIBRARY = f"{MOD_DATA}/Modloader/mods/libsaberstage.so"
RECEIPT_DIR = f"{MOD_DATA}/SaberStage/SourceInstall"
RECEIPT = f"{RECEIPT_DIR}/receipt.json"
REMOTE_RECORDINGS = f"{MOD_DATA}/Mods/SaberStage/Recordings"


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


def deploy(adb: Adb) -> None:
    library = ROOT / "build" / "libsaberstage.so"
    if not library.is_file():
        raise ToolError("build/libsaberstage.so is missing. Complete the verified build first.")
    expected = local_hash(library)
    receipt = adb.read_json(RECEIPT)
    current = adb.hash(REMOTE_LIBRARY)

    if receipt is None and current is not None:
        raise ToolError("libsaberstage.so already exists without a SaberStage source receipt. It may be MBF-managed; no files were changed.")
    if receipt is not None:
        if receipt.get("path") != REMOTE_LIBRARY or receipt.get("state") != "complete":
            raise ToolError("The SaberStage source receipt is unexpected or incomplete; no files were changed.")
        if current != receipt.get("installedSha256"):
            raise ToolError("The installed library changed since source deployment; no files were overwritten.")

    planned = {
        "schemaVersion": 1,
        "state": "planned",
        "path": REMOTE_LIBRARY,
        "installedSha256": expected,
        "deployedAtUtc": dt.datetime.now(dt.timezone.utc).isoformat(),
    }
    adb.write_json(planned, RECEIPT)
    adb.push(library, REMOTE_LIBRARY)
    actual = adb.hash(REMOTE_LIBRARY)
    if actual != expected:
        raise ToolError("Quest library hash did not match the built library after push.")
    planned["state"] = "complete"
    adb.write_json(planned, RECEIPT)
    adb.shell(f"am force-stop {PACKAGE}", check=False)
    adb.shell(f"monkey -p {PACKAGE} -c android.intent.category.LAUNCHER 1", check=False)
    print(f"Verified source deployment: {REMOTE_LIBRARY}\nSHA-256: {expected}")


def remove(adb: Adb) -> None:
    receipt = adb.read_json(RECEIPT)
    current = adb.hash(REMOTE_LIBRARY)
    if receipt is None:
        if current is None:
            print("No receipt-owned SaberStage source install was found. Nothing was removed.")
            return
        raise ToolError("libsaberstage.so exists without a source receipt. It may be MBF-managed; nothing was removed.")
    if receipt.get("path") != REMOTE_LIBRARY or receipt.get("state") != "complete":
        raise ToolError("The source receipt is unexpected or incomplete; nothing was removed.")
    if current != receipt.get("installedSha256"):
        raise ToolError("The installed library no longer matches the receipt; nothing was removed.")
    adb.shell(f"rm -f '{REMOTE_LIBRARY}' '{RECEIPT}'")
    adb.shell(f"rmdir '{RECEIPT_DIR}' 2>/dev/null || true", check=False)
    print(f"Removed only receipt-owned source file: {REMOTE_LIBRARY}")


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
            (work / name).write_text(result.stdout + result.stderr, encoding="utf-8", errors="replace")

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
        raise ToolError("The SaberStage Recordings folder does not exist on the Quest yet.")
    timestamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    root = output_root or (ROOT / "SaberStage Recordings")
    destination = root / f"SaberStage-Recordings-{timestamp}"
    destination.mkdir(parents=True, exist_ok=False)
    adb.pull(REMOTE_RECORDINGS, destination)
    print(f"Recordings copied to: {destination}")


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("deploy")
    subparsers.add_parser("remove")
    logs = subparsers.add_parser("collect-logs")
    logs.add_argument("--output-root", type=pathlib.Path)
    recordings = subparsers.add_parser("pull-recordings")
    recordings.add_argument("--output-root", type=pathlib.Path)
    args = parser.parse_args()
    adb = Adb()
    if args.command == "deploy": deploy(adb)
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
