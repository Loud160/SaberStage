# Stream pause while a map is paused: repair plan and verification

## Evidence and scope

The September 2, 2026 19:08 EDT crash is preserved in
`diagnostics/stream-pause-map-crash/`, including `FINDINGS.md`, native logs,
dependency logs, the tombstone, and the exact installed library. Its SHA-256 is
`036309991665af02374ae8d834848bc6ff1fd1631b31a92ba3d42c7b0cddc4aa`.

AFK binding returned a zero texture handle; SaberStage then opened a modal
parented to its settings-menu view during gameplay. BSML's modal-show hook
caught an unidentified exception and aborted. The stack establishes the popup
as the fatal path. It does not establish precisely when the AFK texture became
invalid or name the object that caused the modal exception.

Only the AFK resource/activation boundary and this error-reporting path are in
scope. Preserve the pending chat scrollbar, panel-size, and audio-icon changes.
Do not modify BigScreen, BSML, unrelated layouts, or broadcast encoding policy.

## Planned changes

1. Replace the duplicate streaming-error modal with the existing ErrorManager
   queue. It presents the native dialog only on a valid, active menu flow;
   gameplay errors remain logged and queued until that flow is available.
   Remove the obsolete modal fields rather than retaining a second unsafe path.
2. Root the AFK texture with SafePtrUnity and protect the native texture using
   DontUnloadUnusedAsset. A non-null C++ address alone is not proof that a
   Unity texture is alive. Clear releases both the texture and its retained root.
3. Make activation report failure, including GIF upload failure, and reprepare
   an invalid cached texture. Never enter AFK/mute state after a failed bind.
4. Validate/log the encoder override boundary separately for a dead object,
   missing capture bridge, zero native handle, and thrown exceptions. Return a
   recoverable error instead of claiming pause succeeded or throwing through a
   Unity button callback.
5. Add regression checks for these contracts, run host/tooling tests and the
   Android build, and document their results separately from headset testing.

## Required headset verification

- Start a stream in menus; pause/resume it.
- Enter a map, pause the map, then pause/resume the stream; repeat after another
  menu/map transition. The AFK card should appear and both audio sources mute.
- Exercise a genuine AFK preparation/binding failure: no settings modal may be
  opened from the world-panel callback in gameplay. The error must remain in
  the log and appear on the safe menu dialog when returning to menus.
- Check that no repeated texture decoding, new modal crash, or leaked AFK
  textures appear during repeated transitions.

## Implemented repair

- `ShowLivestreamActionError` now only enqueues a message through
  `ErrorManager::ReportUserVisible`. Removed the old BSML error-modal fields,
  construction, and `Show()` call. This is removal of the crash-producing
  route, not another catch layered around a hook that calls abort internally.
- `AfkMediaSource` owns a `SafePtrUnity<Texture2D>`, returns null for a dead
  texture, and marks newly created textures `DontUnloadUnusedAsset`. Its clear
  path explicitly destroys a live texture and releases the retained root.
- `Activate()` returns false for missing/dead media or a failed GIF upload.
  `PauseLivestream` does not mute or set AFK state unless activation and GPU
  binding both succeed. Missing/dead cached images are prepared again.
- Preparation runs inside the existing logging guard; an exceptional partial
  decode is discarded. Replacing the AFK image while it is actively selected
  is rejected with a resume-first message to avoid destroying an in-use GPU
  resource.
- `SetOverrideTexture` returns success/error, distinguishes a destroyed Unity
  object, missing encoder, and zero GPU handle, and publishes the new source
  only after validation. Exceptions are logged and returned as errors.
- Resume leaves AFK/mute state intact if restoring the camera fails.

## Verification

- `scripts/test-host.ps1`: passed all 8 host test suites and all 54 Python
  tooling checks. Four added checks cover safe error routing, texture ownership
  and activation, pause/resume state ordering, and GPU-bind failure handling.
  These are host/source-contract checks, not a Unity/Quest runtime simulation.
- `scripts/build.ps1`: passed the ARM64 build and private-logger ELF boundary
  verification. No shader changes or shader-bundle rebuild were needed.
- `git diff --check`: passed.
- Built library: `build/libsaberstage.so`, 6,757,344 bytes. SHA-256:
  `6416f4848638997e665fad64cdf4a3f93ab5a3567addd0317355de3f1b339208`.
- Validation logs: `diagnostics/stream-pause-map-crash/host-tests.log` and
  `diagnostics/stream-pause-map-crash/android-build.log`.
- Deployed to Quest 2 `1WMHH840QJ1046` after the user's explicit request. Beat
  Saber was force-stopped and confirmed stopped before replacement. The shared
  deployment tool verified the mod and all three private FFmpeg libraries by
  SHA-256 and completed the install receipt. The installed mod matches the
  hash above. Beat Saber was left stopped for the user to launch.
- Headset reproduction of paused-map/stream-pause behavior: still pending.
