# Prompt 3 spectator-camera implementation

Status: host-verified and ARM64-compiled; Quest runtime verification is still required. This is not a release claim.

## Implemented boundary

SaberStage now owns a bounded camera-profile collection containing one profile with stable ID `primary`. The collection and selected-camera ID are already durable contracts for later additional cameras, while validation prevents extra cameras from being activated or rendered in this release. The runtime creates a new Unity `GameObject` and `Camera`; it does not clone, reparent, disable, retag, or otherwise mutate the HMD camera. It copies scene rendering settings from the current main camera, forces mono output, applies SaberStage's FOV/clipping/culling policy, disables automatic rendering, and manually renders only when a registered consumer has frame demand.

The profile persists:

- display name, enabled state, stable ID, player/world reference frame, static/player/head follow mode, and generic subject anchor;
- position, rotation, FOV, requested width/height/FPS, and near/far clipping;
- position/rotation smoothing;
- anchored-float enable, dead zone, yaw range, lateral range, and response time;
- movement-script enable/file assignment;
- main-camera culling inheritance and an excluded-layer mask.

Schema 2 stores `selectedCameraId` plus a `profiles` array and migrates the unpublished schema 0/1 flat camera settings while retaining recognized FOV, resolution, profile ID, and anchored-float values. New fields receive safe defaults. Camera reset constructs a known visible player-relative profile; it does not reset unrelated subsystems.

## Deterministic motion order

Every update uses exactly this composition:

```text
saved base local pose
-> movement-script local pose (when a valid script and clock are active)
-> time-based position and quaternion smoothing
-> bounded critically damped lateral anchored-float offset
-> resolved player/head/static/world anchor
-> Unity spectator-camera transform
```

Script and anchored-float output never modify the saved base pose. Recenter rebinds the forward/static anchor and clears both smoothing and float velocity.

Player anchoring uses the active Beat Saber `PlayerTransforms` origin associated with the HMD camera. Head following uses the HMD camera transform. World-relative profiles use the current Unity tracking/world frame. Other generic subject anchors currently fall back to the player-root provider without introducing scene-content ownership into the camera subsystem.

## Camera2 movement-script compatibility

Place assigned scripts in:

```text
/sdcard/ModData/com.beatgames.beatsaber/Mods/SaberStage/MovementScripts/
```

The profile stores only a local `.json` file name. Directory separators, traversal, non-JSON extensions, files larger than 256 KiB, more than 4096 frames, nonfinite/out-of-range numbers, unsupported properties/transitions, overlong segments, and scripts longer than eight hours are rejected before activation.

The independently implemented parser accepts the public Camera2 keyframe contract:

- root: `syncToSong`, `loop`, `frames`;
- frame: `transition`, `position`, `rotation`, `FOV`, `duration`, `holdTime`;
- transitions: `Linear`, `Eased`.

Song-synchronized scripts read `AudioTimeSyncController.songTime` directly. Pause therefore freezes naturally; restart and practice seek evaluate directly from the new authoritative time instead of replaying accumulated deltas. Unsynchronized scripts use monotonic unscaled scene-session time. Non-looping scripts hold the last frame. Missing or rejected scripts leave the saved base profile active.

## Render-consumer contract

`CameraManager::SetRenderDemand` and `RemoveRenderDemand` identify consumers independently from the camera. The registry supports the future preview and capture consumers without adding another camera. Active requests negotiate one target and the highest requested cadence up to 60 FPS. `FrameScheduler` schedules 30/60 FPS demand independently from Unity update frequency.

With zero consumers, the render target is released and the spectator camera does not render. The movable HMD preview is the first consumer and remains Prompt 4 work; recording/encoding is not part of Prompt 3.

## Lifecycle

The camera runtime is game-thread owned under `ApplicationRoot`. It watches active-scene changes, tears down the prior camera and target, then reconstructs from the saved profile after a new HMD camera exists. Quest's IL2CPP image does not expose a callable concrete generic XR-subsystem enumeration, so abrupt live HMD pose discontinuities schedule a clean anchor rebind without invoking that unavailable API. Shutdown removes delegates, releases the target, destroys SaberStage Unity objects, and unbinds the update driver.

## Verification completed

- Host tests: profile safety repair, schema migration/round-trip persistence, transform/recenter math, quaternion interpolation, general smoothing, anchored-float damping/bounds/reset/non-mutation, Camera2-format parsing and rejection limits, transition timing/seek/completion, motion precedence, demand negotiation, and 30 FPS scheduling.
- Quest ARM64 Release build: successful against Beat Saber `1.40.8_7379` generated bindings with serialized build execution.

Still required on the Quest: startup/log verification, menu/game scene creation, pause/restart/finish/return transitions, camera enable/disable, actual XR recenter behavior, visual placement/FOV/culling, 720p/1080p render cost, and repeated-session stability. No device result should be inferred from the successful build.
