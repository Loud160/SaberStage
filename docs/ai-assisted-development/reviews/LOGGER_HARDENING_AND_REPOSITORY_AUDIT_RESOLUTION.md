# Logger Hardening and Repository Audit Resolution

## Scope and result

This focused pass started from `Avatar-Framework` commit
`c8f785092cab8e3c27b8bbefd7bf23921b1cfe98`. It did not redesign SaberStage's
camera, recording, streaming, or menu behavior. It replaced the mod's
direct Paper2 logging path, contained confirmed native failure boundaries,
removed repeated settings-file work from continuous sliders, and brought the
repository's public engineering documentation in line with the current system.

Big Screen was read only and served as the established reference for logging,
error reporting, user-visible recovery, documentation structure, and support
workflow. Native Logger Quest was consumed from an immutable, hash-verified
revision rather than copied into SaberStage.

## Resolved findings

### Private logger and dependency isolation

- SaberStage now initializes its own Native Logger Quest instance before
  ordinary IL2CPP, dependency, subsystem, or hook setup.
- The formatter-compatible `Logging::Logger` facade preserves existing call
  sites, attaches C++ source locations, and cannot throw into the caller.
- The backend writes immediate logcat output plus a bounded asynchronous
  current/previous file pair under SaberStage's ModData directory.
- Native Logger Quest is pinned by official commit archive and SHA-256 in
  `dependencies/native-logger.json`. Preparation validates the lock, archive
  root, entry count, extracted size, path safety, duplicate names, and
  symlinks before promoting a cache.
- SaberStage no longer directly declares Paper2 in `qpm.json` or the generated
  QMOD manifest. Dependencies remain free to use their own compatible Paper2.
- Every native build inspects the final AArch64 ELF and rejects a Paper2
  `DT_NEEDED` entry or visible private Paper wrapper symbols.

### Contextual error handling

- One central `ErrorManager` records the operation, source file, line,
  function, and exception detail for unexpected native failures.
- Worker threads may enqueue a message but never touch Unity UI. A persistent
  main-thread runtime waits for a stable active flow before presenting Beat
  Saber's shared prompt.
- The dialog retains its Unity host safely across frames, stays at the front of
  the active hierarchy, and is re-queued if a flow transition invalidates it
  before acknowledgement. A newest-message policy avoids trapping users behind
  a backlog of stale modals.
- Startup, SaberStage hook callbacks, runtime-driver ticks, menu activation and
  deactivation, and ownership teardown now have explicit exception boundaries.
  Expected file/network/user-input failures continue to use their existing
  operation-specific messages rather than being mislabeled as crashes.

### Lifetime and shutdown

- `ApplicationRoot::Stop()` flushes pending settings and tears down each owned
  subsystem independently. One cleanup failure no longer skips all later
  releases or escapes its `noexcept` destructor path.
- Menu destruction clears the globally discoverable controller before Unity
  objects are released, removes callbacks independently, and guards each
  floating-panel and preview cleanup.
- Recording shutdown now preserves the exact failure context and executes its
  cleanup even when an earlier release fails.
- Completed Twitch workers are reaped through a named non-throwing boundary so
  one platform join error is recorded instead of silently terminating from a
  `noexcept` tick or shutdown path.

### Main-thread responsiveness

- Camera and live audio-volume sliders
  still update their runtime state immediately.
- Their JSON encoding, flush, backup rename, and atomic replacement are now
  coalesced behind a 300 ms main-thread timer. A temporary write failure retries
  at most once per second and logs only when its detail changes.
- Explicit destructive, credential, profile, reset, and one-shot controls still
  save synchronously because their success state must be known before the UI
  reports completion.
- Pending edits are flushed before application teardown.

### Support and repository quality

- The support collector includes current and previous SaberStage native logs,
  logcat, device/package information, redacted settings, source receipt/hash
  state, and available crash-file names.
- A filtered Paper2 excerpt remains only for third-party dependency context.
- Build, architecture, lifetime, testing, third-party, provenance, contribution,
  security, and pull-request documentation now describe the actual boundaries.
- The QPM resolution lock is tracked; build products, downloaded sources,
  QMODs, local diagnostics, and device backups remain excluded.

## Deliberately deferred items

- AFK GIF preparation still performs FFmpeg demux/decode before Unity texture
  creation in one menu action. Correctly moving this requires a two-phase
  worker-data/main-thread-texture design; wrapping the current Unity-coupled
  method in a thread would be unsafe and was not attempted in this focused pass.
- This pass cannot catch native memory corruption, faults before Android loads
  `libsaberstage.so`, or failures inside another mod. Package/ELF validation and
  dependency support logs are the available boundaries for those cases.
- Dependencies such as BSML may continue to install and use Paper2. The goal is
  SaberStage-owned logging independence, not interference with another mod's
  logger.

## Verification results

- All 6 host C++ test targets passed.
- All 41 Python tooling and repository-invariant tests passed.
- The release Android ARM64 build linked successfully.
- The ELF verifier confirmed that `libsaberstage.so` is AArch64, has no direct
  Paper2 `DT_NEEDED` entry, and exposes no private Paper compatibility symbols.
- QMOD validation confirmed the exact expected archive payload, an early-mod
  library that byte-matches the ARM64 build, three byte-matching private FFmpeg
  runtimes, and no direct Paper2 manifest dependency.
- Final artifact hashes were:
  - `libsaberstage.so`: `257daddbb7f9e07ccb192e0a20f74761b4a800ff7928ca5ba2206b55e7926925`
  - `SaberStage.qmod`: `fbfba41acf7a281cb6b34a70e0b345b20106c89a1d53e7bc021bca8cf8e8f8dc`
- The final whitespace and path-scope review passed, with local device backups
  and diagnostics remaining outside the commits.

A Quest install was not part of this overnight source pass. The frontmost
error dialog, private log rotation, slider-save cadence, normal menu behavior,
and clean shutdown must therefore still receive the on-device checks in
`docs/TEST_PLAN.md` before this branch is described as device-verified.
