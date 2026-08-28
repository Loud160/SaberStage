# Tracking, reference spaces, and recenter

SaberStage keeps four explicit transforms: platform tracking space, Beat Saber player/scene space, persisted semantic camera/avatar intent, and final runtime world transform. Profiles are reconstructed against a `PlayerForwardAnchor` after tracking is valid.

`TrackingOriginService` samples the active XR origin mode and observes available tracking-origin/recenter/boundary changes. On a detected origin discontinuity it snapshots the old anchor, establishes the new current player forward, rebuilds runtime camera/avatar anchors from semantic intent, and clears smoothing/anchored-float velocity. It does not rewrite saved placement or calibration. During uncertainty, the last coherent pose is held for a bounded period rather than jumping.

The Prompt 3 camera runtime subscribes to each active `XRInputSubsystem.trackingOriginUpdated` event. It resolves Beat Saber's active `PlayerTransforms` associated with the HMD camera and uses its origin transform for the player anchor; the HMD pose is only a fallback when that object is not available. An origin event schedules a game-thread rebind and clears motion state. This path is compiled but remains a Quest device-test item.

Player-relative profiles follow normal Quest recenter logically. Explicit world-relative profiles, if added later, must declare their behavior. `Recenter Camera to Current Forward` and `Recenter Avatar to Current Forward` are recovery actions, not startup requirements.

Unity documents tracking origin modes and recenter through `XRInputSubsystem`; provider/platform behavior still requires a real Quest test. See [Unity XR input](https://docs.unity3d.com/Manual/xr_input.html) and [XR SDK input subsystem](https://docs.unity3d.com/Manual/xrsdk-input.html).
