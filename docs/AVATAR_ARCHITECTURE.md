# Integrated avatar architecture

The avatar is a Stage 3 SaberStage subsystem, not a separate general-purpose mod. `AvatarManager` owns selected asset, load/unload, rig, IK/tracking binding, per-avatar scale/model-forward correction, calibration, visibility, and failure fallback. Camera and compositor see a generic subject anchor/render source; encoder and transports do not know an avatar exists.

Tracking inputs are HMD and controllers initially, with optional supported body trackers later. Calibration persists semantic offsets relative to the current player/tracking origin, never opaque scene-world coordinates. Recenter rebases runtime anchors while preserving calibration. Manual `Recenter Avatar` and per-avatar/factory resets remain recovery tools.

Default visibility is full avatar in broadcast and hidden/minimal in HMD to avoid body occlusion. Assets are validated and budgeted before activation; failure removes only the avatar source. VRM Qavatars demonstrates Quest VRM feasibility but is only a [research reference](https://github.com/BSQ-VRM/VRM-Qavatars); SaberStage will not copy its loader, rig, or implementation.
