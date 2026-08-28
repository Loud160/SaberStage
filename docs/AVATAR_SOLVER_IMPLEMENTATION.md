# Trackerless Beat Saber avatar solver

## Scope and stop point

This branch implements the three-point trackerless avatar pass through torso orientation, active pelvis estimation, crouch/lean, planted feet, discrete translation and pivot steps, analytic legs, and conservative hop recovery. It is intentionally optimized for one standing Beat Saber player rather than continuous world locomotion.

It does not add SlimeVR or other physical trackers, SpringBone physics, terrain or stair fitting, a gait clock, walking/running animation, neural estimation, or additional avatar formats. The already verified HMD/controller head and arm path remains direct and is solved before the inferred lower body.

## Native architecture

```text
PlayerTransforms or active HMD camera + VRController pair
        |
        v
three cached pose reads in Update
        |
        v
TrackingSample (one coherent, fixed-size native value)
        |
        v
StaticTrackerlessAvatarSolver
  - exact calibrated head and wrist targets
  - LOCKED / TURNING / SETTLING torso yaw
  - pelvis lean, persistent translation, crouch/bend estimate
  - bounded FABRIK spine and conservative shoulders
  - stateful elbow poles and two analytic arms
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

Humanoid binding measures arm and leg segments, shoulder and hip width, every available hips-to-head segment, feet/toes, rest bend directions, eye position, and skeletal floor. Player calibration stores neutral HMD pose, floor/tracking origin, forward, standing HMD height, and controller-to-wrist offsets. Scale remains `standing HMD height / avatar skeletal eye height`.

Head position and rotation are reconstructed directly from the calibrated HMD delta. Wrist targets are direct controller poses composed with their calibration offsets. Neither is smoothed by the body estimator. The spine, shoulders, pelvis, and feet solve around these targets. Regression tests exercise head position/rotation, wrist position/rotation, elbow pole hemisphere, dynamic lower-body motion, and the pre-render second solve path.

## Body yaw and chest distribution

The persistent torso-yaw state machine has three states:

- `LOCKED` holds a yaw anchor inside the comfortable neck cone. A short excursion does not move chest, hips, legs, or feet.
- `TURNING` begins immediately beyond the hard cone or after a named soft-cone dwell. It rate-limits toward the head while leaving a small residual neck angle.
- `SETTLING` waits for small yaw error and low head angular speed before establishing a new anchor.

The gameplay-forward direction is only a four-degree-per-second drift prior near the original anchor. It cannot pull an intentional 90/360-degree turn back to the note highway. Controller rotation never drives torso yaw. Hand midpoint adds at most a small chest-only yaw contribution, and its confidence fades to zero during fast saber movement.

FABRIK connects the estimated pelvis to the exact head target with measured segment lengths, two normal passes, and three maximum passes. Accumulated chain length distributes body yaw, residual head rotation, side/forward bend, and the bounded hand-midpoint contribution. The final head rotation remains exact.

## Pelvis, lean, crouch, and translation

Horizontal HMD displacement is first measured relative to a persistent body translation. Motion inside a leg-length-scaled support radius is primarily lean: the spine bends while the pelvis follows only a small share and feet remain unchanged. Displacement outside the translation threshold must persist through a dwell, then moves a smoothed body origin. Pelvis motion is constrained against planted-foot support, leg reach, and measured spine reach.

The geometric vertical estimator compares HMD height loss with forward displacement in the current torso frame. Mostly vertical loss lowers the pelvis strongly for a squat. Simultaneous forward displacement blends toward a hip hinge with less pelvis drop and more spine bend. Maximum drop is normalized by measured leg reach; no fitted polynomial or inherited model constants are used.

Only inferred pelvis/body values use frame-rate-independent exponential stabilization. Head and wrist targets remain direct.

## Feet, steps, and legs

Each foot stores planted/current/start/destination poses, state, reason, progress, and duration. A `PLANTED` foot retains its exact world pose. Ideal stance is recalculated from pelvis projection, scaled hip width, torso yaw, and small capped translation/yaw prediction, but never directly moves an anchor.

Step requests independently score:

- pelvis leaving the support region;
- ideal-to-planted position error;
- useful leg extension for which a materially better destination exists;
- planted-to-ideal foot yaw divergence;
- persistent body translation.

The greater urgency wins, lateral velocity provides a small side-selection preference, and ties alternate after the first step. Destinations cannot cross the body centerline, are distance-capped by leg reach, and keep the known Beat Saber floor height. Only one foot steps at a time; after landing a short double-support interval prevents rapid alternation.

A step uses cubic smoothstep for horizontal position, one parabolic lift arc, and quaternion interpolation for landing rotation. The final destination becomes the new fixed world anchor. The yaw error is an explicit trigger, so turn-in-place movement produces pivot steps without requiring translation.

Legs use the existing analytic two-bone primitive. The hip root now comes from the solved moving pelvis rather than the neutral pose. Knee poles combine body-forward direction, side-specific outward bias, measured rest direction, and previous-frame hemisphere. History becomes strongest near extension and outward bias increases during deep crouches.

## Airborne handling

`AIRBORNE` requires sustained evidence: calibrated head rise plus upward velocity, or a larger rise with impossible grounded extension. It does not activate for ordinary vertical sway. While airborne, floor anchors are released and both feet relax below the moving hips with bounded knee bend. Descent and floor reachability must persist through landing hysteresis before plausible stance poses are planted again.

## Reset and recovery

`StaticTrackerlessAvatarSolver::Reset` clears all fixed-size inferred state. Avatar bind, unbind, neutral recalibration, VRM replacement, tracking restoration, large HMD discontinuity, significant tracking-origin translation/yaw change, and non-finite solver state all reset or reseed yaw, pelvis, feet, active steps, airborne state, and elbow/knee histories. This prevents stale world anchors from surviving scene/origin discontinuities.

## Tuning and diagnostics

`BodySolverTuning.hpp` contains every principal body threshold and rate under a named field. Most distance thresholds are fractions of measured leg reach, hip width, or calibrated eye height. Values are independently selected SaberStage starting points and must be tuned from Quest recordings; they are not copied from Basis, RenIK, Qavatars, or RootMotion Final IK.

The existing on-demand diagnostic action now logs body mode, yaw state/error/estimate, pelvis, lean, crouch, body translation, both foot states/anchors/ideal positions/reasons/destinations/progress, leg reach, knee poles, solve time, solve count, Transform reads/writes, FABRIK passes/error, and limb reachability. Normal frames do not emit these logs.

## Performance and validation status

The hot path uses only fixed-size arrays and trivially copyable persistent values. It performs no bone discovery, reflection, physics query, or allocation during a solve. Host allocation instrumentation covers both the initial and a dynamic body/step update. Normal runtime input remains three cached pose reads and one write for each mapped/solved bone (normally about 20-24).

Host tests cover calibration, FABRIK bounds/lengths, analytic limb reach/softening, direct upper-body regression, duplicate-sequence rejection, yaw lock/dwell/turn behavior, lean without skating, crouch-versus-forward-bend, planted anchors, one-foot-at-a-time stepping, support-foot stability, step lift/landing, explicit pivot steps, airborne entry/landing, deterministic reset/reseed, and zero heap allocations. The Android ARM64 library also compiles.

Black Heart head and arms were verified in an actual Beat Saber map before this pass. The new torso, pelvis, legs, steps, and airborne behavior have not yet been visually validated or timed on Quest; use the requested in-game matrix and recordings before treating these starting values as final.

## ozz provenance

`src/avatar/TwoBoneIK.cpp` is marked `MIT_OZZ_DERIVED`. It adapts only ozz-animation's MIT-licensed target-softening curve and analytic two-bone construction from revision `744eb9d99f606eda849acb0b1204f7a3dc20bca1`. The complete MIT notice is present both in that file and in `docs/THIRD_PARTY_NOTICES.md`.

SaberStage changes the implementation to scalar native math, world-space joint positions, fixed value inputs/outputs, and its own pole-plane construction. It imports no ozz containers, jobs, matrices, SIMD layer, skeleton runtime, or animation framework. No Qavatars FinalIK, RootMotion Final IK, Basis, or RenIK source or fitted model was used.
