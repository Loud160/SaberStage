# Avatar Fit, Posture, Grip, Proportion, and Collision Implementation

## Purpose and checkpoint

This document is the implementation contract for the avatar-fit expansion discussed after on-device testing of the arm-span retargeting work. The protected reference checkpoint is commit `3d2b9c2` (`Checkpoint arm-span avatar fitting and player profiles`). That checkpoint must remain a usable comparison point while the work in this document is developed.

The goal is to preserve the current trackerless full-body solver and its exact headset/saber targets while making avatar size, proportions, grip placement, standing posture, floor placement, and collision behavior configurable enough to handle highly stylized VRM models. This is not a replacement IK engine and must not become a second copy of the body solver. Legacy and arm-span sizing are alternate retargeting inputs feeding the same maintained solver.

Implementation status (August 31, 2026): the settings/schema, UI, solver,
grip editor, calibration-audio routing, bounded collision options, and host tests
described here are implemented in the `Avatar-Framework` worktree. The complete
host/tooling suite and Quest-native build pass. Visual behavior, calibration
audio audibility, UI fit, and performance still require the Quest 2 validation
matrix below; compilation is not being treated as proof of those outcomes.

## Confirmed user decisions

- The existing arm-span sizing mode remains available and may scale the avatar root as high as `2.5x`.
- A legacy height-sizing mode must also be available. It uses the pre-arm-span formula based on player standing HMD height divided by authored avatar eye height.
- The current height-correction range must be expanded to approximately `50-150%` per adjustable body region and `0.75 m` or `45%` of natural eye height overall, whichever is smaller.
- Height Balance remains `-1.0` through `+1.0` but uses `0.01` increments. It transfers fitted vertical proportion between legs and torso, subject to geometry safety, even when no net player-height correction is required.
- A final manual avatar scale establishes the played root scale before Height Balance redistributes the fitted vertical proportions. It ranges from `50-200%`, defaults to `100%`, and is controlled by an enable toggle. Height Balance must not change the resulting overall height or arm span.
- The arm-reach option is named `Keep Hands on Sabers`, defaults ON, and may extend the solved arm chain as far as `2.5x` when required. Turning it OFF must warn that the hands can separate from saber handles after scaling or during unreachable poses.
- Each player profile has one grip configuration. Players who change physical grip styles should use another player profile.
- Left and right grip placement are adjusted independently by directly moving a world-space hand target, with position and rotation sliders retained for fine adjustment.
- Automatic shoulder-width estimation is worth attempting, but must be confidence-gated. Manual body-proportion controls remain available.
- Arm/body clipping prevention and arm/SpringBone interaction are separate options. Both default OFF and warn about possible gameplay performance impact before being enabled.
- The current backward-spine curve limit remains as a hard safety control. A separate Back Stiffness control changes how readily the spine bends after available neck motion is used.
- A temporary session-only debug switch hides hair on the primary avatar and all display clones. It defaults OFF at every game launch and is intentionally not persisted.
- Stance Width increases to a maximum of `400%`.
- Neutral Knee Bend ranges from `0-20 degrees`, uses one-degree increments, and defaults to zero additional bend beyond the avatar's authored/rest pose.
- Attack Pose ranges from `-20` to `+20 degrees`, uses one-degree increments, and defaults to zero. Positive values add a forward hip/waist hinge. Negative values counter a naturally forward stance and may continue into a mild backward bias. It interacts with knee bend and must not break planted feet or exact head/eye tracking.
- Auto Floor Height defaults ON. With Auto enabled, the runtime tracking-origin floor is used and a manual offset is added. With Auto disabled, the calibrated floor plane is used and the same manual offset is added. Floor Offset ranges from `-0.25 m` to `+0.25 m` in `0.005 m` steps.
- Calibration audio must provide countdown ticks, a measurement tone, and a completion shutter through an audibly proven game/menu output path.

## Settings ownership and persistence

### Player-profile ownership

All non-debug controls introduced here belong to the active local player profile. Avatar-specific controls must additionally be keyed by the selected avatar path so one player can fit different VRMs independently.

The existing `AvatarPlayerProfile.avatar` snapshot remains the owner of the active player's avatar preferences. Per-avatar fit data belongs in the existing retargeting-profile collection or its schema-compatible successor. Do not create unrelated parallel settings maps for individual controls.

### Per-player and per-avatar values

The following values are stored per player and selected avatar:

- sizing mode;
- Match Player Height;
- Height Balance;
- Manual Avatar Scale enabled/value;
- Keep Hands on Sabers;
- left and right controller/grip-to-hand position, rotation, and finger-closure values;
- body-proportion adjustment enabled;
- automatic shoulder width enabled;
- base torso width, shoulder width, waist/hip width, lower-torso width, neck-base width, and head-size values;
- torso height, hip-to-knee length, knee-to-ankle length, and whole-leg width values;
- arm/body clipping prevention;
- arm/SpringBone interaction;
- neutral knee bend;
- attack-pose bias;
- Auto Floor Height and floor offset.

Existing general avatar rendering and solver controls remain in the active player's `AvatarSettings` unless migration into the per-avatar fit record is necessary to prevent contradictory state.

### Session-only value

`Hide Hair for Solver Testing` is runtime-only. It must not be serialized, migrated, copied between player profiles, or retained after restart. It applies to the primary avatar, worn-avatar view, movable/recording previews, and every display clone, without destroying renderers or changing the avatar asset.

### Schema migration

- Increment the settings schema exactly once for this feature group.
- Existing installations must retain all prior settings.
- Existing retargeting entries receive safe defaults for every new field.
- Existing controller-to-wrist offsets must migrate into the active player/avatar fit entry so the current visible grip does not jump after upgrade.
- Unknown avatar keys and invalid/non-finite values are repaired without discarding unrelated profiles.
- Bounds enforced during parsing and repair must match the UI and solver bounds.

## Sizing modes and processing order

### Mode names and behavior

The UI may use a toggle named `Arm-Span Avatar Sizing`:

- ON: select root uniform scale from calibrated player arm span divided by measured avatar arm span. Require the existing confidence threshold and fall back safely when input is invalid. Raise the upper safety clamp from `1.8x` to `2.5x`.
- OFF: use the legacy uniform height scale, `standingHmdHeight / avatarEyeHeight`, with the same finite-value and root-scale safety checks used before arm-span retargeting.

Both modes feed the same current solver and retain all current elbow, wrist, stepping, crouch, expression, material, spring, and recording fixes. Legacy mode is not a forked copy of `AvatarSolver`.

### Authoritative adjustment order

Every neutral geometry rebuild must apply adjustments in this order:

1. Load authored VRM rest geometry at scale `1.0`.
2. Select the base uniform scale from arm span or legacy player height.
3. Apply optional base torso width, shoulder, waist/hip, lower-torso, neck-base, head-size, torso-height, and leg proportion retargeting.
4. Apply the final manual avatar scale multiplier and establish the actual played/recorded root scale.
5. At that final scale, apply optional player-height correction and then redistribute vertical proportion across lower body and torso according to Height Balance. Preserve explicit torso/leg length changes and keep the final overall height and arm span invariant while the balance value moves. The balance remains effective when the correction delta itself is zero.
6. Recompute every scale-dependent neutral measurement, solver threshold, segment length, floor relationship, grip target, collider approximation, spring attachment, and diagnostic value.
7. Reseed persistent body state if any geometry-affecting setting changes.

The final manual scale is the actual played and recorded avatar size. It must not be silently undone by Match Player Height. Height Balance runs afterward only as a skeletal distribution step; its endpoints may change leg-versus-torso proportions but not final height or arm span.

### Expanded height correction

- Lower-body and torso vertical scales are bounded to approximately `0.50-1.50`.
- Total requested correction is bounded to the smaller of `0.75 m` or `45%` of natural fitted eye height.
- Height Balance uses `0.01` UI increments.
- The center value preserves measured vertical contribution after applying any height correction.
- The endpoints transfer the available safe proportion toward one region while applying an equal and opposite transfer to the other, so total height does not change.
- Geometry validation must reject non-finite, collapsed, inverted, or implausibly short segments and fall back to the last valid fit.
- Diagnostics must report requested, bounded, applied, and residual height corrections plus which bounds were reached.

## Final manual scale and reach behavior

### Manual Avatar Scale

- Toggle defaults OFF.
- Slider defaults `100%`, range `50-200%`.
- Slider is disabled or hidden while the toggle is OFF, but its saved value is preserved.
- The multiplier applies after base sizing, height correction, and body proportions.
- Changing the value updates the live avatar without reloading the VRM and reseeds the body solver once.

### Keep Hands on Sabers

- Defaults ON.
- ON preserves the tracked saber handle as the authoritative hand-position target.
- Shoulder assistance and proportional upper/lower-arm extension may increase the effective chain length up to `2.5x`.
- Stretch should be distributed across upper and lower arm according to authored segment proportions.
- Rotation remains limited by the existing anatomical wrist cone and elbow hemisphere protections.
- OFF removes emergency chain extension and the final forced unreachable hand endpoint. Reachable targets still solve normally. Unreachable targets clamp anatomically, so visible hand/saber separation is expected.
- The first attempt to turn the option OFF displays a plain-language warning. Cancel keeps it ON; confirm applies OFF.
- Diagnostics report requested reach ratio, applied extension, whether the cap was reached, and whether separation resulted from the option being OFF.

## Grip placement editor

### Entry points

Add `Position Left Hand` and `Position Right Hand` buttons to the Avatar Fit controls. Each opens one movable world panel plus a translucent globe with three colored rotation bands at the active hand, and begins a temporary adjustment session for that side.

### Visibility and interaction

- The adjusted arm and hand must be visible in the headset even when Wear Avatar is OFF.
- Only the selected arm is rendered to the headset during editing. The torso, head, opposite arm, and legs stay hidden so the headset cannot end up inside an obstructing body mesh.
- Because VRoid avatars often combine the torso and limbs in one skinned renderer, arm-only visibility uses compact meshes filtered from the imported skin weights rather than assuming one renderer per limb.
- The opposite controller remains available to point at the panel and grab the target globe. A grab from the hand being calibrated is rejected because moving both the tracking source and its destination would create an unusable feedback loop.
- The adjusted controller continues to drive its live target so the user can turn and move the grip while inspecting contact.
- Temporary visibility must not alter recording visibility, clone settings, or the saved Wear Avatar state.
- The complete source avatar remains on the spectator-camera layer while the selected filtered arm uses the headset-only layer, so recordings continue to contain one complete avatar and never a duplicate editor arm.
- Closing or cancelling restores the prior visibility state.

### Controls

The primary adjustment is the world-space target globe. The user grabs and moves/rotates it in six degrees of freedom with the opposite controller. The globe is the actual target supplied to arm IK, not a direct bone editor: the shoulder, elbow, forearm, wrist, and hand continue to move through the normal solver. When it is not grabbed, it follows the live adjusted hand so it cannot become a stale marker in the scene.

The panel exposes the same target transform as seven fine-adjustment values:

- position X, Y, Z;
- rotation pitch, yaw, roll;
- grip closure from the authored/rest finger pose through and beyond the ordinary relaxed saber grip;
- Reset;
- Save;
- Cancel.

Position controls should use metre values with fine increments suitable for controller grip alignment. Rotation controls should use degrees. Use symmetric ranges and document coordinate directions in tooltips. Values update live without reloading the avatar.

Manual position, pitch, yaw, and roll form one conventional local rigid transform relative to the unadjusted controller/saber-derived wrist target. The globe and sliders read and write this same transform; there is no second correction layer. The completed world target is supplied to arm IK before its elbow is selected, using the avatar's authored hand-to-forearm direction as a bounded elbow cue. Translation therefore moves the complete analytic arm chain, while target rotation establishes the required wrist orientation without directly writing a bone. Grip Closure affects only finger curl after wrist solving; it must not move or rotate the hand target.

Live globe movement uses a dedicated per-hand solver update. It does not reapply render settings, reset foot/body history, or recalibrate the avatar at controller refresh rate.

Save writes to the active player/selected-avatar fit entry. Cancel restores the pre-edit values. Reset restores the neutral inferred controller-to-wrist/grip offset for that side but does not save until Save is selected.

## Body-proportion retargeting

### Controls

Add a master `Adjust Body Proportions` toggle. When enabled, show:

- `Auto Shoulder Width`;
- Torso Width slider;
- Shoulder Width slider;
- Waist/Hip Width slider;
- Lower Torso Width slider;
- Neck Base Width slider;
- Head Size slider;
- Torso Height slider;
- Hip to Knee Length slider;
- Knee to Ankle Length slider;
- Leg Width slider.

Manual values remain stored when automatic shoulder estimation is enabled. Turning Auto off restores the saved manual shoulder value rather than resetting it.

### Automatic shoulder-width estimate

There are no shoulder or elbow trackers, so automatic shoulder width is an estimate, not a direct measurement. Use the accepted multi-pose calibration data to fit a plausible shoulder-pivot separation:

- T pose;
- arms forward;
- arms 45 degrees outward/Y pose;
- hands at chest;
- hands near same-side shoulders and cross-body poses when Advanced calibration supplies them.

The fit must solve against both sides together, reject asymmetric/outlier samples, enforce anthropometric bounds normalized to standing height/arm span, and produce a confidence value. Automatic mode is used only above a documented threshold. Otherwise retain authored/manual shoulder width and report why auto fit was unavailable.

### Geometry behavior

- Torso Width is the base lateral scale of the complete torso. The shoulder, waist/hip, and lower-torso values are relative refinements applied after it, rather than competing absolute replacements.
- Shoulder Width changes the lateral separation of the clavicle/upper-arm pivots while maintaining a centered chest and symmetric rest pose. Its `50-300%` range is multiplied by Torso Width when computing effective avatar arm span, so widening either the base torso or the shoulders reduces the root scale needed to match the same player reach.
- Waist/Hip Width changes the lateral hip/waist support region without changing overall avatar height.
- Lower Torso Width scales skin weighted to the hips/lower torso, then cancels inherited scale at the upper torso and leg roots so it does not accidentally widen the entire avatar.
- Neck Base Width scales the lower neck where it joins the shoulders rather than trying to move a centerline neck bone sideways.
- Head Size scales the complete head and face while making the upper, head-weighted part of the neck follow the head. The lower neck remains governed by Neck Base Width, so ordinary skin weights provide a tapered transition rather than a hard size discontinuity.
- Torso Height scales the vertical torso contribution after automatic height fitting.
- Hip to Knee Length and Knee to Ankle Length independently scale the two leg segments and therefore alter final avatar height.
- Leg Width scales both complete legs while cancelling the inherited scale at the feet so authored footwear proportions remain intact.
- Skin, clothing, rigid attachments, spring roots, and clones must follow the retargeted geometry consistently.
- Prefer bone-space rest-geometry deformation that naturally uses existing skin weights. If a model lacks usable weights, fall back safely and expose the limitation in diagnostics rather than destructively editing mesh data.
- Prevent double application when rebinding or switching avatars. Always derive from immutable authored rest geometry.

## Posture controls

### Stance Width

- Preserve the current lower bound unless device testing proves it unusable.
- Raise the maximum to `400%`.
- Continue scaling the hip-width-derived neutral foot separation.
- Recompute support polygons and step thresholds from the selected stance so a wide stance does not immediately trigger corrective steps.

### Neutral Knee Bend

- Range `0-20 degrees`, step `1 degree`, default `0` additional degrees.
- Apply symmetrically to both legs in neutral standing.
- Preserve authored knee bend direction and the existing anti-flip knee pole.
- Compensate pelvis height/position so feet stay on the selected floor plane and the head/eye target remains exact.
- Additive attack pose must solve from the knee-adjusted neutral geometry, not overwrite it.
- Crouch, squat, stepping, and airborne paths must start from and return to the configured neutral bend.

### Attack Pose

- Range `-20` through `+20 degrees`, step `1 degree`, default zero.
- Apply as a persistent bias to hip/waist hinge distribution relative to the calibrated natural stance.
- Positive values bias the torso forward; negative values remove forward bias and may create a mild rearward hinge.
- Do not rotate or translate the tracked eye away from the HMD target.
- Use calibrated neck motion to counter-rotate gaze before adding compensating spine curvature.
- Coordinate with neutral knee bend and foot support: a stronger forward attack pose may require slightly more knee bend/pelvis adjustment, but may not move planted feet or create a backward C-shaped spine.
- Blend setting changes over a short bounded interval to avoid a visible snap while previewing.

### Neck priority and Back Stiffness

- Extend the runtime calibration profile with accepted neck yaw and pitch ranges from Advanced calibration.
- Basic/unavailable calibration uses conservative generic anatomical limits.
- HMD pitch/roll remains a head/neck responsibility first.
- As neck use approaches its calibrated soft range, progressively involve upper spine segments.
- Only after the calibrated hard range is exhausted may additional compensation use the full spine safety envelope.
- Add `Back Stiffness`, where higher values require more neck use and stronger positional evidence before spine bow increases.
- Keep the existing `Backward Spine Curve` slider as the absolute safety ceiling preventing inversion/extreme rearward C-bowing.
- The two controls must not multiply into a zero-motion dead zone. Stiffness changes response/distribution; Backward Spine Curve changes the final maximum.
- Diagnostics report requested head pitch, neck-applied pitch, spine-applied pitch/bow, calibrated limits, stiffness, and safety-limit utilization.

## Floor placement

### Auto Floor Height

- Defaults ON.
- ON uses the active tracking-origin floor and follows legitimate tracking-origin/recenter changes.
- OFF uses the floor plane stored by the active player calibration and does not automatically adopt later tracking-origin height changes.
- In either mode, `Floor Offset` is added last.

### Floor Offset

- Range `-0.25 m` through `+0.25 m`.
- Step `0.005 m`.
- Zero default.
- Positive and negative direction must be described unambiguously in the tooltip after confirming the solver coordinate convention.
- The adjustment affects primary avatar, worn body, clones, stepping anchors, and recordings consistently.
- Changing floor settings clears/reseeds planted-foot state so stale anchors do not pull the avatar back to the prior floor.

## Temporary hair-debug visibility

- Add `Hide Hair for Solver Testing` near posture/spine controls.
- It is session-only and defaults OFF every launch.
- It hides hair on the primary runtime avatar and all display clones.
- It must not change imported materials, renderer ownership, recording masks, or persistent Hide Hair/Wear Avatar choices.
- Turning it OFF restores exactly the renderers hidden by this debug feature.
- Mark the control and implementation for removal after spine behavior is accepted.

## Collision features

### Prevent Arm-Body Clipping

- Defaults OFF.
- Enabling displays a performance warning and requires confirmation.
- Use low-cost analytic body volumes derived from retargeted geometry: torso/chest capsules or tapered ellipsoids plus optional hip volume.
- Do not perform per-triangle skinned-mesh collision.
- During arm solving, first move a hand target that lies inside the torso ellipsoid to the nearest front surface. This intentionally allows temporary hand/saber separation: an avatar hand cannot occupy solid torso space.
- Evaluate a fixed, allocation-free set of outward/front elbow candidates and score the elbow plus both arm half-segments against the torso volume. Select the least-penetrating valid path so cross-body poses do not merely move the elbow while leaving the forearm through the chest.
- Preserve the original tracked controller/saber target in diagnostics even when collision resolution changes the solved target; do not create NaNs, arm inversion, or frame-to-frame pole flips.
- Apply temporal continuity and bounded iterations. No heap allocation is permitted in the per-frame solver.
- Diagnostics report penetration depth, corrected joints, unresolved target-inside-body cases, and solve-time cost.

### Arm Interaction with Spring Bones

- Defaults OFF.
- Enabling displays a performance warning and requires confirmation.
- Generate a small fixed set of arm collision capsules/spheres from solved upper-arm and forearm segments.
- Feed those shapes only to SaberStage's SpringBone collision pass; do not add Unity rigidbodies or general-purpose physics.
- Respect SpringBone quality/budget settings and skip work when SpringBones are disabled.
- This feature may move authored secondary-motion chains such as hair, clothing, or chest accessories only when SaberStage SpringBones are enabled and the avatar actually contains compatible spring chains. It does not affect rigid meshes and does not guarantee anatomically correct soft-body simulation.
- Diagnostics report active generated colliders and added SpringBone solve time.

## Calibration audio repair

### Required cue sequence

- One audible tick for each countdown second.
- A clearly distinguishable measurement tone while movement capture is active.
- Stop the tone and play one shutter/completion sound when the measurement closes.
- Retried/rejected steps repeat the sequence correctly without overlapping clips.

### Implementation constraints

- Keep generated clips or bundled assets private to SaberStage.
- Route cues through a proven menu/game audio path or explicitly configured mixer group that is audible beside Beat Saber's menu music.
- Do not use the previously crashing `PlayOneShot` binding.
- Log initialization and one concise line per requested cue during diagnostic builds/device validation, including whether the source, clip, mixer/output route, and playback state were valid.
- Cue failure must never interrupt calibration; visual guidance remains authoritative.

## UI organization and gating

The Avatar tab may need another subpage rather than compressing the existing calibration page. Controls should be grouped as:

1. Sizing Mode and Height Fit.
2. Manual Final Scale and Saber Reach.
3. Grip Placement.
4. Body Proportions.
5. Posture and Floor.
6. Collision and temporary diagnostics.

Only applicable controls are interactable/visible:

- Height Balance requires Match Player Height and arm-span sizing.
- Manual Scale slider requires its toggle but retains its value while disabled.
- Auto Shoulder Width requires Body Proportions and a confident compatible calibration; manual shoulder value remains saved.
- Proportion sliders require Body Proportions; Shoulder Width is disabled while Auto Shoulder Width is active.
- Collision controls are independent of sizing mode.
- The temporary hair switch is always available during this development pass.

All new controls require plain-language tooltips, reset buttons for adjustable numeric values, and visual state refresh after profile switching, avatar switching, reset, calibration completion, or menu reconstruction.

## Performance and lifetime requirements

- No per-frame heap allocation in body, arm, floor, collision, or proportion solving.
- No Unity object creation/destruction while ordinary avatar tracking is running.
- Geometry-affecting setting changes rebuild cached neutral data once and reseed persistent state.
- UI callbacks save settings and request a bounded rebuild; they must not perform expensive mesh traversal synchronously every slider tick.
- Grip-edit visibility, debug hair state, generated collision shapes, and warning dialogs must be cleaned up when leaving the menu, switching player/avatar, unloading the mod, or destroying the runtime.
- Display clones consume the already solved/retargeted avatar transforms and must not run independent IK or collision solvers.

## Verification plan

### Host tests

Add deterministic tests for:

- arm-span scale up to `2.5x` and legacy height sizing;
- expanded height correction and `0.01` balance behavior;
- final scale ordering and bounds;
- Keep Hands on Sabers ON/OFF, `2.5x` reach cap, and unreachable separation;
- per-player/per-avatar migration and grip offsets;
- shoulder auto-fit confidence/fallback and manual proportions;
- neutral knee bend, attack-pose interaction, exact eye target, and planted feet;
- calibrated neck-first distribution, Back Stiffness, and backward safety limit;
- Auto Floor Height and manual offset reseeding;
- analytic arm/body avoidance, target-inside-body fallback, and pole continuity;
- generated arm/SpringBone collider budgeting;
- no allocations in steady-state solve paths;
- session-only hair state never entering settings JSON.

### Tooling and build validation

Run in order:

1. `scripts/test-host.ps1`.
2. Avatar shader bundle rebuild only if shader assets change.
3. `scripts/build.ps1` or the repository's guarded build/QMOD path.
4. Inspect Git diff, package contents, and settings/schema fixtures.

### Quest 2 device validation

Test each phase separately before combining conclusions:

- audible Basic and Advanced calibration cue sequence;
- arm-span and legacy sizing on the same avatar/profile;
- maximum `2.5x` root scale and expanded height compression;
- final 50%, 100%, and 200% scale with Keep Hands on Sabers ON/OFF;
- independent left/right grip alignment saved across reload and profile switch;
- manual and automatic shoulder width on at least two differently proportioned VRMs;
- knee bend and attack-pose extremes without floating feet, eye error, or backward C-bow;
- floor auto/manual behavior before and after recenter;
- hair-debug visibility across primary avatar and clones;
- arm crossing with clipping prevention OFF/ON;
- authored SpringBone interaction OFF/ON and performance-panel comparison;
- menu preview, gameplay, restart, pause/resume, results, and avatar switching lifetime paths.

Collect an on-demand avatar diagnostic log and a third-person recording for each geometry/solver phase. Do not declare visual success from host tests or compilation alone.

## Implementation phases and rollback points

1. Settings/schema/UI foundations and sizing modes.
2. Expanded height correction, final scale, and Keep Hands on Sabers.
3. Grip editor and calibration audio repair.
4. Floor, stance, knee, attack pose, calibrated neck priority, Back Stiffness, and debug hair.
5. Manual and confidence-gated automatic body proportions.
6. Arm/body clipping prevention.
7. Arm/SpringBone interaction.
8. Full host/build validation and Quest visual/performance pass.

Each phase should remain buildable and testable. If a later experimental phase regresses the avatar, revert to the preceding phase rather than layering emergency fixes over invalid geometry.

## Explicit non-goals

- No Final IK source copying.
- No external tracker support in this pass.
- No per-triangle skinned collision or general Unity rigidbody ragdoll.
- No destructive rewriting of source VRM files.
- No second full body-solver implementation for legacy sizing.
- No persistence for the temporary hair-debug setting.
- No claim that automatic shoulder width is an exact physical measurement.
