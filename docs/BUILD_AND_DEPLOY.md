# Build, package, and development deployment

## Verified local baseline

This scaffold uses QPM CLI 1.5.11, Android NDK r27d, Windows CMake/Ninja for the Quest cross-build, and WSL2 CMake/Ninja for host tests. It targets Beat Saber Quest `1.40.8_7379`. The target is deliberately version-specific. Do not install the QMOD on a different Beat Saber build without updated generated bindings and revalidation.

## Restore and host tests

From PowerShell in the repository:

```powershell
& 'C:\Users\Owner\AppData\Local\Programs\QPM\qpm.exe' scripts host-test
```

This restores dependencies, builds the platform-neutral settings code in WSL, runs CTest, and runs the receipt/tooling/repository invariant tests on Windows.

## Build and package

```powershell
& 'C:\Users\Owner\AppData\Local\Programs\QPM\qpm.exe' scripts qmod
```

The build uses QPM's Ninja, the installed QPM NDK at `C:\Users\Owner\AppData\Roaming\QPM-RS\ndk\android-ndk-r27d`, and a user-local CMake at `C:\Users\Owner\AppData\Local\SaberStage\tools\cmake\cmake\data\bin\cmake.exe`. That CMake was installed as a user-local tool and is not part of the repository. On the first build, the script also invokes WSL to create the SHA-pinned private FFmpeg/Mbed TLS ARM64 runtime documented in `docs/DIRECT_FFMPEG_AND_LIVESTREAM.md`; later builds reuse the staged runtime.

Before CMake runs, `scripts/prepare-native-logger.py` resolves the immutable
Native Logger Quest revision recorded in `dependencies/native-logger.json`.
The archive must match its committed SHA-256 before extraction, and a verified
copy is reused from `.cache/dependencies/native-logger-quest` on later builds.
The logger is compiled statically into `libsaberstage.so`; it is not a QMOD
dependency or a separately installed Quest library. QPM dependencies may still
bring Paper2 for their own use.

Output is `SaberStage.qmod`. Packaging automatically verifies manifest
identity, payload boundaries, dependency IDs, byte-for-byte inclusion of
SaberStage and all three private FFmpeg libraries, ELF64/AArch64 identity, and
Scotland2 entry-point names. The native build additionally rejects a direct
Paper2 `DT_NEEDED` entry or leaked private Paper bridge symbols. Build outputs,
downloaded dependency sources, generated manifests, binaries, and QMODs are
ignored by Git. `qpm.shared.json` is intentionally tracked so the QPM-resolved
dependency graph can be reviewed and reproduced.

Rich chat adds the pinned Quest SongCore 1.1.26 dependency for this game version.
It also adds a viewport-clipped chat sprite shader to the shared Android bundle.
After editing shader source, run `scripts/build-avatar-shaders.ps1` before the
native build. Unity verifies all six addressable shaders in the produced bundle;
the existing avatar/preview shaders remain in the same archive. A source-only
deployment must not substitute an unverified SongCore build or overwrite another
mod's dependencies; use the normal QMOD dependency installation when needed.

## Development install and smoke test

Confirm one intended headset with:

```powershell
& 'C:\Users\Owner\AppData\Local\Programs\QPM\platform-tools\adb.exe' devices -l
```

For a normal managed installation, install `SaberStage.qmod` through ModsBeforeFriday or the established Quest mod manager for the target Beat Saber installation. Do not manually copy libraries around the modloader.

For source development, use `Build-And-Deploy.bat` on Windows or `Build-And-Deploy-Linux.sh` from a configured Linux/WSL environment. The deploy choice uses `scripts/quest_tool.py`; it writes an ownership receipt, refuses to overwrite an unreceipted or changed library, verifies the pushed SHA-256, and then launches Beat Saber. The launchers stop ADB when finished so ModsBeforeFriday can connect later.

Use `Collect-SaberStage-Logs.bat` / `Collect-SaberStage-Logs-Linux.sh` to create a support ZIP. Use `Remove-SaberStage.bat` / `Remove-SaberStage-Linux.sh` before returning to an MBF-managed copy. Removal is hash-gated and deletes only the receipt-owned source library and receipt; it preserves settings, logs, dependencies, recordings, and unrelated mods.

The support ZIP includes `saberstage-native.log` and
`saberstage-native.previous.log`, logcat, package/device information, redacted
settings, source-deployment receipt/hash state, and a crash-file listing. It
also includes a filtered Paper2 excerpt strictly for dependency context. Stream
keys and Twitch tokens are redacted; review any archive before sharing it.

After an authorized recording test, copy recordings without deleting them from the Quest:

```powershell
python scripts\quest_tool.py pull-recordings
```

The command copies completed `SaberStage_*.mp4` files from the Quest's standard `/sdcard/Oculus/VideoShots` folder into a new timestamped folder under `SaberStage Recordings` in the repository. It does not copy the Quest recorder's unrelated videos and does not delete anything from the headset.

Capture the support archive while launching Beat Saber and verify the exact checklist in `docs/TEST_PLAN.md`.

The settings file should be created at:

```text
/sdcard/ModData/com.beatgames.beatsaber/Mods/SaberStage/settings.json
```

The private log files should be created at:

```text
/sdcard/ModData/com.beatgames.beatsaber/Mods/SaberStage/Logs/saberstage-native.log
/sdcard/ModData/com.beatgames.beatsaber/Mods/SaberStage/Logs/saberstage-native.previous.log
```

Device deployment is development-only. Keep a copy of logs and QMOD hash with every smoke result.
