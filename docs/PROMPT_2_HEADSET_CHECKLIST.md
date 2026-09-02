# Prompt 2 consolidated headset checklist

Run this checklist only after the complete host/build/package gate passes. The goal is one meaningful device session, not one restart per cosmetic change.

## Preparation

1. Confirm the connected device is the intended Quest 2 and Beat Saber is `1.40.8_7379`.
2. Capture the existing SaberStage settings and source-install receipt before changing anything.
3. Back up the existing settings, then seed `tests/fixtures/prompt2-device-settings.json`. It sets `general.diagnosticsEnabled=false` and `camera.fovDegrees=92.0` while leaving every other field valid.
4. Deploy the final receipt-owned library and verify its Quest SHA-256 matches the locally verified build.

## First launch

1. Beat Saber reaches the main menu and remains running.
2. SaberStage's private native log shows final SaberStage `0.1.0`, target/runtime game versions, QPM/NDK versions, schema-1 settings load, main-menu registration, and application-root startup.
3. The private native log shows exactly one explicit registration for `saberstage::ui::MenuFlowCoordinator` and does not show SaberStage initiating a broad `custom_types::AutoRegister()` pass.
4. The left mod menu contains exactly one `SaberStage` entry.
5. Opening SaberStage matches Camera2's topology: a `Cameras` panel on the left containing `Primary`, and `SaberStage | Primary` selected-camera settings in the center with Beat Saber's normal Back button. The bottom and right screens remain unused in Prompt 2.
6. The complete eight-line description is visible without horizontal/vertical clipping or overlap with either reset button.
7. The center settings page exposes exactly `Reset general settings` and `Factory reset SaberStage`; the left panel exposes no premature camera actions, and no preview, recording, companion, avatar, scene, broadcast, chat, or Discord controls appear.

## Reset proof in the same launch

1. Press `Reset general settings` once.
2. Read back the settings file: `general.diagnosticsEnabled` must become `true`, while `camera.fovDegrees` must remain `92.0`.
3. Confirm the private native log contains `General settings reset` and no save error.
4. Press `Factory reset SaberStage` once.
5. Read back the settings file: the camera FOV must return to `70.0` and all other fields must match safe defaults.
6. Confirm the private native log contains `Factory reset completed` and no save error.

## Restart and exit

1. Exit Beat Saber normally and confirm there is no SaberStage frame in any tombstone.
2. Start Beat Saber once more.
3. Confirm the process remains healthy, settings load without migration/repair, the menu still opens, its text still fits, and the default settings survived restart.
4. Collect one SaberStage support ZIP and record the final QMOD/library hashes.

If an unrelated mod crashes startup, attribute the first native mod frames and repeat only the affected launch portion after that external issue is resolved. Do not disable, replace, or edit unrelated mods from the SaberStage task.
