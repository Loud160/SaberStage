# Camera motion and preview performance — September 3, 2026

## Scope and evidence

User reports that enabling **Enable Script** roughly halves menu FPS, without
recording or streaming. Read-only support capture:
`diagnostics/camera-motion-performance/SaberStage-Support-Logs-20260903-035407.zip`.
Keep support files and settings private. The log identifies
a 241-frame song-synchronized, looping movement script.

Confirmed source problems:

- `CameraManager::SampleMovementScript` searches all Unity objects each frame
  when no song clock exists. Menus have no active gameplay song clock.
- Player-root fallback repeats unsuccessful global searches in player-follow
  mode. Runtime discovery belongs to scene lifecycle with bounded retries.
- Every camera setting change reloads/parses the movement JSON, including
  repeated slider callbacks unrelated to the selected script.
- Floor preview requests are fixed at 1920x1080/15; movable preview requests
  are fixed at 512x288/15. They already share one renderer.
- The menu coordinator already detaches the floor preview on Back and
  deactivation, but fallible Unity cleanup occurs before removing its demand.
- Expanded cadence tests found an existing scheduler rounding error: an early
  deadline accepted by the epsilon check retained almost a full interval via
  `fmod`, causing an extra render on the next headset tick. Ten of the 24 tested
  FPS/refresh-rate combinations failed before the correction.

The lookup behavior is proven in source; how much of the reported FPS loss it
accounts for must be measured on the headset after the fix.

## Authorized implementation plan

1. Cache gameplay-scene state from lifecycle events. Perform no song-clock or
   player-root global discovery in menus. Use bounded main-thread discovery
   retries during gameplay startup/reacquisition; never move Unity work onto a
   background thread. Reset cache/timing state at scene/camera transitions.
2. Reload a movement file only when its filename or enable state changes.
   Preserve smooth movement, song seeking and scene clock resets. Toggling the
   script off/on can explicitly reload edits to the same file.
3. Add low-frequency motion/render CPU timing and lookup-count summaries, gated
   by Diagnostics. CPU wall time is not an asynchronous GPU measurement.
4. User confirmed **separate floor and movable resolution/FPS dropdowns** in the
   existing Preview tab. Use the proven camera dropdown row/layout helper, hover
   tips and separate Floor Preview / Movable Preview groups. Preserve current
   defaults, placement and recording/stream settings. Offer 512x288, 960x540,
   1280x720 and 1920x1080; rates 5, 10, 15, 24, 30 and 60 FPS.
5. Persist and validate each preview's values independently. Settings missing
   the new fields retain the prior effective defaults. A visible recording or
   stream remains the renderer owner; previews reuse its existing output.
6. Release floor demand before fallible cleanup on menu exit. Hide/clear its
   image and placement gizmo; detect a destroyed/hidden floor panel as a backup.
   Closing the menu must leave only the enabled popout preview's demand, or no
   preview demand at all. Never terminate an active recording/stream.
7. Native coverage: zero menu discovery, bounded startup retries, script cache
   invalidation, preview size/rate validation, menu close/reopen and recording
   coexistence. Tooling checks: actual runtime/UI wiring and fail-closed cleanup.
   Run host/tooling tests, ARM64 build and package verification.

## Boundaries

- Preserve Claude Code's and all existing uncommitted work. BigScreen read-only.
- No unrelated UI layout, encoder, or shader changes.
- No deployment or new commit requested in this turn.
- Do not claim FPS recovery or visual correctness from host/build checks alone.

## Results

### Implemented

- `include/saberstage/camera/RuntimeWorkPolicy.hpp`: data-only retry and
  script-selection policies. Gameplay source discovery is forbidden in menus;
  an unavailable gameplay source is retried at most four times per second.
- `src/camera/CameraManager.cpp`: cache gameplay-scene state at lifecycle
  transitions, invalidate clock/player references with camera/scene changes,
  and retain parsed script frames across unrelated settings edits and scene
  transitions. Song-synchronized movement still uses the actual song clock;
  unsynchronized scripts still run in menus. Off/on explicitly reloads a file.
- Diagnostics now produce a `CameraWork` summary every five seconds with
  song/player lookup counts, file loads, motion CPU time, render-scheduling CPU
  time, preview render count, combined demand and encoder ownership. These
  counters distinguish repeated discovery from actual rendering cost.
- `src/camera/FrameDemand.cpp`: accepts preview demands down to 5 FPS and
  consumes an epsilon-early deadline completely instead of retaining a nearly
  full interval. Existing bounded-debt behavior after a hitch is preserved.
- `include/saberstage/preview/PreviewRenderPolicy.hpp`: one shared preset
  definition for UI/runtime validation. Both monitors offer the four listed
  resolutions and six listed rates. Defaults remain floor 1080p/15 FPS and
  movable 512x288/15 FPS, so existing installations do not silently change.
- Settings schema 28 persists four separate fields under `preview`:
  `floorResolutionWidth`, `floorFramesPerSecond`, `floatingResolutionWidth`,
  `floatingFramesPerSecond`. Heights derive from the supported 16:9 presets.
  Invalid/missing values fall back to each monitor's own defaults.
- The existing Preview tab uses native camera dropdown rows in **Floor Preview
  (Menu Only)** and **Movable Preview** groups. Hover tips explain performance
  cost and render sharing. Callbacks save preview settings and update demand;
  they do not change recording settings or reload camera scripts.
- `src/preview/PreviewManager.cpp`: detachment clears floor demand before Unity
  visual/cache cleanup. It hides/clears the floor image and its placement gizmo.
  Runtime visibility checks also remove demand if the view becomes inactive
  without the normal callback. Reopening reactivates the image with its saved
  quality. Floating-preview reset preserves both monitors' quality settings.

The previews retain the existing **one-renderer** design: with both visible,
the higher requested dimensions and FPS are shared. Closing the mod menu leaves
only the enabled movable preview's demand. During recording/streaming, previews
reuse the encoder's output rather than adding another camera render or changing
the encoder's selected resolution/rate. Separate saved settings therefore avoid
an unnecessarily expensive movable preview after leaving the menu; they do not
introduce two independent renders.

### Validation

- All 14 native test suites passed, including independent settings round-trip,
  old-settings defaults, invalid settings, zero menu discovery, bounded retries,
  script-selection invalidation, menu close/reopen demand, capture coexistence,
  and all 24 combinations of preview FPS with 72/80/90/120 Hz headset updates.
- All 63 repository/tooling checks passed, including actual runtime/UI wiring
  and floor-demand removal order.
- `git diff --check` passed.
- ARM64 incremental build passed; private-logger ELF verification and
  `tests/VerifyPackage.py` passed after rebuilding `SaberStage.qmod`.
  Build logs: `diagnostics/camera-motion-performance/build.txt` and
  `diagnostics/camera-motion-performance/build-final.txt`.
- Final library SHA-256:
  `89ac6c9c3396646c3beddcbf0ab1ab65b0d346744aacaf2d40d217b03efb65ed`.
- Final QMOD SHA-256:
  `67b22747ba7d9c48a4144d5d5e6cdc68d55d5c0de74137d5a0383bfc17f4e0ae`.

### Headset verification still required

1. Compare Enable Script off/on in the same menu, with identical previews,
   capture state and headset refresh rate. With Diagnostics enabled,
   `CameraWork` must show zero `songLookups`/`playerLookups` in the menu and no
   repeated `fileLoads` when changing unrelated camera settings.
2. Enter a map and verify the song-synchronized path still moves/seeks/loops
   correctly; return to the menu and verify lookups stop again.
3. Verify both dropdown groups are readable/clickable within the existing
   panel bounds and persist independently after restarting the game.
4. Set floor 1080p/30 and movable 512x288/5. Close SaberStage's menu: the log
   should show floor detachment, and combined demand should fall to 512x288/5
   while the movable preview remains visible. Turn the movable preview off:
   without capture there should be no active render demand or preview renders.
5. Reopen the menu and verify the floor image resumes at its saved quality.
   Repeat while recording/streaming: capture must continue at its selected
   quality, with neither preview monitor appearing in the captured image.

No FPS improvement or on-headset layout has been claimed from host tests.
No deployment, commit, or unrelated menu edits were performed.
