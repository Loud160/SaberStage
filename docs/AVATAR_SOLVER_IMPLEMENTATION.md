# Static trackerless avatar solver implementation

## Scope and current stop point

This branch implements the requested solver foundation through the static planted-lower-body and pelvis lean/crouch milestone. It deliberately stops before body-yaw states, procedural steps, turn steps, jumping, and optional FBT constraints.

This is a solver/framework increment, not a VRM loader. `AvatarManager::BindHumanoidAnimator` is the explicit handoff from a future asset loader. Until a loader supplies a humanoid `Animator`, SaberStage starts the solver runtime but does not display an avatar or run its hot path.

## Native architecture

```text
PlayerTransforms head/left hand/right hand
        |
        v
three cached GetPositionAndRotation calls
        |
        v
TrackingSample (fixed-size native values)
        |
        v
StaticTrackerlessAvatarSolver
  - measured rest-pose scale
  - pelvis lean/crouch estimate
  - bounded FABRIK spine
  - shoulders and stateful elbow poles
  - two-bone arms
  - fixed foot anchors and two-bone legs
        |
        v
SolvedHumanoidPose (enum-indexed fixed array)
        |
        v
one SetPositionAndRotation per mapped bone
```

`SolverPersistentState` contains only the previous elbow/knee pole directions, fixed foot anchors, foot rotations, and solve/frame stamps. The solver uses no dynamically growing containers and performs no heap allocation.

## Calibration measurements

Humanoid binding records each mapped bone's original local and world pose and measures:

- left/right upper- and lower-arm length;
- left/right thigh and lower-leg length;
- shoulder and hip width;
- every available hips-to-head spine segment;
- neck-to-head offset;
- left/right foot-to-toe length when toe bones exist;
- measured rest elbow/knee bend directions;
- eye position from both eye bones when present, falling back to the head bone origin;
- eye height relative to the lower mapped foot/toe skeletal point.

Player calibration samples one neutral standing pose: HMD height above the Beat Saber tracking origin, floor height, tracking-origin facing, and optional controller-to-wrist offsets. Avatar scaling is `standing HMD height / avatar skeletal eye height`; mesh bounds and fitted anatomical constants are not used.

## Solver behavior

The spine is a standard fixed-length FABRIK chain with two normal forward/backward passes, at most three passes, a small rest-relative initialization bend, and a root shift bounded by measured lower-leg length. Head rotation is exact after the positional solve. Twist influence is distributed by accumulated measured chain length.

Elbow poles project both persistent history and the measured rest pole into the current shoulder-to-hand plane. Rest direction has more influence while bent; history dominates near extension. A projected controller-forward cue contributes at most ten percent and only while bent. Shoulder translation is capped by measured shoulder width.

Foot anchors are initialized from the calibrated world-space stance and do not step. Both legs use the same two-bone primitive as the arms. Pelvis horizontal support and vertical crouch limits are scaled from measured hip width and leg reach. Forward displacement smoothly increases hinge behavior without a fitted squat polynomial.

## ozz provenance

`src/avatar/TwoBoneIK.cpp` is marked `MIT_OZZ_DERIVED`. It adapts only ozz-animation's MIT-licensed target-softening curve and analytic two-bone construction from revision `744eb9d99f606eda849acb0b1204f7a3dc20bca1`. The complete MIT notice is present both in that file and in `docs/THIRD_PARTY_NOTICES.md`.

SaberStage changes the implementation to scalar native math, world-space joint positions, fixed value inputs/outputs, and its own pole-plane construction. It imports no ozz containers, jobs, matrices, SIMD layer, skeleton runtime, or animation framework. No Qavatars FinalIK, RootMotion Final IK, Basis, or RenIK implementation or fitted model was used.

## Ordering and diagnostics

Normal frame ordering is tracking sample in `Update`, native solve and bone writes in `LateUpdate`. A custom component attached only to the SaberStage spectator camera invokes an explicit fresh sample/solve/write during `OnPreCull`, immediately before both preview `Camera.Render()` and camera-driven recording renders.

Diagnostics retain calibrated measurements, target poses, pelvis and shoulder targets, elbow/knee poles, FABRIK error/pass count, limb reachability, solves per Unity frame, three Transform reads, and the actual number of mapped Transform writes. Calibration and mapped bones are logged once at bind; high-volume per-frame logging is not enabled.

## Validation status

Host tests cover calibration geometry, eye-height scaling inputs, FABRIK length preservation and pass bounds, reachable/unreachable two-bone behavior, pole hemisphere selection, complete static solving, duplicate-sequence rejection, crouch pelvis movement, planted-foot persistence, and zero heap allocations during a complete native solve.

The ARM64 Quest library compiles. On-headset visual validation is blocked on the separate VRM asset-loader/binding increment; deploying this solver alone would not produce an avatar to inspect.
