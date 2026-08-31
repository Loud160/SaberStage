# SaberStage PC Pose Analyzer

This folder contains a **PC-only diagnostic plugin** used to study how a completed PC avatar pose relates to the tracked head and hand targets that produced it. It is development instrumentation for SaberStage's independently implemented Quest IK engine; it is not part of the Quest mod or its QMOD.

## Black-box boundary

**FinalIK is treated as a black box.**

The analyzer does not read, decompile, translate, patch, or redistribute FinalIK code. It does not call FinalIK implementation methods. It observes only:

- the head and hand target transforms exposed to the loaded avatar;
- the humanoid bone transforms visible after the PC avatar has been posed;
- measurements derived from those observable transforms.

The data describes behavior at the system boundary: targets in, completed avatar pose out. SaberStage's Quest solver remains a separate implementation.

Custom Avatars is discovered at runtime through its observable managed component surface. The analyzer intentionally has no compile-time reference to `CustomAvatar.dll` or `FinalIK.dll`, and neither binary is copied into this folder or the plugin output.

## What is recorded

Each JSONL pose sample contains:

- a torso-local coordinate frame;
- observable head, left-hand, and right-hand targets;
- pelvis, chest, head, shoulder, elbow, and wrist output transforms;
- upper-arm and forearm lengths;
- normalized reach;
- elbow flexion;
- elbow flare and arm-plane direction;
- wrist-to-target residual;
- target and resulting wrist orientation axes;
- observable per-finger curl inputs and the final thumb/index/middle/ring/little humanoid bone chains;
- every finger bone's hand-relative position, axes, and rotation plus a derived chain-bend angle;
- basic chest lean measurements;
- the PC Beat Saber, Custom Avatars, and analyzer versions.

Positions are expressed in Unity meters relative to a frame attached to the posed torso:

- `+X`: avatar right;
- `+Y`: avatar up;
- `+Z`: avatar forward.

This normalization makes captures from different player positions and room orientations directly comparable.

When Custom Avatars has no per-finger tracking source, the record explicitly marks the input as unavailable and records the fully-closed fallback that is visibly applied. The final finger-bone output is still captured. That distinction lets SaberStage reproduce an appropriate controller/saber grip without falsely describing a fallback fist as tracked finger data.

## Controls

The complete comparison workflow can be operated while wearing the headset. Controller controls are enabled by default:

- either primary face button (`A` or `X`) begins a labeled pose-window capture;
- tapping `Y` starts or stops the calibration timeline;
- holding `Y` for 1.25 seconds starts or stops continuous pose sampling;
- tapping `B` saves a screenshot of the active PC game output;
- holding `B` for 1.25 seconds hides or restores the headset help panel.

Using either `A` or `X` for a pose window means the tester can press the button on the hand that does not need to remain perfectly still. A short haptic pulse acknowledges the request, a stronger pulse marks the beginning of the measurement window after the configured countdown, and completion is written immediately before the automatic screenshot. Haptic feedback is best-effort on PC XR runtimes and is not required for capture.

The headset help text is shown by default on a fully transparent world-space canvas and is not interactive, so it cannot block Beat Saber menu pointers. It lists the workflow in order and updates its status and `NEXT` instruction while waiting for an avatar, recording calibration, counting down, sampling a held pose, or running a continuous sweep. It follows the player-view transform but does not assign a screen-space canvas to Beat Saber's camera or modify camera targets, stereo state, culling, or render textures. Set `showVrControllerOverlay` to `false` in `UserData/SaberStagePoseAnalyzer.json` to start with it hidden; holding `B` can still restore it during the session.

Keyboard shortcuts remain available only as a desktop fallback:

- `F9`: begin a labeled pose-window capture;
- `F8`: start or stop a calibration timeline;
- `F10`: start or stop continuous sampling;
- `F11`: save a screenshot.

Set `controllerButtonsEnabled` to `false` in `UserData/SaberStagePoseAnalyzer.json` only when the diagnostic plugin must remain installed during ordinary gameplay without controller-triggered captures.

Set `snapshotLabel` in the configuration before a capture to record the intended physical pose, for example `left-hand-at-shoulder-elbow-horizontal`. The screenshot is evidence of the rendered avatar pose; it is not treated as a measurement of the player's untracked physical elbow.

## Output

Sessions are written to:

```text
<Beat Saber>\UserData\SaberStagePoseAnalyzer\yyyyMMdd-HHmmss\
```

Each session contains:

- `pose-data.jsonl`: session metadata, markers, and pose samples;
- requested `.png` screenshots of the active PC game output.

Calibration observations include the avatar's authored eye height and arm span, its runtime and absolute scale, tracked head height relative to the avatar root, controller-to-controller span, output wrist span, and scale candidates derived independently from each measurement. A record is emitted whenever the runtime scale changes, even when the `F8` timeline is not active. This documents the observable calibration result without reading private calibration state or importing its implementation.

The writer runs on a low-priority background thread. Unity transforms are sampled only on the main thread, and file I/O is kept out of the frame loop.

## Build

The default build target is the locally installed BSManager 1.37.1 instance used for the current PC comparison:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\pc-pose-analyzer\build.ps1
```

To build against another modded PC installation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\pc-pose-analyzer\build.ps1 -BeatSaberDir "D:\Games\Beat Saber"
```

Add `-Install` to copy only `SaberStage.PcPoseAnalyzer.dll` into that installation's `Plugins` directory. The build script refuses to replace the DLL while Beat Saber is running.

## Initial comparison procedure

1. Select one PC avatar and keep its resize/calibration settings fixed.
2. Set a precise `snapshotLabel` in the JSON configuration.
3. Put on the headset, move away from the desk, hold the intended physical pose, and press `A` or `X` with whichever hand can move without disturbing the pose.
4. Remain still through the countdown and capture window.
5. Repeat the pose three to five times on both sides.
6. Include close-to-torso, half-reach, and full-reach variants at waist, chest, and shoulder height.
7. Hold `Y` for 1.25 seconds to start and stop slow continuous sweeps through the same space.
8. Compare the resulting target-to-elbow and target-to-wrist relationships with SaberStage's Quest output.

The first validation pass should verify that the late-frame observation point is actually after the installed Custom Avatars version has completed posing. If a version changes its update order, that adapter must be validated again rather than assuming identical timing.

## Optional Camera2 diagnostic preset

`camera2/SaberStage IK Diagnostic.json` is an optional fixed three-quarter view based on Camera2's normal positionable-camera configuration. Install it with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\pc-pose-analyzer\install-camera2-preset.ps1
```

The script copies the preset into Camera2's camera directory but does not rewrite `Scenes.json`, change existing cameras, or force Camera2 to activate it. Select the preset through Camera2 and make that view the active PC game output before requesting analyzer screenshots. This keeps camera setup independent from pose measurement and avoids depending on Camera2 internals.
