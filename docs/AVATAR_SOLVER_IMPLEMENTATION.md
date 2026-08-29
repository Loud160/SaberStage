# Trackerless Beat Saber avatar solver

## Scope and stop point

This branch implements the three-point trackerless avatar pass through torso orientation, active pelvis estimation, crouch/lean, planted feet, discrete translation and pivot steps, analytic legs, and conservative hop recovery. It is intentionally optimized for one standing Beat Saber player rather than continuous world locomotion.

It does not add SlimeVR or other physical trackers, SpringBone physics, terrain or stair fitting, a gait clock, walking/running animation, neural estimation, or additional avatar formats. The already verified HMD/controller head and arm path remains direct and is solved before the inferred lower body.

## Native architecture

```text
PlayerTransforms/active HMD + controllers, or active Saber handles in gameplay
        |
        v
three cached pose reads in Update
        |
        v
TrackingSample (one coherent, fixed-size native value)
        |
        v
StaticTrackerlessAvatarSolver
  - exact calibrated avatar-eye and reachable hand-position targets
  - LOCKED / TURNING / SETTLING torso yaw
  - pelvis lean, persistent translation, crouch/bend estimate
  - single-curvature spine with bounded constrained-FABRIK fallback
  - bounded shoulders, stateful elbow poles, and two analytic arms
  - two persistent PLANTED / STEPPING feet
  - GROUNDED / AIRBORNE body mode
  - two stateful knee poles and analytic legs
        |
        v
SolvedHumanoidPose (enum-indexed fixed array)
        |
        v
one SetPositionAndRotation per mapped humanoid bone in LateUpdate
```

The spectator-camera pre-render hook can take one fresher three-pose sample and solve the complete body immediately before `Camera.Render()`. Sequence and render-frame stamps reject duplicate samples and prevent inferred state from advancing twice in one Unity frame.

## Calibration and direct upper-body authority

Humanoid binding measures arm and leg segments, approximate arm span, shoulder and hip width, every available hips-to-head segment, feet/toes, rest bend directions, eye position, head-to-eye pose, and skeletal floor. The eye pose uses VRM's actual first-person bone plus offset through the constructed Unity hierarchy when available, then mapped eye bones/fallback geometry. Neutral tracking calibration stores HMD and hand poses, floor/tracking origin, forward, standing HMD height, and controller-to-wrist fallbacks. The separate versioned player profile adds multi-pose grip/reach, lean/step signatures, crouch/duck tendencies, and turn timing without modifying VRM geometry. Scale remains `standing HMD height / avatar skeletal eye height`; arm dimensions are measured and reported separately rather than changing whole-avatar scale.

The Quest HMD drives the avatar eye/view anchor, and the desired Head bone pose is solved backward through the calibrated head-to-eye transform. In menus, wrist targets remain controller poses composed with their explicit calibration offsets. In gameplay, the active Beat Saber `Saber::_handleTransform` bypasses the controller-only offset and supplies the visible grip pose. Reachable hand positions have exact authority; bounded clavicle assistance and at most five-percent limb stretch are used before an unreachable target is clamped. A source-specific grip-to-hand-bone quaternion is calibrated against the anatomical rest chain, and hand rotation is limited to a 70-degree wrist deviation rather than copying controller/saber axes directly. Neither the eye nor reachable hand targets are smoothed by the body estimator.

## Body yaw and chest distribution

The persistent torso-yaw state machine has three states:

- `LOCKED` holds a yaw anchor inside the comfortable neck cone. A short excursion does not move chest, hips, legs, or feet.
- `TURNING` begins immediately beyond the hard cone or after a named soft-cone dwell. It rate-limits toward the head while leaving a small residual neck angle.
- `SETTLING` waits for small yaw error and low head angular speed before establishing a new anchor.

The gameplay-forward direction is only a four-degree-per-second drift prior near the original anchor. It cannot pull an intentional 90/360-degree turn back to the note highway. Controller rotation never drives torso yaw. Hand midpoint adds at most a small chest-only yaw contribution, and its confidence fades to zero during fast saber movement.

For an ordinary reachable pelvis-to-head span, measured spine chords are laid along one solved circular arc. This preserves every segment length, both endpoints, and one continuous bend direction. Degenerate or unreachable spans use a fixed-size, maximum-eight-pass FABRIK fallback guided toward a smooth curve; an end-anchored root shift is permitted only for a genuinely unreachable chain. Signed adjacent forward/lateral bends are measured and a sharp direction reversal raises the objective spine-Z diagnostic. Accumulated chain length distributes body yaw, residual head rotation, side/forward bend, and the bounded hand-midpoint contribution. The final eye and head rotation targets remain exact without overwriting the neck-to-head connection after the chain solve.

## Pelvis, lean, crouch, and translation

Horizontal HMD displacement is first measured relative to a persistent body translation. Motion inside a leg/spine/shoulder/eye-normalized anatomical limit is primarily lean: the spine bends while the pelvis follows only a small share and feet remain unchanged. The lateral lean has a hard believable maximum. Moderate sustained displacement begins moving the body/pelvis after a short dwell, while excess beyond the lean limit becomes translation immediately. Pelvis ground projection is clamped to a simple region around the planted support anchors, leg reach, and measured spine reach.

The geometric vertical estimator compares HMD height loss with forward displacement in the current torso frame. Mostly vertical loss lowers and slightly sets back the pelvis for a squat. Simultaneous forward displacement blends toward a hip hinge with less pelvis drop and more forward spine bend. Backward displacement cannot enter the forward-hinge term. Maximum drop and setback are normalized by measured leg reach; no fitted polynomial or inherited model constants are used.

Only inferred pelvis/body values use frame-rate-independent exponential stabilization. Head and wrist targets remain direct.

## Feet, steps, and legs

Each foot stores planted/current/start/destination poses, state, reason, progress, and duration. A `PLANTED` foot retains its exact world pose. Ideal stance is recalculated from pelvis projection, scaled hip width, torso yaw, and small capped translation/yaw prediction, but never directly moves an anchor.

Step requests independently score:

- pelvis leaving the support region;
- ideal-to-planted position error;
- useful leg extension for which a materially better destination exists;
- planted-to-ideal foot yaw divergence;
- persistent body translation.
- a short, capped prediction of support margin from HMD and body-translation velocity.

The greater urgency wins, lateral velocity provides a small side-selection preference, and ties alternate after the first step. A predicted support-edge request can overlap ongoing pelvis translation instead of waiting for an extreme completed lean. Stationary vertical crouches suppress meaningless translation/support steps while preserving legitimate yaw steps. Destinations cannot cross the body centerline, are distance-capped by leg reach, and keep the known Beat Saber floor height. Only one foot steps at a time; after landing a short double-support interval prevents rapid alternation.

A step uses cubic smoothstep for horizontal position, one parabolic lift arc, and quaternion interpolation for landing rotation. The final destination becomes the new fixed world anchor. The yaw error is an explicit trigger, so turn-in-place movement produces pivot steps without requiring translation.

Legs use the existing analytic two-bone primitive. The hip root comes from the solved moving pelvis. Knee poles combine body-forward direction, side-specific outward bias, measured rest direction, and previous-frame hemisphere. History becomes strongest near extension; crouches add forward knee direction and modest outward bias while retaining the measured hemisphere.

## Airborne handling

`AIRBORNE` requires sustained evidence: calibrated head rise plus upward velocity, or a larger rise with impossible grounded extension. It does not activate for ordinary vertical sway. While airborne, floor anchors are released and both feet relax below the moving hips with bounded knee bend. Descent and floor reachability must persist through landing hysteresis before plausible stance poses are planted again.

## Reset and recovery

`StaticTrackerlessAvatarSolver::Reset` clears all fixed-size inferred state. Avatar bind, unbind, neutral recalibration, VRM replacement, tracking restoration, large HMD discontinuity, significant tracking-origin translation/yaw change, and non-finite solver state all reset or reseed yaw, pelvis, feet, active steps, airborne state, and elbow/knee histories. This prevents stale world anchors from surviving scene/origin discontinuities.

## Tuning and diagnostics

`BodySolverTuning.hpp` contains every principal body threshold and rate under a named field. Most distance thresholds are fractions of measured leg reach, hip width, or calibrated eye height. Values are independently selected SaberStage starting points and must be tuned from Quest recordings; they are not copied from Basis, RenIK, Qavatars, or RootMotion Final IK.

The Avatar panel's `Write Diagnostic Log` action logs, without per-frame spam: HMD/head/eye poses and eye error; pelvis, lateral lean, crouch, forward hinge, translation and predicted support margin; each spine segment direction, signed adjacent bends and reversal warning; each arm's source, shoulder/target, measured lengths, reach ratio plus source-local min/average/max, elbow flexion/pole, hand error, target/grip-offset/final wrist quaternions and wrist error; and each foot's state, request reason, destination, progress/duration, leg reach, and knee pole. It also includes solve time/count, Transform reads/writes, spine error/passes, and limb reachability.

## Performance and validation status

The hot path uses only fixed-size arrays and trivially copyable persistent values. It performs no bone discovery, reflection, physics query, trajectory fitting, JSON work, or allocation during a solve. Host allocation instrumentation covers generic and active-player-profile solves plus dynamic body/step updates. Normal runtime input remains cached HMD/controller poses, active saber handles when present, and one write for each mapped/solved bone (normally about 20-24).

Host tests cover calibration and explicit first-person eye anchors; smooth fixed-length spine solving; neutral, half-bent, near-chest, cross-body, near-full and unreachable arm targets; exact reachable grip position; bounded stretch, shoulder and wrist behavior; controller-derived versus saber-derived player fits and source transitions; personalized return lean, persistent slow/fast translation, diagonal lean, head-only tilt, controller-only motion, crouch/duck and turn behavior; HMD translation/yaw/pitch/roll with exact avatar-eye coincidence and neck length; spine-reversal rejection; early translation/predicted-support stepping; planted anchors, one-foot-at-a-time steps, support-foot stability, step lift/landing, pivot steps, airborne entry/landing, deterministic reset/reseed, persistence fallback, and zero heap allocations. All six CTest targets and all twenty repository/tooling tests pass, and the Android ARM64 library compiles.

The first Black Heart full-body gameplay recording established the correction baseline: stick-straight arms, hand/saber separation, implausible wrists, head/neck discontinuity, waist/spine Z shapes, reversed-looking squat geometry, excessive lateral lean, and delayed steps. This correction pass is host-verified, ARM64-built, and receipt-deployed for the authorized retest, but it has not yet been visually evaluated. Typical gameplay reach ratios, Black Heart/player proportion mismatch, Quest-native solve time, and before/after visual success therefore remain intentionally unclaimed until the same-angle device pass.

## ozz provenance

`src/avatar/TwoBoneIK.cpp` is marked `MIT_OZZ_DERIVED`. It adapts only ozz-animation's MIT-licensed target-softening curve and analytic two-bone construction from revision `744eb9d99f606eda849acb0b1204f7a3dc20bca1`. The complete MIT notice is present both in that file and in `docs/THIRD_PARTY_NOTICES.md`.

SaberStage changes the implementation to scalar native math, world-space joint positions, fixed value inputs/outputs, and its own pole-plane construction. It imports no ozz containers, jobs, matrices, SIMD layer, skeleton runtime, or animation framework. No Qavatars FinalIK, RootMotion Final IK, Basis, or RenIK source or fitted model was used.
