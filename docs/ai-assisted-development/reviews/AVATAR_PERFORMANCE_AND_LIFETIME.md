# Avatar performance and lifetime correction pass

## Scope and starting evidence

Requested September 3, 2026, after reviewing `docs/AVATAR_PERFORMANCE_FIX_PLAN.md`.
Implement the reviewed collider/transform optimizations and corrected consumer-rate
posing. Also investigate and correct concrete lifetime/quality-update failures and
the blocking avatar load/unload path. Preserve IK, calibration, shader behavior,
and the pending rich-chat work. BigScreen remains read-only. No deployment is
authorized by this request.

User benchmark: even low-quality springs with colliders enabled cost much more
than Ultra springs without colliders. Outline rendering also costs time, but
substantially less than colliders. Quality changes on a loaded avatar and repeated
load/unload can crash; load/unload visibly blocks the UI for 0.5–1 second or more.
These observations prioritize investigation; they do not establish a crash stack.
The Quest was not available to ADB at the start of this pass.

## Work order / implementation constraints

1. Inspect current live-quality, ownership, and destruction paths before editing.
   Collect current support/crash logs if the Quest becomes available. Distinguish
   source-confirmed bugs from an unverified explanation of a particular crash.
2. Make spring collision arithmetic native and cache collider world centers once
   per substep where bone ancestry allows it. Colliders beneath simulated joints
   must retain correct updates. Preallocate storage outside the inner loop.
   Preserve collision order, radii, double reprojection, and joint rest reset.
3. Use a combined position/rotation read after the joint reset; reuse the rotation
   and calculate the axis in native math. Add deterministic math/cache tests.
4. Separate normal frame update from forced camera update. Pose camera-only
   avatars immediately before that camera renders, but retain full-rate posing
   for worn avatars, HMD-visible clones, and the isolated grip-editing arm.
   Account for secondary elapsed time once, preserve fixed-rate physics and
   bounded catch-up, and reset on long gaps. Do not gate tracking sampling.
5. Correct evidenced ownership/live-quality bugs and remove unnecessary repeated
   quality work. Keep managed Unity objects alive for the duration of native
   ownership and release render consumers before their shared resources.
   Inspect synchronous import stages; keep Unity construction on the main thread
   and move or schedule only work that is safe to move or spread across frames.
6. Add bounded phase/counter diagnostics for load, unload, material updates,
   pose writes, spring steps, and collider cache behavior. No per-joint logging.
7. Run host tests, tooling tests, Android build, and package verification. Record
   measured results and remaining on-device checks below. Do not claim FPS gains,
   crash resolution, or unchanged visual behavior without device evidence.

## Review corrections to the supplied plan

- Duplicate tracking/solve suppression already exists; count actual solves rather
  than assume 72 + 30 full solves per second.
- Springs already use a fixed-rate accumulator. Camera-rate dispatch does not
  itself reduce the required number of physics substeps.
- A gate inside the shared solve method needs an explicit camera bypass.
- The isolated grip-editing arm is another HMD-visible consumer.
- Collider descendants of simulated joints cannot blindly use a stale cache.
- Existing native solver time excludes Unity pose writes. Measure both.
- Renderer merging remains outside this pass; the shader bundle is not changed
  for this work.

## Results

Follow-up: this pass's build was subsequently deployed at the user's request.
The user reported worse map FPS afterward, including with springs/outlines off.
See [the FPS regression investigation](AVATAR_FPS_REGRESSION_2026-09-03.md).
The validation below records the original build checks, not a confirmed
on-device performance improvement.

### Crash evidence collected

Private support archive and extracted session:
`diagnostics/avatar-performance-review/SaberStage-Support-Logs-20260903-003342.zip`
and `diagnostics/avatar-performance-review/session-003342/`.
The latter includes all three surviving app tombstones, system logcat, the mod's
log, redacted settings and a copy of the installed library for matching symbols.
Do not commit these private captures.

- `tombstone_01`, 2026-09-03 00:17:15 local: `Material::SetFloat` /
  `Material_CUSTOM_SetFloatImpl`, followed by SaberStage's
  `AvatarManager::ApplyAvatarSettings` and BSML's dropdown callback. The native
  material pointer is invalid (`x0 = 0x3000000001`). This is not a frame-budget
  timeout.
- Repeated earlier logcat traces (including 00:00 and 00:03) end in
  `Scripting::GetInstanceIDFor` / `Object_CUSTOM_Destroy`, with SaberStage's
  `VrmUnityRuntime` destructor in the caller chain.
- Other teardown traces name ClockMod/BeatLeader/Qounters code. These are not
  silently attributed to SaberStage or modified by this pass.
- Black Heart logs show 87 main renderers, 164,549 vertices, 22 authored
  colliders, and 146 spring-affected bones. One load logged 81.5 ms parsing and
  911.1 ms Unity construction; another logged 202.7 ms and 891.6 ms. These are
  existing-build baselines, not post-fix measurements.

### Changes implemented

- Native spring vector arithmetic and ordered sphere/bone reprojection, combined
  position/rotation reads, cached world rotation, and fixed-collider center cache.
  Imported ancestry conservatively excludes simulated descendants from caching.
- Consumer-rate posing with an explicit camera entry, isolated-arm visibility,
  per-frame time accounting, fixed-rate catch-up at 30/60/72/90Hz dispatch, and
  one-second spring work counters plus five-second performance logs.
- GC roots for cached Unity wrappers, including inactive outline materials,
  meshes, textures, renderers, nodes and clone references. Native asset retention
  flags prevent unused-feature assets being reclaimed by scene asset sweeps.
  `DontDestroyOnLoad` alone was not a managed-wrapper lifetime guarantee.
- Shader updates only when material settings actually change, detailed bounded
  material-update logging and a useful error popup. Clone material slot arrays
  are updated when outlines change, not just the source renderer's slots.
- Native file parsing on an owned worker; node/texture/primitive construction in
  main-thread cooperative slices. The source avatar is not published until all
  construction and binding succeed. Completion applies the current profile and
  starts calibration only after the humanoid is ready. Startup and profile
  switching use this same asynchronous path.
- Unload hides/unbinds immediately, cancels outstanding completion callbacks,
  and releases Unity resources over subsequent frames. Cancelled parser futures
  remain owned until ready rather than blocking a UI callback with a join.
  Rapid requests are bounded and old retirement completes before new Unity
  construction. The blocking legacy runtime loader was removed.
- Texture Limit intentionally remains a next-load option, as its existing UI
  tooltip states; it is not silently presented as an instantaneous resize.

### Verification

- Host suite: 14/14 passed, including new native runtime policy tests.
- Tooling/repository tests: 56/56 passed.
- Native runtime policy tests also passed AddressSanitizer and
  UndefinedBehaviorSanitizer on the host.
- Final Quest ARM64 build and private-logger boundary verification passed.
- QMOD packaging, its second build, and package verification all passed.
- Whitespace/error-marker diff validation passed. Existing unrelated rich-chat
  changes remain in the working tree; this pass does not commit or discard them.
- No changes deployed. FPS improvement, live quality stability, repeated
  load/unload/GC behavior, and visual equivalence still require Quest testing.
  Individual Unity texture decode/upload or humanoid-builder operations cannot
  be preempted mid-call: slices above 15ms are logged rather than promised away.

Build artifacts (SHA-256, September 3, 2026):

- `build/libsaberstage.so`:
  `e671c8d3d14f4d836f195da11e183781ac89c7e23d2df78f5bf5750788e55572`
- `SaberStage.qmod`:
  `9a72e25ef6712b0afe96e3064e950116c4a50559c2960b943e1fb260e213b4bf`

### Next on-device verification

1. Change material quality options with the avatar loaded, including outline
   off/on and clone visibility. Check material phase logs and watch for invalid
   objects or stale outline slots. Texture Limit takes effect on the next load.
2. Repeatedly load/unload and change calibrated profiles, including cancelling
   a load. Verify the UI remains usable and the new avatar is published only
   when ready; inspect any construction slices exceeding 15ms.
3. Repeat the same map/settings benchmark with colliders on and off. Compare
   spring milliseconds, center reads, collision tests, actual pose write count,
   and HMD frame rate; no post-fix performance claim is made here.
4. Compare camera-only rendering, wearing, clones, and isolated grip-arm editing
   at different camera rates. Springs should maintain their configured cadence
   and reset safely after a long gap; controller tracking remains full-rate.
