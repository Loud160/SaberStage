# Prompt 15 — Implement SaberStage's integrated Quest avatar system

Implement the avatar as a first-class SaberStage capability after the camera, local recording, and remote-output architecture are stable.

The avatar exists to represent the player in SaberStage's third-person recording and broadcast views. It is intentionally part of the same mod rather than a separately supported general-purpose avatar product.

Do not copy or port an existing avatar mod. Research public formats, platform behavior, and user-visible expectations, then independently engineer the implementation.

## Study first

Research:

- VRM and other appropriate avatar formats;
- humanoid skeletons;
- 3-point IK;
- HMD/controller tracking;
- future full-body tracking inputs;
- skinned-mesh cost;
- shader/material restrictions;
- texture limits;
- physics/secondary motion;
- broadcast-only rendering and culling;
- shadows;
- update frequency;
- player-forward calibration;
- Quest recenter behavior;
- avatar licensing/metadata that must be preserved or shown.

## Core user experience

Follow the same rule as successful PC Camera2 plus avatar setups:

> Select and calibrate once, then the avatar loads correctly on later launches without routine attention.

Do not bake a mysterious absolute world heading during setup.

Separate:

- tracking space;
- current player forward;
- model-forward correction;
- scale;
- IK/body calibration;
- per-avatar offsets and profile data.

Desired normal behavior:

- selected avatar automatically loads on launch;
- the full avatar appears in the selected third-person camera, local recordings, companion output, TV output where supported, OBS output, and later broadcast scenes;
- it faces the correct player-forward direction;
- normal Quest recenter keeps it logically aligned;
- changing maps does not require recalibration;
- changing avatars loads that avatar's own profile rather than inheriting incompatible calibration.

Provide:

- Recenter Avatar to Current Forward;
- Reset Avatar Calibration;
- Reset Current Avatar Profile;
- Change Avatar;
- Disable Avatar;
- Factory Reset Avatar subsystem.

These are recovery and preference controls, not routine launch steps.

## Architecture

Avatar loading, tracking, IK, calibration, and rendering belong to an `AvatarService`/`AvatarManager` boundary inside SaberStage.

Do not couple avatar internals to:

- MediaCodec;
- MP4;
- RTMP/RTMPS;
- Wi-Fi;
- USB;
- Avalonia;
- OBS;
- Discord.

The camera/compositor sees a renderable player subject. Encoders and transports know only about frames and media packets.

## Visibility

Default:

```text
HMD:
full avatar hidden or minimal to avoid obstruction

Preview and output cameras:
full avatar visible according to the selected profile/scene
```

Allow meaningful visibility control, but prevent common self-obstruction and performance traps through safe defaults.

## Performance and support scope

Add avatar-complexity inspection and clear warnings. Measure avatar cost independently and in combination with preview, local recording, companion streaming, and heavy maps on Quest 2.

Document the supported avatar formats and use cases narrowly. Do not imply that SaberStage's avatar subsystem is a universal avatar framework for unrelated Quest applications or mods.

Obtain real on-headset visual verification of pose, calibration, orientation, culling, restart restoration, recenter, and recorded/remote output before calling the phase complete.
