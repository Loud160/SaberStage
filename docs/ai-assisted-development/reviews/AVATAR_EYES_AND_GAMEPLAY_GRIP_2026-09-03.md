# Avatar eyes and menu/game grip investigation — September 3, 2026

## Reports and scope

- BlackHeart looks normal in menus, but in maps the eyes look closed most of
  the time and briefly open as though blinking backwards.
- User confirms this still happens with Animated Expressions **off** and
  dates the regression to the introduction of the arm-span sizing system.
- Stream footage also shows hands holding gameplay sabers differently from
  the menu pointers after the per-avatar grip adjustment.
- Another avatar remains in a T-pose. Its identity and a matching failed-load
  capture have not yet been established; do not assume it is Miku.
- User explicitly requires menu pointers and gameplay sabers to use exactly
  the same calibrated grip relationship. The hand correction below implements
  that requirement without changing saved calibration or finger settings.
- Do not invert blinks or tune expressions to hide the eye symptom. Its cause
  remains unresolved and needs the runtime diagnostic capture described below.

## Current evidence

Private support capture:
`diagnostics/avatar-eyes-and-grip/SaberStage-Support-Logs-20260903-023842.zip`,
extracted to `diagnostics/avatar-eyes-and-grip/session-023842/`.
The game was not stopped, restarted, or updated to obtain this capture.

The captured active profile selects `5063571287661799621.vrm`, with
`animatedExpressions=false`, `armSpanAvatarSizing=false`, and head, neck-base,
and torso-width settings at 100%. The selected per-avatar fit retains manual
scale 68%. Do not equate that manual multiplier with the total computed fit.
The logs do not currently contain per-frame/requested-vs-actual facial weights.
They show a successful BlackHeart binding, not an identified failure for the
unnamed other avatar.

Read-only model reference:
`C:/Users/Owner/Downloads/BlackHeart_5063571287661799621.vrm`.
Private numeric inspection script:
`diagnostics/avatar-eyes-and-grip/inspect_morphs.py`.
Do not add the model or captured settings/logs to a public commit.

The model's face mesh binds Blink to target 13 (`Fcl_EYE_Close`), Joy to target
3 (`Fcl_ALL_Joy`), and the other emotions to distinct full-face targets.
No initial mesh weights are authored. Joy moves eyelid vertices as well as the
mouth/brows. Strong Joy plus Blink can therefore interact, but this **does not
explain the user's confirmed Animated Expressions-off menu/map symptom**.

## Source trace and rejected diagnosis

`AvatarManager::UpdateAutomaticExpressions` returns before the blink timer when
the runtime enable flag is false. The quality switch updates that flag through
`ApplyAvatarSettings`; disabled state clears the automatically used presets.
Neither the basic blink envelope nor `ApplyExpression` reverses weight meaning
in gameplay. A menu-only test-expression button can also write weights, so the
diagnostics must read Unity state, not just the automatic controller's cache.

An initial inspection incorrectly identified eye bones as remaining in neutral
world positions. That was corrected before any solver change: the final loop
in `StaticTrackerlessAvatarSolver::Solve` explicitly composes both eye bones
with the solved head. This loop also exists in the pre-arm-span commit
`bc946c9` and the arm-span checkpoint `3d2b9c2`.

Added native coverage verifies actual eye-to-head offsets and rotations across
head translation/crouch/yaw/pitch, both sizing modes, multiple scale inputs, and
avatars with/without eye bones. It does not exercise Unity rendering or prove
the visible problem solved. Head-size/local nonuniform scaling and render state
are separate from this native check; captured head-size settings are neutral.

## Hand source trace and implemented correction

Before this correction, `AvatarManager::SampleTracking` used a real saber grip
when available, otherwise the controller. Menu fallback deliberately set
`handIsSaberGrip=true` even when `saberGrip.valid=false`, but gameplay fallback
did not, temporarily switching to a different wrist-target path.

The former `SampleSaberGripPose` derived a new grip center from blade/handle
geometry (default depth of 0.085 m from the blade base, or a bounded authored
depth). That **SaberStage-added** translation was absent in the fallback menu
controller path. It was not an offset imposed by Beat Saber. It predated manual
grip alignment and duplicated its responsibility after the user had already
positioned the hand. The default depth is not the magnitude of the final offset;
that depended on the live blade and handle geometry.

The solver additionally rebuilt the grip-to-hand rotation using live controller
versus saber rotations. On a tracking reset, that conversion could cancel a
saber's rotation, changing both the wrist orientation and the basis of the
saved positional adjustment. These were two independent mod-side differences.

Changes:

- Removed the blade-derived recentering helper. Sample the actual handle's pose
  directly, preserving the menu controller fallback's existing origin.
- Treat a valid controller fallback as the same grip source in either scene,
  including while waiting for a gameplay Saber component. Keep `saberGrip.valid`
  independent so the calibration fitter still knows when a real saber was seen.
- Use one constant grip-local anatomical rotation from the saved calibration;
  do not derive it from the live controller/saber orientation at reset. Apply
  the existing per-avatar manual position/rotation after that unchanged basis.
- Preserve existing profile values, basic/advanced calibration, grip closure,
  thumb settings and the non-grip controller-to-wrist path. No migration or
  recalibration is required for the menu-adjusted grip.

Native reproduction: `TestGripCalibrationSurvivesPointerSaberTransition` failed
before the solver correction with `one calibrated pointer-to-hand anchor must
also hold the gameplay saber identically`. It now verifies the **solved hand
bones**, not just requested targets, through menu, gameplay-before-discovery,
gameplay-with-sabers, and return-to-menu. Coverage includes both hands, basic,
controller-fitted and saber-fitted profiles, both sizing modes, 0.68/1.0 final
scale, translation/rotation about all three axes and resets mid-motion. An older
test asserting the hand ignored saber rotation was updated to the explicit
user-required grip-relative invariant.

This verifies the mathematical anchor contract. It does not prove that every
custom saber/pointer mesh has an identical authored visual grip. The on-device
BlackHeart menu/map comparison remains required before declaring visual success.

## Added diagnostic coverage

The existing five-second avatar performance gate now also records:

- `AvatarFaceDiag manager`: gameplay tracking, automatic expression enable,
  requested blink/emotion weights and blink timer state.
- `AvatarFaceDiag bone`: actual eye pose relative to Head and position error
  against the native solver result.
- `AvatarFaceDiag runtime/preset`: actual root/body/head scale factors and
  renderer blend-shape weight ranges for Blink/Joy/Fun/Angry/Sorrow.
- `AvatarGripDiag`: real saber availability separately from the grip flag,
  source and actual hand poses in controller space, the actual hand's grip-local
  position/rotation, solver position error, finger closure and thumb curve.

These are bounded readbacks, not renderer changes, per-frame hierarchy scans,
mesh baking, or new tracking discovery. They reuse the existing log gate so
the diagnostic path does not dirty UI/render state during the FPS investigation.

## Required next evidence

After the user authorizes installing the combined diagnostic build, capture at
least five seconds in the menu, then a map showing the issue with expressions
off, then return to the menu. Keep the same avatar/profile/grip throughout.

Compare actual weights and eye-to-head transforms across the scene transition.
If the automatic flag is false but weights change, identify the writer. If
weights stay at zero and bones agree, inspect visible face/eyelid rendering
rather than modifying blink timing. Compare `handInGrip` and `gripLocalRotation`
in the two `AvatarGripDiag` hand anchors across the menu/map transition. If the
anchor is stable but visible alignment differs, inspect the actual pointer and
saber model frames instead of adding another inferred offset. A screenshot or
short clip of the face during the map would provide the missing visual evidence.

## Validation and deployment

- All 14 host CTest suites pass, including the expanded grip transition test.
- All 60 tooling tests pass; sampling coverage forbids blade-derived recentering
  and verifies the shared fallback grip flag and bounded diagnostic readbacks.
- ARM64 incremental build and private-logger ELF dependency check pass.
- The chat draw-order and idle-chat performance fixes from the prior pass remain
  included. The user has now authorized loading this build when ready.
- QMOD packaging verification passes. Library SHA-256:
  `620baffcc86af8209a8466e014c4b470d1562d757643d095672fa14686ffb3e3`.
  QMOD SHA-256:
  `58e49e86eac3b33c28b5aee4bf8a431a28fc8d301adab7440fdf4c049f8ef0b0`.
- Deployed to Quest 2 `1WMHH840QJ1046` after force-stopping Beat Saber and
  confirming its process was gone. Receipt-owned deployment verified all four
  payload hashes. The installed mod matches the library hash above. No settings,
  profiles or other mods were changed. Receipt output is in the private file
  `diagnostics/avatar-eyes-and-grip/deploy.txt`.
- Beat Saber was left closed for the user to start. On-headset visual grip,
  face and chat checks remain pending. Build/tests and installed-file hashes
  are not claims that those runtime checks have passed.
