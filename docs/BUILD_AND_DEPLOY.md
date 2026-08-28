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

The build uses QPM's Ninja, the installed QPM NDK at `C:\Users\Owner\AppData\Roaming\QPM-RS\ndk\android-ndk-r27d`, and a user-local CMake at `C:\Users\Owner\AppData\Local\SaberStage\tools\cmake\cmake\data\bin\cmake.exe`. That CMake was installed as a user-local tool and is not part of the repository. Output is `SaberStage.qmod`. Packaging automatically verifies manifest identity, payload boundaries, dependency IDs, byte-for-byte library inclusion, ELF64/AArch64 identity, and Scotland2 entry-point names. Generated dependencies, manifests, binaries, and QMODs are intentionally ignored by Git.

## Development install and smoke test

Confirm one intended headset with:

```powershell
& 'C:\Users\Owner\AppData\Local\Programs\QPM\platform-tools\adb.exe' devices -l
```

For a normal managed installation, install `SaberStage.qmod` through ModsBeforeFriday or the established Quest mod manager for the target Beat Saber installation. Do not manually copy libraries around the modloader.

For source development, use `Build-And-Deploy.bat` on Windows or `Build-And-Deploy-Linux.sh` from a configured Linux/WSL environment. The deploy choice uses `scripts/quest_tool.py`; it writes an ownership receipt, refuses to overwrite an unreceipted or changed library, verifies the pushed SHA-256, and then launches Beat Saber. The launchers stop ADB when finished so ModsBeforeFriday can connect later.

Use `Collect-SaberStage-Logs.bat` / `Collect-SaberStage-Logs-Linux.sh` to create a support ZIP. Use `Remove-SaberStage.bat` / `Remove-SaberStage-Linux.sh` before returning to an MBF-managed copy. Removal is hash-gated and deletes only the receipt-owned source library and receipt; it preserves settings, logs, dependencies, recordings, and unrelated mods.

After an authorized recording test, copy recordings without deleting them from the Quest:

```powershell
python scripts\quest_tool.py pull-recordings
```

The command creates a new timestamped folder under `SaberStage Recordings` in the repository.

Capture the support archive while launching Beat Saber and verify the exact checklist in `docs/TEST_PLAN.md`.

The settings file should be created at:

```text
/sdcard/ModData/com.beatgames.beatsaber/Mods/SaberStage/settings.json
```

Device deployment is development-only. Keep a copy of logs and QMOD hash with every smoke result.
