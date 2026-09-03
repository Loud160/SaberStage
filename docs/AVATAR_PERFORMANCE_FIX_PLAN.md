# Avatar Performance Fix Plan

Scope: the avatar halves the headset frame rate (72 -> 36 FPS) whenever it is
loaded and the spectator camera is rendering, regardless of avatar quality
settings. Recording/streaming/preview without an avatar have no measurable
impact. This document identifies the exact causes and prescribes the fixes.
Implement ONLY what is written here. Do not redesign the IK solver, do not
change any solver math, do not touch the BigScreen repository, and do not
"simplify" the fixes into different mechanisms.

## Why settings changes appear to do nothing (read first)

The Quest compositor locks the app to 36 FPS the moment total frame time
exceeds ~13.9 ms. The avatar currently adds several independent costs that sum
past that budget; removing one of them still leaves the total above 13.9 ms,
so the displayed FPS does not move. Therefore: measure FRAME TIME (OVR Metrics
overlay or logged milliseconds), not just FPS, when verifying each fix. The
mod's own numbers help: `RuntimeStatistics::springSolverMilliseconds` is
already tracked, and the `Write Diagnostic Log` button records
`nativeSolveMicroseconds` and transform write counts.

## Cause summary (verified by code inspection)

1. The avatar pipeline runs at HMD rate (72 Hz) PLUS once per spectator render
   (30/60 Hz), even though in the default configuration only the 30 FPS
   spectator camera can see the avatar (it lives on the spectator-only avatar
   layer). That is ~102 full solve+write passes per second to produce 30
   visible frames.
   - `src/avatar/AvatarRuntimeDriver.cpp`: `Update()` -> `SampleTracking()`,
     `LateUpdate()` -> `SolveAndWrite()` + `UpdateSecondaryMotion()` — every
     Unity frame, unconditionally.
   - `src/avatar/AvatarManager.cpp`: `camera_.SetBeforeRenderHandler(...)` ->
     `EnsureSolvedForSpectatorRender()` — samples and solves AGAIN before every
     spectator render. The duplicate-sequence skip never fires because the
     head has always moved since LateUpdate, so the sequence is always new.

2. The SpringBone inner loop performs an il2cpp `Transform.TransformPoint`
   call for EVERY collider for EVERY joint, every substep.
   - `src/avatar/vrm/VrmUnityRuntime.cpp`, `SimulateSpringStep(...)`: the loop
     `for (colliderIndex < colliderCount) { ... collider.transform->TransformPoint(collider.localOffset) ... }`
     is nested inside `for (auto& joint : chain.joints)`. Collider world
     centers do not change between joints within one substep; they are being
     recomputed ~joints × colliders times (Black Heart: 146 joints × 11 active
     colliders ≈ 1,600 calls per substep; at spring quality High = 45 Hz × 2
     substeps that is ~144k redundant il2cpp calls per second). Each il2cpp
     transform call costs roughly 1.5–4 µs on Quest: this loop alone is
     several milliseconds per frame.
   - The CORRECT pattern already exists ten lines below in the same function:
     the generated arm colliders are pre-cached into
     `generatedArmColliderCenters_` / `generatedArmColliderRadii_` arrays and
     the joint loop reads plain floats. Mirror exactly that.

3. Each spring joint additionally performs ~5 il2cpp transform calls per
   substep (`set_localRotation`, `get_position`, `TransformDirection`,
   `get_rotation`, `set_rotation`) where 2–3 suffice.

4. Structural (LOWER priority, larger change): the avatar renders as ~87
   separate SkinnedMeshRenderers (one per VRM primitive; Black Heart:
   87 renderers, 164k vertices). This multiplies draw calls and skinning
   dispatches in the spectator pass and in every view that shows clones.

## Fix 1 — Hoist spring collider centers out of the joint loop

File: `src/avatar/vrm/VrmUnityRuntime.cpp`, function `SimulateSpringStep`.

At the TOP of `SimulateSpringStep` (once per substep, before the chain loop),
resolve every active collider's world center into a pre-sized cache, exactly
like the generated arm collider arrays that already exist:

- Add members (near `springColliders_`):
  `std::vector<UnityEngine::Vector3> springColliderCenterCache_;`
  `std::vector<float> springColliderRadiusCache_;`
  `std::vector<std::uint8_t> springColliderAliveCache_;` (or pack into one
  struct vector — implementer's choice, but plain arrays of value types only).
- Fill them once per substep:
  for each of the first `stats_.activeSpringColliderCount` entries of
  `springColliders_`: if `IsAlive(collider.transform)` store
  `collider.transform->TransformPoint(collider.localOffset)` and
  `collider.radius`, else mark dead.
- Inside the joint loop, replace the per-joint
  `collider.transform->TransformPoint(...)` and `IsAlive(...)` calls with
  reads from the cache. The collision math itself must remain identical
  (same order, same `radius + group.hitRadius`, same double re-projection).

NOTE: `group.hitRadius` varies per chain group, so cache only the collider's
own radius and keep the `+ group.hitRadius` addition in the joint loop.

Result: collider il2cpp calls drop from joints × colliders to colliders, per
substep (~1,600 -> ~22 for Black Heart).

## Fix 2 — Thin the per-joint il2cpp calls in the same loop

Same function. Current per-joint sequence:

    joint.transform->set_localRotation(joint.restLocalRotation);
    const auto origin = joint.transform->get_position();
    const auto restDirection = SafeDirection(joint.transform->TransformDirection(joint.localAxis), ...);
    ... later ...
    joint.transform->set_rotation(op_Multiply(rotation, joint.transform->get_rotation()));

Changes, preserving identical math:

- Replace the `get_position` + `TransformDirection` pair with ONE combined
  read: `joint.transform->GetPositionAndRotation(byref(position), byref(rotation))`
  AFTER the `set_localRotation` reset, then compute
  `restDirection = SafeDirection(Rotate(worldRotation, joint.localAxis), ...)`
  natively (rotating a cached local axis by the world rotation is exactly what
  `TransformDirection` does; the mod already has a native quaternion-vector
  rotate used elsewhere — if none exists in this file, add a small static
  helper rather than calling into Unity).
- Reuse that already-read world rotation in the final write:
  `set_rotation(op_Multiply(rotationDelta, worldRotation))` instead of calling
  `get_rotation()` again.
- Do NOT remove the `set_localRotation(joint.restLocalRotation)` reset — the
  read must happen after it, since the rest pose is the reference frame.

Result: ~5 il2cpp calls per joint per substep become ~3.

## Fix 3 — Gate the solve/write/springs to consumer rate

Files: `src/avatar/AvatarManager.cpp` (primary), possibly a small helper on
`VrmUnityRuntime` for visibility state.

Principle: the avatar only needs to be POSED as often as something renders
it. Today the only consumers are (a) the spectator camera at 30/60 FPS via
the existing before-render handler, and (b) the player's own headset — but
(b) ONLY when Wear Avatar is enabled or a display clone uses a layer the HMD
renders.

Implementation:

- Add a private helper in `AvatarManager::Impl`, e.g.
  `bool AvatarVisibleToHmd() const noexcept`, returning true when ANY of:
  - the last applied settings had `wearAvatar == true`;
  - `vrmRuntime_->StandinActive()` AND the standin layer is not
    `camera::kAvatarLayer` (i.e. visibility is Headset+Camera or HeadsetOnly).
  Track both facts from `ApplyAvatarSettings` in plain bool/int members — do
  NOT query Unity objects per frame for this.
- In the LateUpdate path (`SolveAndWrite` + `UpdateSecondaryMotion` as called
  from `AvatarRuntimeDriver::LateUpdate`): when `AvatarVisibleToHmd()` is
  FALSE, skip BOTH calls entirely and instead accumulate the frame's
  `deltaTime` into a new member (e.g. `pendingSecondaryMotionSeconds_`).
  Recommended shape: keep the driver file untouched and put the gate inside
  the two AvatarManager methods themselves (early return + accumulate), so
  every existing caller inherits the behavior.
- In `EnsureSolvedForSpectatorRender` (the before-spectator-render handler):
  after the existing `SampleTracking(); SolveAndWrite();`, when the LateUpdate
  path is gated off, also run the secondary motion with the ACCUMULATED time:
  `UpdateSecondaryMotion(pendingSecondaryMotionSeconds_ + currentDelta)` and
  reset the accumulator. The spring simulator already uses an internal
  fixed-rate accumulator, so feeding it larger deltas at 30 Hz is safe; its
  existing `deltaTime > 0.25F` reset guard must remain the outer bound.
- `SampleTracking()` stays per-frame in `Update()` — it is cheap and the
  velocity/step-inference/calibration paths want dense samples. Do not gate it.
- When `AvatarVisibleToHmd()` is TRUE, behavior must remain exactly as today
  (full-rate LateUpdate solve; the spectator pre-render solve keeps its
  current role).
- Edge cases that must keep working:
  - Toggling Wear Avatar or clone visibility mid-session flips the gate on
    the next frame (that is why the flags are cached in ApplyAvatarSettings).
  - When NO spectator render is happening (camera disabled/no demand) and the
    avatar is HMD-invisible, it is correct for the avatar to stop being posed
    entirely — nothing can see it. It must still resume cleanly when a
    consumer appears (the accumulator guard above handles the time jump).
  - The display-clone `SyncStandin()` calls already live inside
    `UpdateSecondaryMotion`/`EnsureSolvedForSpectatorRender` and follow the
    gate automatically. Verify no other per-frame caller bypasses it.

Result: in the default configuration (~30 FPS camera, no wear, no
HMD-visible clones) the entire avatar CPU cost drops to ~30% of current, in
addition to Fixes 1–2.

## Fix 4 (OPTIONAL, only if still over budget after 1–3) — merge renderers

At avatar build time (`VrmUnityRuntime::BuildMeshes`), primitives sharing the
same material and skin could be combined into one mesh with submeshes, cutting
~87 SkinnedMeshRenderers to ~14. This is a larger change with real risk
(morph-target index remapping, expression bindings, wear-layer
classification, clone pairing all index renderers). Do NOT attempt it in the
same commit as Fixes 1–3. If attempted later, it requires its own plan.

## Verification (all required)

1. Host tests and tooling tests pass (`python tests/ToolingTests.py`, host
   CMake suite).
2. On-device, default configuration (avatar loaded, 30 FPS camera, no wear,
   no clones): frame rate returns to 72, or frame time drops by at least
   6 ms versus the pre-fix build (record both numbers).
3. Spring visuals unchanged: hair/clothing motion and collisions look the
   same at Medium and High spring quality (Fixes 1–2 must be math-identical).
4. Wear Avatar ON: avatar animates at full headset rate (no 30 Hz stutter in
   your own view). Clone set to Headset+Camera: same check on the clone.
5. Wear OFF + clones off: recordings still show smooth 30/60 FPS avatar
   motion (camera-rate posing is exactly camera-synchronized by design).
6. Log one line when the gate state changes (e.g. "Avatar posing at camera
   rate" / "Avatar posing at headset rate") so sessions are diagnosable.
7. `springSolverMilliseconds` in the stats before/after Fix 1+2 on the same
   avatar and settings — include both values in the report.

## Explicitly out of scope

- No changes to solver math, calibration, retargeting, or finger posing.
- No changes to shaders or the shader bundle.
- No changes to the BigScreen repository (reference-only).
- No renderer merging (Fix 4) in this pass.
