# Arm, Elbow, Wrist, and Grip IK Reference Data

## Purpose and status

This document is the handoff record for improving SaberStage's Quest arm solver from measured PC avatar behavior. It is intentionally self-contained: a new development conversation should be able to start here, locate the evidence, understand what has already been concluded, and continue without relying on chat history.

This is a planning and evidence document. It does **not** claim that the proposed elbow model has been implemented. The repository state examined for this write-up was branch `Avatar-Framework` at commit `09ca3ad9e949375898189e222690612991127f31` on August 30, 2026. The working tree also contained later uncommitted work, so symbols and behavior must be rechecked before implementation.

## Resume brief

The current Quest solver already has a sound analytic two-bone arm solution and keeps a live saber handle as an authoritative hand-position target. The weak point is its elbow bend-plane predictor. The current `StableElbowPole()` begins from a mostly fixed outward/downward torso-local direction, blends rest and prior-frame history, and applies only a small controller-orientation cue. This is stable, but it cannot reproduce the way a convincing PC avatar changes elbow placement as a hand moves across the chest, changes height, or moves closer to and farther from the torso.

The captured PC data supports the following replacement model:

> **Elbow position = analytic two-bone arm geometry + position-driven bend-plane goal + limited controller-roll bias + temporal continuity and anatomical constraints.**

The strongest measured relationship is between normalized reach and elbow flexion. Hand position relative to the shoulder and torso should determine the elbow bend plane. Controller orientation should remain a filtered, clamped secondary influence. Wrist orientation should continue to follow the controller/saber target independently of the elbow calculation.

## Evidence boundary

The PC analyzer treats FinalIK as a black box. It records observable tracking targets entering the PC avatar system and the resulting humanoid transforms after the avatar has been posed. It does not read, decompile, translate, patch, or redistribute FinalIK source code.

The resulting relationships are empirical observations, not a disclosure or reconstruction of FinalIK internals. They are useful for designing and validating SaberStage's independently implemented Quest solver.

The PC analyzer implementation and methodology are documented in:

- [`tools/pc-pose-analyzer/README.md`](../tools/pc-pose-analyzer/README.md)
- [`tools/pc-pose-analyzer/AnalyzerHost.cs`](../tools/pc-pose-analyzer/AnalyzerHost.cs)
- [`tools/pc-pose-analyzer/AvatarBinding.cs`](../tools/pc-pose-analyzer/AvatarBinding.cs)
- [`tools/pc-pose-analyzer/PoseSampler.cs`](../tools/pc-pose-analyzer/PoseSampler.cs)
- [`tools/pc-pose-analyzer/PoseModels.cs`](../tools/pc-pose-analyzer/PoseModels.cs)
- [`tools/pc-pose-analyzer/PoseSessionWriter.cs`](../tools/pc-pose-analyzer/PoseSessionWriter.cs)
- [`tools/pc-pose-analyzer/camera2/SaberStage IK Diagnostic.json`](../tools/pc-pose-analyzer/camera2/SaberStage%20IK%20Diagnostic.json)

## Primary datasets and analysis files

These files are intentionally not duplicated into the repository because the raw session is approximately 341 MB. They are the authoritative local references for this analysis.

### Raw PC pose session

```text
C:\Users\Owner\BSManager\BSInstances\1.37.1\UserData\SaberStagePoseAnalyzer\20260830-184316\pose-data.jsonl
```

- Size: 341,339,375 bytes.
- Input lines: 19,201.
- Full-run pose samples: 18,837.
- Analyzer version: `0.1.0+09ca3ad9e949375898189e222690612991127f31`.
- PC Custom Avatars version: `5.4.1.0`.
- Configured continuous sample rate: 30 Hz.
- Coordinates: torso-local Unity meters, `+X` avatar right, `+Y` avatar up, and `+Z` avatar forward.

### Analysis workbook

```text
C:\Users\Owner\Documents\ChatGPT\Quest 2\outputs\saberstage-pose-analysis-20260830\SaberStage_FinalIK_Black_Box_Analysis.xlsx
```

- Size: 8,842,319 bytes.
- `Overview`: source metadata and evidence-backed conclusions.
- `Run Comparison`: run duration, sampling, movement ranges, speeds, and orientation summaries.
- `Elbow and Grip`: input/output correlations and reach-bin summaries.
- `1s Time Series`: one-second aggregate motion traces.
- `All Samples`: all 18,837 full-run samples and calculated fields.
- `Data Dictionary`: field definitions, calculations, coordinate convention, and limitations.

The workbook was read using the project spreadsheet-analysis tooling during this documentation pass. It was not modified.

## Recorded runs

| Run | Grip | Duration | Samples | Effective rate | Notes |
| --- | --- | ---: | ---: | ---: | --- |
| 1 | Default Quest 2 grip | 248.99 s | 6,434 | 25.84 Hz | Known to limit full-arm range and favor wrist flicks or full extension. |
| 2 | Claw grip | 239.08 s | 6,296 | 26.33 Hz | More wrist action and faster, easier direction changes. |
| 3 | Claw grip | 230.36 s | 6,107 | 26.51 Hz | A second natural-play claw sample with different choreography. |

Run 1 covered approximately 326.08–575.07 seconds in the session, Run 2 approximately 587.78–826.87 seconds, and Run 3 approximately 844.45–1074.81 seconds.

These are three different maps. They provide broad natural-play motion but are not a controlled same-map grip comparison. Grip, map difficulty, choreography, and player behavior are confounded.

## Motion and grip observations

| Run | Median hand speed L/R | P95 hand speed L/R | Median controller-roll proxy L/R |
| --- | --- | --- | --- |
| 1, default | 0.759 / 0.774 m/s | 3.789 / 3.917 m/s | 94.36° / -98.10° |
| 2, claw | 1.179 / 1.236 m/s | 4.787 / 5.475 m/s | 158.75° / -149.22° |
| 3, claw | 1.115 / 1.146 m/s | 4.463 / 5.197 m/s | 144.79° / -134.26° |

The controller-roll proxy is `atan2(targetRight.y, targetUp.y)` in the torso-local frame. It is a useful comparison signal, not a guaranteed native controller Euler angle.

Important observations:

1. Grip orientation is clearly visible in the controller input.
2. The resulting PC wrist orientation follows the tracked target orientation to numerical precision.
3. The claw-grip runs contain higher hand speeds, but different maps prevent attributing the entire difference to grip.
4. Elbow placement is not explained by controller roll alone.
5. The analyzer could observe output finger bones, but no true per-finger input was available. The recorded input fallback curl was `1.0`, so this dataset cannot establish a finger-tracking model.

## Correlation evidence

The values below are Pearson correlations from the workbook. They indicate linear association, not causation or a complete solver formula.

### Elbow flexion

| Run/side | Hand X | Hand Y | Hand Z | Reach | Roll proxy | Hand speed |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Run 1 left | -0.245 | -0.563 | 0.067 | **0.969** | 0.618 | 0.227 |
| Run 1 right | 0.050 | -0.553 | -0.037 | **0.965** | -0.622 | 0.184 |
| Run 2 left | -0.379 | -0.420 | 0.040 | **0.966** | 0.357 | 0.334 |
| Run 2 right | 0.092 | -0.293 | 0.206 | **0.955** | -0.089 | 0.394 |
| Run 3 left | -0.196 | -0.149 | 0.250 | **0.966** | -0.009 | 0.394 |
| Run 3 right | 0.308 | -0.186 | 0.061 | **0.963** | 0.136 | 0.330 |

Reach is consistently the dominant flexion signal. This is expected for an analytic two-bone chain: once upper-arm length, forearm length, shoulder position, and hand target are known, the bend magnitude is geometrically constrained.

### Elbow-pole vertical component

| Run/side | Hand X | Hand Y | Hand Z | Reach | Roll proxy | Hand speed |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Run 1 left | -0.271 | -0.686 | -0.339 | 0.720 | 0.708 | 0.083 |
| Run 1 right | 0.090 | -0.716 | -0.465 | 0.671 | -0.792 | 0.006 |
| Run 2 left | -0.331 | -0.616 | -0.292 | 0.701 | 0.593 | 0.183 |
| Run 2 right | 0.010 | -0.596 | -0.344 | 0.461 | -0.594 | 0.145 |
| Run 3 left | -0.214 | -0.422 | -0.182 | 0.598 | 0.360 | 0.175 |
| Run 3 right | -0.011 | -0.485 | -0.411 | 0.431 | -0.418 | 0.028 |

Hand height, forward distance, reach, and controller roll all contribute. The varying roll relationship between grips and runs is why roll must be a secondary, bounded signal rather than a direct elbow-pole mapping.

## Physical behavior the model should reproduce

The intended elbow behavior discussed during analysis is:

- A hand raised near its same-side shoulder normally leaves the elbow pointing mostly downward.
- Moving that hand inward toward the center of the chest raises and flares the elbow outward.
- With a hand near the shoulder-height center line and close to the chest, the elbow may approach a roughly horizontal bend.
- Moving the hand farther forward opens the shoulder-hand-elbow triangle and generally lets the elbow fall lower.
- At waist or lower-chest height, the preferred PC behavior often forms a visible triangle between upper arm, forearm, and torso rather than pinning the elbow straight down.
- Cross-body reaches must remain anatomically plausible and must not let the elbow flip behind the shoulder or through the torso.
- Near full extension, elbow direction becomes underconstrained. History and a stable anatomical prior must dominate instead of allowing a frame-to-frame flip.
- Wrist/controller rotation matters, but rapid saber cuts should not whip the elbow around. Only the slower, anatomically useful component should bias the elbow bend plane.
- The wrist must not invert or bend backward beyond its anatomical range. Its positional endpoint remains the tracked saber grip.

These are qualitative expectations that should be validated against the recorded output and on-headset video, not treated as literal fixed angles for every avatar.

## Current Quest implementation to inspect

Primary solver files:

- [`include/saberstage/avatar/AvatarSolver.hpp`](../include/saberstage/avatar/AvatarSolver.hpp)
- [`include/saberstage/avatar/PoseTypes.hpp`](../include/saberstage/avatar/PoseTypes.hpp)
- [`include/saberstage/avatar/BodySolverTuning.hpp`](../include/saberstage/avatar/BodySolverTuning.hpp)
- [`src/avatar/AvatarSolver.cpp`](../src/avatar/AvatarSolver.cpp)
- [`src/avatar/TwoBoneIK.cpp`](../src/avatar/TwoBoneIK.cpp)
- [`src/avatar/Calibration.cpp`](../src/avatar/Calibration.cpp)
- [`src/avatar/calibration/PlayerCalibrationProfile.cpp`](../src/avatar/calibration/PlayerCalibrationProfile.cpp)
- [`docs/AVATAR_SOLVER_IMPLEMENTATION.md`](AVATAR_SOLVER_IMPLEMENTATION.md)
- [`docs/PLAYER_CALIBRATION.md`](PLAYER_CALIBRATION.md)

At the examined snapshot:

- `SolveArm()` calculates shoulder assistance, bounded arm stretch, the elbow pole, analytic two-bone geometry, exact saber-hand position, grip-to-hand orientation, and a wrist-deviation clamp.
- `StableElbowPole()` starts from a hard-coded torso-local outward/downward prior of approximately `(±0.35, -1.0, -0.12)`.
- It blends the rest pose with prior-frame history, increasingly favoring history near extension.
- A controller-forward cue contributes only about ten percent of the available bend-dependent blend.
- A signed cone with a minimum dot of `0.60` prevents the elbow from crossing the intended outward/downward hemisphere.
- The current wrist-deviation defaults are 70° for ordinary controller/menu tracking and 105° for a tracked gameplay saber grip.
- A tracked saber remains an authoritative positional target even if measured arm length is insufficient.

Do not remove the analytic two-bone solver. Its MIT-derived provenance is already documented in `src/avatar/TwoBoneIK.cpp` and `docs/THIRD_PARTY_NOTICES.md`. The proposed work changes the bend-plane goal and continuity rules around it.

## Proposed elbow-pole model

### 1. Build torso-relative inputs

For each arm and frame, calculate stable normalized inputs:

- hand lateral displacement from its shoulder and from torso center;
- hand height relative to shoulder, chest, and pelvis;
- hand forward distance from the chest plane;
- shoulder-to-hand reach divided by current solved arm length;
- whether the hand is same-side, near the center line, or cross-body;
- controller roll around the hand/forearm axis;
- hand speed and angular velocity;
- current elbow-pole history and distance from full extension.

Use a mirrored canonical representation so one function can handle both arms without accidentally giving left and right different behavior.

### 2. Keep analytic flexion

Use the current two-bone solution to determine elbow bend magnitude. Reach already explains the recorded flexion extremely well. Do not replace this with a statistical flexion guess.

### 3. Predict a position-driven bend plane

Create a desired pole direction from hand position:

- a low or strongly forward hand biases the elbow downward;
- an inward, chest-adjacent hand increases outward flare and raises the elbow;
- a high, same-side hand retains more downward bias;
- a cross-body hand receives a controlled outward/forward solution that cannot cross through the torso;
- the strength of position cues fades near full extension, where the bend plane is poorly constrained.

This can begin as a compact piecewise or smoothly blended model. A later regression fit may replace coefficients, but the implementation should keep the physical terms named and inspectable rather than hiding them in an opaque high-order formula.

### 4. Add limited roll bias

Controller roll may rotate the position-driven pole within a safe cone, but it must be:

- low-pass filtered;
- gain-limited;
- angular-velocity limited;
- reduced during fast saber cuts;
- reduced near full extension;
- allowed to adapt to grip style without requiring a hard-coded “claw grip” mode.

The dataset shows that grip materially changes the roll distribution. A single direct roll-to-elbow rule would overfit one grip and fail another.

### 5. Enforce continuity and anatomy

The final pole must include:

- hysteresis around side/center-line transitions;
- maximum pole rotation per second;
- prior-frame continuity;
- a strict anatomical bend hemisphere;
- explicit rejection of elbow-behind-shoulder and elbow-through-torso states;
- a near-extension singularity policy that freezes or slowly relaxes toward the anatomical prior instead of flipping;
- reset/reseed logic when avatars, calibration, tracking source, or gameplay/menu context changes.

### 6. Keep wrist and finger concerns separate

The PC wrist follows the tracked target orientation. SaberStage should keep the hand on the saber and apply its existing anatomical wrist clamp, improving the clamp only if on-headset evidence still shows wrist inversion.

The current dataset does not contain true finger input. A closed saber grip can be implemented from the avatar's humanoid finger rest pose and a deterministic grip profile, but it should not be presented as inferred from these captures.

## Offline replay and fitting plan

The raw JSONL session should become a deterministic offline replay input for candidate elbow-pole functions. The replay tool should run the same predictor used by the Quest solver, or a source-shared pure-math equivalent, against every captured frame.

Required metrics:

- mean, median, P95, and maximum elbow-position error;
- bend-plane angular error;
- left/right signed hemisphere crossings;
- pole flips and angular velocity spikes;
- wrist orientation error;
- error by reach bin;
- error by hand-height, lateral-position, and forward-distance bins;
- behavior within the near-extension band;
- per-run and per-grip results rather than only one aggregate score.

The validation objective is not “match every PC frame at any cost.” The Quest result must be smooth, anatomically valid, fast enough for gameplay, and robust outside these three maps.

## Additional data collection that would improve the model

The current data is sufficient for a meaningful first improvement. It is not sufficient for universal coefficients or an exact behavioral clone.

A larger dataset should record:

- anonymous player ID;
- grip style for each hand;
- headset and controller model;
- player standing eye height and arm span;
- dominant hand;
- avatar identifier and measured body proportions;
- PC avatar calibration mode and resulting runtime scale;
- controller-to-grip offsets;
- map, difficulty, modifiers, practice speed, and analyzer/game versions;
- repeated standardized motion sequences;
- at least one common reference map across players;
- natural gameplay maps covering different choreography;
- same-player, same-map repeats with different grips when practical.

For statistical fitting, split validation by player, not by individual frame. Otherwise adjacent frames from the same person and map will leak into both training and validation and make the model look more general than it is. Treat player and map as separate effects; grip can be an explicit effect or an inferred orientation distribution.

No physical elbow tracker was present. The PC output is a reference behavior, not ground-truth human elbow position. If future users provide elbow or full-body trackers, those captures should be kept as a separate ground-truth dataset rather than mixed invisibly with black-box PC output.

## Implementation order

1. Add a pure, testable canonical input structure for one arm.
2. Add raw-session replay/import tooling and baseline the existing `StableElbowPole()` against the workbook session.
3. Implement the position-first desired pole while retaining current analytic flexion.
4. Add bounded roll influence.
5. Add continuity, singularity, and anatomical rejection rules.
6. Add diagnostic counters for flips, cone clamps, and near-extension holds.
7. Validate offline against all three runs.
8. Build and test on Quest in menu/world-preview mode before gameplay.
9. Record gameplay from several map styles and compare elbows, wrists, saber grip attachment, and frame cost.
10. Tune only after the structural model is correct; do not stack per-pose patches on the old fixed pole.

## Acceptance criteria

- Elbows never reverse their hinge direction or bend behind the shoulder.
- Hands remain attached to authoritative saber grips.
- Wrist orientation responds to the controller without visually flipping.
- Close-to-chest, center-line, shoulder-height, waist-height, forward-reach, overhead, and cross-body poses transition smoothly.
- No discontinuity occurs when crossing the torso center line.
- Near full extension does not produce elbow popping or side changes.
- Default and claw grip both look plausible without a grip-specific manual switch.
- Offline metrics improve over the current fixed-pole baseline on all three runs, not only the aggregate dataset.
- Quest CPU cost remains negligible relative to avatar skinning and rendering.

## Decisions already made

- Keep the analytic two-bone solver for bend magnitude.
- Use hand position and reach as primary elbow inputs.
- Use controller roll as a filtered secondary input.
- Preserve wrist/controller target behavior separately from elbow placement.
- Treat FinalIK only as a black-box behavioral reference.
- Do not claim the captured PC output is physical elbow ground truth.
- Do not blindly copy or translate FinalIK source.

## Decisions still open

- Exact input normalization and coefficient form for the position-driven pole.
- Whether the first implementation is hand-authored piecewise blending, a small fitted model, or a fitted model converted to readable terms.
- Exact near-extension threshold and pole angular-velocity limit.
- Whether grip adaptation remains implicit or uses optional calibration metadata.
- Whether a future tracker dataset should supersede or merely supplement the PC reference behavior.
