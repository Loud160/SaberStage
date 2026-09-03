# Recording cadence and gameplay tracking repair

Scope: investigate and fix the two failures reported on September 2, 2026. Do not change the chat redesign, UI layout, saved fit settings, or calibration measurements. Device installation requires a separate go-ahead.

## Evidence collected before editing

- Baseline: `1d12b80`, branch `logger-hardening-and-repo-audit`.
- Installed library SHA-256: `6416f4848638997e665fad64cdf4a3f93ab5a3567addd0317355de3f1b339208`.
- Local-only support capture: `diagnostics/recording-and-gameplay-avatar/support-204331/`; native log, redacted settings, and separately pulled `PlayerCalibration.json`.
- Local MP4s in the parent diagnostic directory: `SaberStage_2026-09-02_20-22-56.mp4`, `SaberStage_2026-09-02_20-23-53.mp4`, and `SaberStage_2026-09-02_20-29-41.mp4`.
- FFprobe and full decode: 140, 100, and 557 valid frames respectively; achieved average frame rates about 17.0, 16.3, and 24.5 FPS despite a 30 FPS target. Video/audio durations agree closely. These are real missing temporal samples, not loss of every encoded frame.
- FFmpeg bridge counts match from scheduled frames through encoded packets, with zero encoder drops, GL/EGL failures, or writer failures. Hollywood also reports skipped presentation deadlines. All capture targets use **1x MSAA**. Bitrate or 4x MSAA is not an evidence-backed explanation.
- Livestream ran without local output. Its 13,639 captured frames reached the output without network video queue drops. There is no local gameplay clip proving the folded pose visually.
- Profile 1 has a valid completed basic calibration. Do not ask the user to recalibrate as a substitute for fixing scene handling.
- Direct XR/menu tracking was acquired at 20:33:17. MainMenu -> GameCore at 20:33:53; no PlayerTransforms acquisition followed. Gameplay saber handles were acquired, so the solver could combine retained menu head/root transforms with gameplay hand targets. GameCore -> MainMenu at 20:38:12 coincides with the user's reported return to normal.

## Source findings and implementation plan

1. **Tracking handoff:** direct XR readiness checks only object lifetime, unlike the other tracking paths. Reject inactive/stale cameras and roots; invalidate cached scene sources at scene changes; retry gameplay PlayerTransforms while a fallback is in use. Never write a stale solved sample while tracking is unavailable.
2. **Calibration coordinates:** move neutral poses into the new tracking-origin coordinate frame on a handoff/recenter. Preserve measured standing height and controller offsets; a crouched transition must not become a new standing calibration. Reset solver history and velocity baselines at handoff.
3. **Capture accounting:** distinguish skipped camera deadlines, encoder queue loss, and live network loss. A stopped/previous livestream must not contribute to a local recording's counters. Preserve segment totals over pause/resume.
4. **Capture diagnostics:** add bounded aggregate timing for spectator preparation/render callbacks and direct render bridge/surface presentation. Record average/max timing and stage counters, not per-frame log spam. CPU callback wall time is not a GPU timer; report it accordingly.
5. **Concrete capture hot-path defects:** review GL state restoration and avoid unnecessary recurring work only where the current source demonstrates it. Do not hide real skipped deadlines, rewrite timestamps to disguise missing frames, lower the user's quality settings, or promise a performance cure before measuring it.

## Additional observations / limits

- Existing backward-spine protection bounds pelvis/head displacement, not every solved joint angle. The circular-arc solver can still curve under compressed endpoints. Fix input coordinate coherence first; a full curvature-policy change is not an established cause of this menu/gameplay regression.
- Extracted menu video still contains preview feedback/control panels. This is an existing render-exclusion defect, not proof that it caused the measured slowdown. Avoid speculative changes to panel layout/materials during this repair.
- The current calibration log uses a legacy retargeting overload; its wording alone does not prove arm-span sizing was inactive.

## Validation and handoff

- Add host regressions for coordinate rebasing, preserved dimensions/offsets, scene-source readiness policy, and recording segment/drop accounting as appropriate.
- Run all host and tooling tests, build ARM64, inspect the final diff.
- Record the exact fixes and results here. On-device acceptance remains: upright avatar through menu/map/results; live logs showing active tracking source and coherent head/floor; local capture timing/actual FPS on the same workload using both backends.
- Keep raw support captures, videos, and private calibration data out of commits.

## Implemented fixes

- `AvatarManager.cpp`: scene-handle invalidation, active-HMD/root checks, gameplay source promotion, origin rebasing, solver/velocity reseeding, and suppression of stale writes while sources are unavailable. Acquisition/rebase logs name the source and record head/origin/floor/standing-height values.
- `Calibration.cpp`: pure coordinate-frame rebasing moves neutral head/hands/forward/floor without changing standing height or controller-local grip offsets. No profile file migration or recalibration is required.
- `AvatarSolver.cpp`: fixed a separate **rotation-order defect**. `AlignBone` already aligned a spine segment to the solved world-space direction, but subsequent body yaw rotated that tilt again. At 180 degrees, forward tilt could become backward skin rotation even with correct joint positions. The reference pose is now yawed first, then aligned to the solved segment.
- `PoseTypes.cpp` / `AvatarManager.cpp`: exact duplicate pre-render samples in the same Unity frame reuse the existing pose/velocity/sequence instead of repeating IK and finger writes. New frames, changed head/hand/controller poses, and changed grip sources still solve immediately. This is not a tracking-rate reduction.
- `RecordingController.cpp`: capture-deadline loss and encoder-queue loss are separate from live network loss. Finished Direct FFmpeg segment totals survive pause/resume. Only the live view adds current live network drops; local totals cannot inherit a previous stream's queue counters.
- `DirectFfmpegCapture.cpp`: restores separate read/draw framebuffer bindings after rendering to the MediaCodec surface; caches the shader uniform location instead of looking it up for each frame. Requests swap interval zero **on the encoder surface only**, leaving the headset surface/settings alone. Logs expose the EGL config's minimum interval and any request failure. EGL may clamp the request; this is not a claim that swaps can never block. [Khronos EGL reference](https://www.khronos.org/files/egl-1-4-quick-reference-card.pdf).
- `CameraManager.cpp` / recording bridge: bounded aggregate CPU wall-time counters for avatar preparation, spectator callbacks, encoder bridge and surface swap, plus Unity frame cadence and per-stage loss counts. One log snapshot every five seconds and at segment stop, including short clips. No per-frame log writes or GPU readbacks were added. These are cumulative session/segment statistics, not GPU execution timings.

## Validation results so far

- The new turned-spine regression failed before the rotation-order correction (`yaw=1.5708`, first spine segment dot alignment `0.984699`) and passed afterward. It checks skinned-bone orientation against solved child directions at 0, +90, -90, and 180 degrees.
- New host regressions cover neutral-frame round trips with translated/rotated origins, a crouched handoff preserving standing dimensions, unchanged local grip offsets, invalid-origin rejection, and same-frame pose deduplication without suppressing late movement.
- All **8 host suites** and **56 Python tooling checks** pass. The stage-accounting/GL restoration checks are source invariants; they do not emulate Android MediaCodec or Unity scene lifetime.
- First ARM64 compile exposed generated Unity Scene getters that are non-const. Corrected the local scene value's qualifier; final ARM64 build and private-logger ELF validation passed. The final readiness check matches the head/camera hierarchy, without assuming the authoritative origin reference must be its ancestor.
- Built artifact: `build/libsaberstage.so`, 6,768,992 bytes, SHA-256 `8e277ba4fcfac8053d85e7873dc8888c10c75568dc66787fbc57ef20a319e10b`. Build logs: `diagnostics/recording-and-gameplay-avatar/repair-build.log` and `repair-final-build.log`; tooling results: `repair-tooling-tests.log`.
- Not deployed in this turn. No changes to device settings, saved player/avatar profiles, the shader bundle, or chat/menu layout. The separate rich-chat planning document's existing edits were preserved.
- Still required on device: verify scene acquisition and avatar orientation with Profile 1; compare actual local recording cadence using both backends and inspect the new timing breakdown. The prior clips prove capture starvation, but do not identify its dominant CPU/GPU cost. Do not claim the overall frame-rate problem resolved based solely on compilation/tests.
