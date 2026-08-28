# Integrated avatar architecture

The avatar is a Stage 3 SaberStage subsystem, not a separate general-purpose mod. `AvatarManager` owns selected asset, load/unload, rig, IK/tracking binding, per-avatar scale/model-forward correction, calibration, visibility, and failure fallback. Camera and compositor see a generic subject anchor/render source; encoder and transports do not know an avatar exists. The manager now owns exactly one `VrmUnityRuntime`; unload unbinds the solver before destroying the hierarchy, meshes, materials, textures, and generated Avatar.

Tracking inputs are HMD and controllers initially, with optional supported body trackers later. Calibration persists semantic offsets relative to the current player/tracking origin, never opaque scene-world coordinates. Recenter rebases runtime anchors while preserving calibration. Manual `Recenter Avatar` and per-avatar/factory resets remain recovery tools.

Default visibility is full avatar in broadcast and hidden/minimal in HMD to avoid body occlusion. Assets are validated and budgeted before activation; failure removes only the avatar source. VRM Qavatars demonstrates Quest VRM feasibility but is only a [research reference](https://github.com/BSQ-VRM/VRM-Qavatars); SaberStage will not copy its loader, rig, or implementation.

## Implemented trackerless-solver boundary

The `AvatarManager` now accepts one Unity humanoid `Animator` from the VRM runtime. Binding maps and caches the humanoid transforms once, records their original local and world rest poses, measures the avatar skeleton, disables the Animator so it cannot overwrite solved bones, and restores the original local transforms and Animator state on unbind. The solver does not discover or load VRM assets itself. `Vrm0Parser` produces neutral C++ data without Unity types; `VrmUnityRuntime` is the only glTF-to-Unity adapter and passes only the completed Animator through this seam.

Per rendered pose, `PlayerTransforms` supplies three cached Transform handles: head, left hand, and right hand. `Update` performs three `GetPositionAndRotation` tracking reads into a fixed-size native `TrackingSample`. `LateUpdate` runs the native solver and issues one `SetPositionAndRotation` call per mapped/affected humanoid bone. The primary SaberStage camera has a pre-cull callback that resamples, solves, and writes immediately before its manually controlled render, including Hollywood recording renders. Sequence and Unity-frame stamps prevent the same input sample from being solved twice while still allowing a deliberately fresher pre-render sample.

The current native solver contains measured eye-height scaling, a maximum-three-pass FABRIK spine, conservative shoulders, stateful elbow poles, analytic two-bone arms and legs, a `LOCKED`/`TURNING`/`SETTLING` body-yaw estimator, geometric pelvis lean/translation and crouch/bend estimation, persistent planted foot anchors, one-foot-at-a-time translation/pivot steps, and conservative airborne recovery. Optional FBT overrides, terrain feet, continuous locomotion, and gait animation are intentionally absent.

The solver hot path uses fixed arrays and value types. Host tests instrument global allocation and verify zero heap allocations during a complete solve. Managed object discovery and humanoid binding occur outside the per-frame solve path.
