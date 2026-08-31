# Arm-Span Scaling and Height Retargeting Plan

## Purpose and status

This document records the complete design discussion for changing SaberStage from height-based uniform avatar scaling to arm-span-based scaling, with an optional vertical skeletal correction that compresses or stretches the avatar to the player's height without changing the arm fit.

It is written as a self-contained handoff for a future development conversation. It describes the desired behavior, current implementation, formulas, UI, persistence, solver consequences, risks, and testing needed to implement the feature correctly.

This design is implemented on branch `Avatar-Framework` as an on-device test candidate. Host tests and the Quest build pass. Visual behavior across unusually proportioned avatars, the settings UI, and profile switching still require the Quest 2 test matrix below before the work should be treated as complete.

## Implementation status (August 30, 2026)

- T-pose calibration now derives a versioned player arm span and confidence value. Compatible version-2 profiles are refitted from their saved source captures rather than discarded or trusted with stale derived values.
- Avatar rest geometry supplies the matching skeletal arm span used for scale selection.
- The neutral avatar uses arm-span uniform scaling when the player and avatar measurements are credible. Invalid or low-confidence measurements use the bounded legacy height-scale fallback.
- The avatar's feet remain on the calibrated floor, while HMD translation is applied relative to the neutral player pose instead of forcing the neutral avatar head to the HMD's absolute world position.
- `Match Player Height` is off by default. When enabled, only vertical lower-body and torso segments receive the bounded residual height correction; arms, shoulder width, hands, and fingers are unchanged.
- `Height Balance` distributes that correction between the legs and torso using the locked `Legs  ←  Even  →  Torso` direction.
- Leg, spine, crouch, lean, stepping, and reach-dependent measurements are rebuilt from the corrected neutral skeleton and the solver is reseeded whenever fit settings change.
- Match-height and balance settings are stored independently for each avatar within a player's Avatar settings.
- Shared-Quest player profiles own only player calibration and the complete Avatar settings group. Camera, recording, preview, broadcast, and other SaberStage settings remain shared.
- Fit diagnostics record the player span, avatar span, confidence, selected scale source, uniform scale, residual height correction, and effective post-retarget dimensions.

Safety bounds in the current implementation are a 0.55-1.80 uniform scale, a 0.70-1.30 per-region vertical scale, a maximum correction of the smaller of 0.45 metres or 28 percent of natural avatar height, and a 0.55 player-span confidence threshold. Invalid geometry or non-finite results fall back to the existing height-based scale without blocking avatar loading.

Validation completed before on-device testing includes the host solver, calibration, settings migration/profile, camera, recording, and VRM test suites plus the ARM64 Quest build, including BSML UI compilation and the existing avatar-shader asset. Quest 2 visual and interaction testing remains required.

## Resume brief

SaberStage currently scales an avatar uniformly from standing HMD eye height:

```text
uniformScale = playerStandingHmdHeight / avatarSkeletalEyeHeight
```

That aligns the avatar head to the player's head but can leave the avatar's hands unable to reach the tracked saber handles without shoulder assistance or visible arm stretching. For a third-person avatar, the saber handles are the hard visual constraint. The rest of the avatar does not have to occupy the same absolute body dimensions as the player.

The intended replacement is:

1. Uniformly scale the avatar from player arm span so its natural hand reach matches the player and the sabers remain in the hands.
2. Keep the avatar's feet on the game floor.
3. Treat HMD motion as a relative motion source, rather than forcing the avatar's head to the HMD's absolute world position.
4. Add a `Match Player Height` toggle. When enabled, correct the remaining height difference by adjusting vertical skeletal segment lengths without changing shoulder width or arm lengths.
5. Add a `Height Adjustment Balance` slider:
   - **Left:** move more compression/stretch into the lower body and legs.
   - **Center:** distribute correction proportionally across upper and lower body.
   - **Right:** move more compression/stretch into the upper body and torso.

The slider direction above is a locked requirement. Use an explicit UI caption such as `Legs  ←  Even  →  Torso` so it cannot be implemented backward.

## Why arm span is the base scale

The avatar is primarily a third-person visual representation. Its head does not technically have to occupy the exact HMD world position for believable movement. Head translation and rotation can be retargeted as deltas from a neutral pose.

The hands are different: they visibly hold game sabers whose world-space poses are controlled by tracked controllers. A mismatch between avatar arm span and player reach forces shoulder translation, arm stretching, or visible separation between hand and saber. Scaling from arm span solves the most important hard endpoint before the rest of the body is retargeted.

This intentionally differs from a strict one-to-one physical overlay. Exact absolute head alignment is still important for first-person full-body presence, wall collision, and tracker alignment, but SaberStage's recording/avatar use case can prioritize convincing third-person movement and saber attachment.

## Existing PC reference behavior

The current public PC Custom Avatars implementation offers separate resize modes. It does not reconcile arm span and height simultaneously:

- Arm-span mode: `playerArmSpan / avatarArmSpan`.
- Height mode: `playerEyeHeight / avatarEyeHeight`.

Relevant public source:

- [Beat Saber Custom Avatars repository](https://github.com/nicoco007/BeatSaberCustomAvatars)
- [`PlayerAvatarManager.cs`](https://github.com/nicoco007/BeatSaberCustomAvatars/blob/main/Source/CustomAvatar/Player/PlayerAvatarManager.cs), particularly `CalculateAvatarScale()`.
- [`ArmSpanMeasurer.cs`](https://github.com/nicoco007/BeatSaberCustomAvatars/blob/main/Source/CustomAvatar/UI/ArmSpanMeasurer.cs).

`ArmSpanMeasurer` samples the distance between the two tracked hand transforms every 0.1 seconds, smooths the measurement, and completes after the value remains stable for approximately three seconds. This simple method is useful as a UX reference, but SaberStage may derive a more robust arm-span estimate from its existing guided calibration captures.

The PC pose session used for the arm analysis did not contain a complete PC calibration timeline or a scale transition. The PC calibration conclusion comes from the public observable implementation above, not from the three gameplay motion runs.

### Related local analyzer evidence

The analyzer is already capable of recording authored avatar eye height and arm span, runtime/absolute scale, tracked head height relative to the avatar root, controller-to-controller span, output wrist span, and independent height- and arm-span-derived scale candidates. Its implementation and capture procedure are in [`tools/pc-pose-analyzer/README.md`](../tools/pc-pose-analyzer/README.md) and the adjacent analyzer source files.

The existing August 30 session and workbook are useful for arm-motion behavior, but they are **not** a complete calibration dataset:

```text
Raw session:
C:\Users\Owner\BSManager\BSInstances\1.37.1\UserData\SaberStagePoseAnalyzer\20260830-184316\pose-data.jsonl

Analysis workbook:
C:\Users\Owner\Documents\ChatGPT\Quest 2\outputs\saberstage-pose-analysis-20260830\SaberStage_FinalIK_Black_Box_Analysis.xlsx
```

If direct PC calibration comparison is needed during implementation, record a new session that starts before selecting the PC resize mode, includes the full arm-span or height calibration operation, and ends only after the avatar's runtime scale has stabilized. Keep that calibration capture separate from natural-gameplay motion runs.

## Current SaberStage behavior

Relevant files:

- [`src/avatar/AvatarSolver.cpp`](../src/avatar/AvatarSolver.cpp)
- [`src/avatar/Calibration.cpp`](../src/avatar/Calibration.cpp)
- [`include/saberstage/avatar/PoseTypes.hpp`](../include/saberstage/avatar/PoseTypes.hpp)
- [`include/saberstage/avatar/BodySolverTuning.hpp`](../include/saberstage/avatar/BodySolverTuning.hpp)
- [`src/avatar/calibration/PlayerCalibrationProfile.cpp`](../src/avatar/calibration/PlayerCalibrationProfile.cpp)
- [`include/saberstage/avatar/calibration/PlayerCalibrationProfile.hpp`](../include/saberstage/avatar/calibration/PlayerCalibrationProfile.hpp)
- [`docs/AVATAR_SOLVER_IMPLEMENTATION.md`](AVATAR_SOLVER_IMPLEMENTATION.md)
- [`docs/PLAYER_CALIBRATION.md`](PLAYER_CALIBRATION.md)

At the examined snapshot:

- `AvatarScale()` returns `standingHmdHeight / avatar.eyeHeight`.
- `BuildNeutralPose()` uniformly scales each rest-world bone position around the avatar eye and anchors the avatar eye at `player.neutralHead.position`.
- Live solving derives the head target from the absolute tracked HMD pose.
- Advanced calibration measures grip offsets, effective reach, lean, step, crouch, and turn behavior.
- Advanced calibration does not currently change whole-avatar scale.
- Effective reach affects shoulder assistance and bounded arm compensation.
- The default arm stretch limit is 1.05; a directly tracked gameplay saber may use an emergency limit of 1.10.

Implementation must audit every use of `AvatarScale()` and `standingHmdHeight`. Those values currently influence more than neutral pose construction, including leg reach, stance, stepping, airborne thresholds, crouch/lean normalization, and diagnostics.

## Required settings and UX

### Base scale policy

The planned base behavior is `Scale Avatar By Arm Span`. If a selectable policy is retained for compatibility, arm span should be a distinct explicit mode rather than silently changing the meaning of the current height scale.

### Match Player Height

- Type: toggle.
- Off: use the avatar's natural height after uniform arm-span scaling.
- On: apply vertical skeletal correction so the avatar's eye/standing height matches the player target while preserving the arm-span scale for width and arms.
- When off, the balance slider must be visibly disabled and have no solver effect.

### Height Adjustment Balance

- Type: centered slider with a reset glyph.
- Suggested normalized range: `-1.0` to `+1.0`.
- `-1.0`: strongest allowed emphasis on legs/lower body.
- `0.0`: even/proportional distribution between lower and upper stretchable chains.
- `+1.0`: strongest allowed emphasis on torso/upper body.
- Suggested visible caption: `Legs  ←  Even  →  Torso`.
- Reset value: `0.0`.
- Tooltip: changing balance does not change the requested final avatar height; it changes where the necessary compression or stretch is applied.

The settings should be stored per avatar. A long-legged avatar and a short-legged avatar need different correction balances, and a user should not have to retune one after selecting the other.

## Measurement requirements

### Avatar arm span

Use authoritative humanoid rest-pose geometry. A practical skeletal measure is the distance from one hand/grip reference through the shoulder chain to the other hand/grip reference, or an equivalent sum of:

```text
left arm reach + shoulder width + right arm reach
```

The exact measure must use the same grip/hand endpoint semantics as the solver. Do not mix fingertip span with wrist or saber-grip span without a documented correction.

The implemented shoulder-chain width is the distance between the left and right upper-arm joints. It must not use the distance between the VRM `LeftShoulder` and `RightShoulder` clavicle pivots: some avatars place those pivots close to the chest while the clavicle-to-upper-arm segments contain most of the shoulder reach. Omitting that reach understates the avatar arm span and can incorrectly drive uniform scale to its upper safety clamp.

### Player arm span

The existing calibration already captures multiple arm poses and controller/grip transforms. Prefer a stable estimate from accepted extended-arm captures rather than one instantaneous controller-to-controller distance.

Candidate procedure:

1. Ask the player to extend both arms naturally in a T pose or the closest comfortable equivalent.
2. Measure between the calibrated grip/hand endpoints, not raw controller origins.
3. Collect several samples after the countdown.
4. Reject motion, asymmetric reach, and insufficient extension.
5. Use a median or robust trimmed estimate.
6. Record confidence and the source calibration version.

If basic calibration needs to remain simple, the stable-measurement behavior in PC `ArmSpanMeasurer.cs` is a reasonable UX reference.

### Player height target

The current neutral calibration records standing HMD height above the tracking floor. Unless a later design adds a user-entered target, this remains the height correction target:

```text
playerTargetEyeHeight = neutralHmdY - calibratedFloorY
```

Standing pose quality matters. Store the measured target with confidence and allow recalibration/reset.

## Scale and correction formulas

### Step 1: uniform arm-span scale

```text
armScale = playerArmSpan / avatarArmSpan
naturalArmScaledEyeHeight = avatarEyeHeight * armScale
heightDelta = playerTargetEyeHeight - naturalArmScaledEyeHeight
```

If `Match Player Height` is off, stop here. The avatar retains its arm-span-scaled natural height.

### Step 2: divide the vertical skeleton into correction regions

Define stretchable lower-body length `L` and upper-body length `T` from authoritative rest geometry after arm scaling:

- `L`: floor/foot reference through shins and thighs to the hips.
- `T`: hips through spine/chest/neck to the eye reference.

Do not include foot length as a vertical stretch segment. Avoid scaling the skull/head geometry. Upper-body correction should primarily affect pelvis-to-spine, spine-to-chest, chest-to-neck, and related vertical offsets.

### Step 3: center distribution

At slider center, distribute the height delta proportionally so both regions receive the same scale factor:

```text
centerLegWeight = L / (L + T)
centerTorsoWeight = T / (L + T)
```

“Even” means even/proportional scaling pressure, not necessarily an equal number of centimeters added to legs and torso.

### Step 4: bias the distribution

Let `balance` be in `[-1, +1]`, where negative means legs and positive means torso. Map it to a bounded leg weight:

```text
legWeight = BiasAround(centerLegWeight, balance, safeMinimumLegWeight, safeMaximumLegWeight)
torsoWeight = 1 - legWeight

legDelta = heightDelta * legWeight
torsoDelta = heightDelta * torsoWeight

legScale = 1 + legDelta / L
torsoScale = 1 + torsoDelta / T
```

The exact bias curve remains an implementation decision. It should be smooth around zero and should never drive either region beyond safe anatomical scale bounds.

The total must remain invariant:

```text
legDelta + torsoDelta = heightDelta
```

Moving the slider changes the distribution, not the final target eye height.

## Skeletal retargeting rules

Do **not** implement this as raw Unity nonuniform root scaling such as:

```text
root.localScale = { armScale, heightScale, armScale }
```

That would distort vertical arm components, hands, face, clothing, outlines, colliders, spring bones, and other imported avatar data. It can also recreate the arm mismatch that arm-span scaling was meant to solve.

Instead, build a corrected neutral skeleton:

1. Apply `armScale` uniformly to all avatar rest geometry.
2. Keep shoulder width, clavicle width, upper-arm length, forearm length, hand size, and finger size at the arm-span scale.
3. Keep foot length and head/skull size at the arm-span scale.
4. Apply lower-body vertical correction to thigh and shin bone offsets.
5. Apply upper-body vertical correction to pelvis-to-spine, spine, chest, upper-chest, and neck offsets.
6. Reconstruct descendants from corrected parent-child offsets so the hierarchy remains coherent.
7. Recalculate the corrected eye reference from the resulting skeleton.
8. Keep feet on the calibrated game floor.

The implementation must preserve each segment's rest direction where practical and change only the component intended by the vertical correction. It must not produce negative segment lengths, inverted bones, or a floating floor reference.

## Decoupled head retargeting

With arm-span scaling, the avatar's neutral head may not coincide with the player's HMD. The HMD should drive relative motion:

```text
headTranslationDelta = currentHmdPosition - neutralHmdPosition
headRotationDelta = currentHmdRotation * inverse(neutralHmdRotation)

avatarHeadTargetPosition = correctedNeutralAvatarHeadPosition
                         + Retarget(headTranslationDelta)

avatarHeadTargetRotation = headRotationDelta
                         * correctedNeutralAvatarHeadRotation
```

The exact translation gain may be one-to-one initially, but it must be applied as a delta from neutral rather than forcing the avatar head to the HMD's absolute world position. Floor alignment and body translation should remain solver-owned.

First-person visibility must hide or exclude the avatar head as needed to prevent the camera from sitting inside visible face geometry.

## Hard and soft targets

The retargeting hierarchy should be explicit:

### Hard constraints

- A tracked gameplay saber grip is an exact hand-position target.
- Feet must remain on the calibrated floor when planted.
- Bone lengths and correction bounds must remain valid.

### Soft constraints

- Relative HMD motion drives head and upper-body motion.
- Pelvis/body translation can combine HMD delta and controller midpoint evidence.
- Shoulder assistance, spine bend, stepping, crouching, and lean solve the remaining body posture.
- A small visual residual may be tolerated at the head before detaching a hand from a saber.

## Solver systems that must be recalculated

Height redistribution changes effective anatomy. After applying it, recalculate or rederive:

- left and right thigh lengths;
- left and right lower-leg lengths;
- spine segment lengths and total spine height;
- pelvis, hip, chest, shoulder, neck, head, and eye neutral positions;
- minimum leg reach;
- stance width and support geometry where height-normalized assumptions are used;
- step distance, step height, and safe leg extension thresholds;
- crouch, lean, airborne, and landing thresholds currently based on eye height or leg length;
- collision, first-person exclusion, clothing, outline, and spring-bone attachment behavior where cached geometry is involved;
- diagnostics and calibration confidence derived from old dimensions.

Search all uses of `AvatarScale()` and `player.standingHmdHeight` before implementation. Do not change only `BuildNeutralPose()` and leave downstream solver thresholds using old geometry.

## Persistence and migration

Recommended per-avatar settings:

```text
scalePolicy = ArmSpan
matchPlayerHeight = false or chosen release default
heightAdjustmentBalance = 0.0
```

The calibration/profile schema also needs versioned fields for:

- measured player arm span;
- measurement method and confidence;
- avatar arm-span measurement/version;
- resulting arm scale;
- target eye height;
- lower- and upper-body correction scales;
- selected balance;
- algorithm version.

Changing an avatar, scale mode, height toggle, balance, or calibration must reset/reseed persistent solver state. Do not let elbow, pelvis, foot, stance, or body-history values from the old skeleton carry into the new one.

## Diagnostics

Log enough information to diagnose a poor fit without requiring a video first:

- player arm span and confidence;
- avatar arm span and source bones;
- `armScale`;
- player target eye height;
- natural arm-scaled eye height;
- requested height delta;
- balance value;
- lower and upper stretchable lengths;
- resulting leg and torso scale factors;
- every safety clamp applied;
- corrected final eye height and residual error;
- whether hands used controller or authoritative saber targets;
- whether persistent solver state was reseeded.

The UI can remain simple; these details belong in diagnostics rather than the normal settings panel.

## Safety limits

Exact values still require testing, but the implementation must include:

- minimum and maximum valid arm scale;
- minimum and maximum leg and torso correction scales;
- a maximum total height correction before warning or refusing the fit;
- no zero, negative, or inverted segment lengths;
- a fallback to arm-span-only natural height when skeletal geometry is incomplete;
- a clear error when arm span cannot be measured confidently;
- finite-value checks before applying a corrected skeleton.

Extreme avatar/player mismatches should produce a bounded imperfect fit rather than corrupted geometry.

## Implementation order

1. Add tests for current avatar/eye/arm measurements and identify every scale-dependent solver path.
2. Add robust avatar arm-span measurement using the same endpoint semantics as gameplay hand targets.
3. Add player arm-span capture and confidence to calibration/profile data.
4. Add a versioned scale-policy data model without changing runtime behavior.
5. Implement uniform arm-span neutral-pose scaling.
6. Decouple neutral avatar head placement from absolute HMD world position and retarget HMD deltas.
7. Validate hands, feet, head motion, first-person visibility, and gameplay/preview parity with height matching off.
8. Add vertical skeletal correction and safe segment reconstruction.
9. Add `Match Player Height`, `Height Adjustment Balance`, reset behavior, tooltips, and per-avatar persistence.
10. Recalculate every dependent body-solver measurement and threshold.
11. Add migration, state reseeding, diagnostics, and failure fallback.
12. Test a proportion-diverse avatar matrix on Quest before choosing release defaults.

## Test matrix

At minimum, test:

- the user's long-leg, shorter-torso avatar;
- a short-leg, long-torso avatar;
- a realistic human-proportion avatar;
- an extreme stylized avatar;
- a short avatar and a tall avatar;
- arm-span scaling with height matching off;
- height matching on at balance `-1`, `0`, and `+1`;
- compression and stretching cases;
- menu/world preview and actual gameplay;
- crouching, forward lean, lateral lean, stepping, fast lunges, overhead cuts, cross-body cuts, and rest pose;
- exact saber attachment;
- planted-foot floor contact;
- first-person head exclusion;
- spring bones, clothing, MToon outlines, facial geometry, and avatar materials;
- avatar switching and calibration reset;
- recording output and live preview parity;
- performance on Quest 2 before wider Quest 3/3S testing.

## Acceptance criteria

- Natural arm reach matches the player's calibrated reach closely enough that ordinary gameplay does not need visible arm stretching.
- Tracked sabers remain in the avatar's hands.
- With height matching off, the avatar keeps its natural arm-span-scaled proportions.
- With height matching on, final corrected eye/standing height reaches the player target within a small documented tolerance.
- Moving the balance slider changes only where height correction is distributed; it does not change arm span or final target height.
- Slider left visibly emphasizes leg correction; slider right visibly emphasizes torso correction.
- Feet remain on the floor and do not float or penetrate because of correction.
- Head motion remains responsive and believable even though neutral avatar head position is decoupled from absolute HMD position.
- No body part is nonuniformly scaled in a way that damages face, hands, clothing, outlines, or spring bones.
- Existing calibration, avatar switching, menu preview, gameplay, and recording remain stable.

## Decisions already made

- Arm span is the base uniform scale for this design.
- Saber/hand endpoint alignment has priority over absolute avatar/HMD head alignment.
- HMD pose should be retargeted as motion relative to neutral.
- Height correction is optional and controlled by a toggle.
- Height correction must preserve arm span, shoulder width, arm lengths, hand size, and finger size.
- Height correction is skeletal/segment-based, not raw nonuniform root scaling.
- Balance direction is **left = legs**, **center = even/proportional**, **right = torso**.
- Height-correction settings should be per avatar.
- Feet remain on the game floor.

## Decisions still open

- Exact safe arm, leg, and torso scale limits.
- Whether arm-span scaling is the only policy or an explicit option beside legacy height scaling.
- Default state of `Match Player Height` for existing and fresh users.
- Exact robust player arm-span capture procedure and minimum confidence.
- Whether the height target remains measured standing HMD eye height or can be manually overridden.
- HMD translation-delta gain and whether vertical and horizontal gains differ.
- Exact smooth bias curve for the balance slider.
- Which spine/neck offsets receive upper-body correction for incomplete humanoid rigs.
- How large a mismatch should warn, clamp, or fall back to natural arm-span height.
