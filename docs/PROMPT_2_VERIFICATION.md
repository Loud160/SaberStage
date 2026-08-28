# Prompt 2 verification record

Date: 2026-08-27 (America/New_York)

Status: **host-complete; final consolidated device run in progress**.

This record separates behavior directly observed on the Quest from host-only evidence. It must be updated before Prompt 2 is called complete.

## Build under test

- Target headset: Quest 2 (`hollywood`), serial ending `J1046`.
- Beat Saber: `1.40.8_7379`.
- SaberStage: `0.1.0`.
- Current native library SHA-256: `78e29a97de47ef3bf7e24c2f42da53bebf00e4640edf6ae65b8b3fece44b3270`.
- Current QMOD SHA-256: `0cc49cd1027190f995e4cfd8f69b556b4c828f6202fea9e7f5fdf91c625543dd`.
- The source-deployment receipt and Quest read-back both match the current native-library hash.

## Host verification

- QPM dependency restore: passed.
- Platform-neutral settings build: passed.
- CTest `saberstage.settings`: 1/1 passed.
- Settings tests cover defaults, stable initial camera identity, anchored-float default, field/type repair, even dimensions, every subsystem reset, reset isolation, factory reset, save/reload persistence, schema-0 migration, non-destructive future-schema handling, malformed JSON recovery, orphaned temporary cleanup, and interrupted-save backup recovery.
- Python receipt/tooling/repository suite: 11/11 passed.
- Android ARM64 cross-build: passed.
- QMOD package generation and automatic payload/manifest/ELF verification: passed.
- `llvm-readelf` confirmed ELF64 little-endian AArch64, exported `setup`/`late_load`, and the expected dynamic dependencies.
- Python development helper syntax check: passed.
- Linux launcher syntax checks (`bash -n`): passed.
- Support archive smoke test: passed. The archive contained logcat, device/package information, filtered SaberStage Paperlog lines, settings, source receipt/install hash information, and available crash-file listing.
- A host lockup had zero-filled QPM's three generated repo metadata files and its package-index cache. Each corrupt file was preserved with a `.corrupt-after-lockup` suffix, QPM regenerated fresh metadata/index files, and the serialized ARM64 build and package verification then passed.

## Quest observations

An earlier scaffold build loaded successfully and Beat Saber reached its menu. Paperlog directly showed:

- SaberStage `0.1.0` loading with target Beat Saber, QPM, and NDK versions;
- runtime Beat Saber `1.40.8_7379` and Unity `2022.3.33f1`;
- existing schema-1 settings loaded without migration or repair;
- the application root started.

The settings document exists at `/sdcard/ModData/com.beatgames.beatsaber/Mods/SaberStage/settings.json` and contains the expected safe defaults. Restart persistence was directly observed through the `Settings loaded` line.

The first UI attempt used `RegisterSettingsMenu`. Although BSML accepted that registration, the user correctly reported that SaberStage did not appear in the normal mod menu. That attempt is a device failure, not a pass. SaberStage now uses BSML's main-menu flow-coordinator registration. The user then verified that the main-menu entry appeared, opened, and contained two buttons. The description text clipped horizontally, so that build also remains a UI failure.

The first revised text layout still overlapped the `Reset general settings` button on-device because BSML's settings container did not honor child heights by default. The current build explicitly enables child-height control, disables forced height expansion, reserves fixed text/button rows, uses an eight-line bounded description, and prevents text overflow. Compile-time assertions limit both line width and line count.

The user clarified that the left-side surface must be the complete full-size menu, not a narrow rail or a collection of left-aligned controls. One historical candidate therefore registered one native SaberStage flow coordinator, supplied an empty center view, and put a 110-unit-wide menu in the left slot. The menu owned its title, Back control, description, and reset controls; the stock center title and Back control were hidden. It explicitly registered only `saberstage::ui::MenuFlowCoordinator`; it did not consume other mods' pending types through a broad `AutoRegister()` pass.

The `14d9ecbc...` build was a visual failure: it stretched a side-screen controller to 110 units and fabricated an in-panel header. The user observed an excessively wide and tall header, a lower-than-native header position, and a close-style control instead of Beat Saber's normal Back button. That implementation was removed.

The subsequent `2e8d2fb9...` candidate moved the native main screen to the left. Although it removed the fabricated header, that architecture would consume the center screen and prevent the intended Camera2 multi-panel layout, so it was rejected before visual acceptance.

The later `78e29a97...` candidate followed Camera2's multi-screen flow topology: `Cameras`/`Primary` on the native left screen, selected-camera settings with the native title and Back button in the center, bottom reserved for preview, and right available. It contained no forced screen width, custom close control, or screen-system relocation. All host, ARM64, and package checks passed and the deployed Quest library hash matched. Automatic launch was blocked by Quest's `common_system_dialog_app_launch_blocked_controller_required` guard, so runtime and visual evidence remained pending.

Prompt 4 superseded that candidate after on-device inspection. The active implementation now uses Big Screen's proven real side-panel contract: empty center, one native left `ViewController`, compact in-panel header/Close action, native segmented tabs, and native scroll containers. It does not restore the failed 110-unit stretched panel.

The corrected library was built, receipt-deployed, and discovered by Scotland2 as SaberStage `0.1.0`. That launch crashed before SaberStage's `late_load` callback. The tombstone's first three mod frames were in `libMultiplayerCore.so`; no SaberStage frame or startup line preceded the failure. A separate earlier startup tombstone began in `libchroma.so`. Those crashes are recorded as external test-environment blockers and are not attributed to SaberStage.

## Remaining device gate

Run `docs/PROMPT_2_HEADSET_CHECKLIST.md` once with the final build. It combines visible layout, both distinct reset behaviors, persistence, restart, clean exit, logs, and hashes in one device session.

Prompt 2 must not be called device-complete until those checks pass.
