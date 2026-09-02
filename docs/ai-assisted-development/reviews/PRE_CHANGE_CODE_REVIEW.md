# Pre-change Logger and Reliability Review

## Review snapshot

- Branch base: `c8f785092cab8e3c27b8bbefd7bf23921b1cfe98`
- Baseline host tests: 6 of 6 passed
- Baseline repository/tooling tests: 38 of 38 passed
- Baseline Android build: performed before implementation changes
- Local `SaberStage Device Backups/` and `diagnostics/` directories are not part
  of this review or its commits.

This is a focused source-backed review, not a release-readiness audit. Findings
below require a reachable path in the current implementation; speculative
failure conditions are intentionally excluded.

## Confirmed findings

### 1. SaberStage directly depends on Paper2

**Severity:** High

`include/saberstage/Logging.hpp` constructs a Paper logger directly, while
`qpm.json` and the generated `mod.json` declare `paper2_scotland2` as a direct
dependency. A conflicting Paper2 selected for the shared mod environment can
therefore prevent SaberStage from loading before SaberStage has a working logger
with which to explain the failure.

**Correction:** Statically link Native Logger Quest behind a SaberStage-owned
formatting façade, use its private abort bridge, remove the direct QMOD
dependency, and validate both the ELF and package metadata.

### 2. Two pause-menu hooks allow exceptions to escape

**Severity:** High

`PauseMenuManager_ShowMenu` invokes `MenuShown()` without a boundary.
`PauseMenuManager_OnDestroy` invokes `ForgetUi()` before the original method and
without a boundary. A native exception from either callback can cross a hook
boundary; the destroy hook can additionally prevent Beat Saber's original
cleanup from running.

**Correction:** Guard SaberStage-owned hook work, preserve the original method's
required execution order, and log the exact hook and exception detail.

### 3. `noexcept` application teardown can terminate the game

**Severity:** High

`ApplicationRoot::Stop()` is `noexcept`, but it destroys the menu and calls each
subsystem's shutdown method without local protection. Any thrown destructor or
shutdown exception invokes `std::terminate`; an earlier failure also prevents
later subsystems from releasing their resources.

**Correction:** Stop each subsystem through an independent contextual guard,
reset ownership safely, and continue best-effort teardown after a failure.

### 4. Completed Twitch worker joins are not exception-safe

**Severity:** High

`TwitchService::JoinCompletedWorkers()` is `noexcept` and calls
`std::thread::join()` directly. Although its normal completion flags avoid
blocking the UI, a join failure would call `std::terminate` instead of producing
a diagnostic and allowing other workers to be reaped.

**Correction:** Join each completed worker through a named non-throwing helper
that logs `std::system_error` detail and continues with the remaining workers.

### 5. Logger initialization occurs after normal startup has begun

**Severity:** High

The current Paper façade has no SaberStage-owned initialization or file sink.
There is consequently no independent startup record before IL2CPP setup,
subsystem construction, or hook installation.

**Correction:** Initialize the private logger as the first operation in
`late_load`, use a bounded critical flush for startup failures, and register a
bounded process-exit shutdown.

### 6. `setup` dereferences the loader metadata pointer unconditionally

**Severity:** Medium

`setup(CModInfo* info) noexcept` dereferences `info` without checking it. The
loader is expected to provide a valid pointer, but the function is an external
native boundary declared `noexcept`; a defensive null return is effectively
free and prevents an avoidable process fault if that contract is violated.

**Correction:** Return safely when metadata storage is absent. Logging is not
available yet at this early loader callback.

### 7. High-frequency UI edits synchronously rewrite the settings file

**Severity:** Medium

Camera sliders and several live/avatar adjustment controls synchronously call
the atomic `SettingsService::Save()` path on every value event. Each save writes
and flushes a temporary JSON file, renames the current file to a backup, promotes
the temporary file, and removes the backup. This is real main-thread filesystem
work during continuous controller movement.

**Correction:** Keep in-memory changes and visual feedback immediate, but
coalesce durable writes through a main-thread-owned delayed save that is flushed
when editing stops, the menu closes, or the application shuts down. Explicit
one-shot actions continue to save immediately.

### 8. AFK file selection performs potentially expensive decode inline

**Severity:** Medium

The AFK picker callback calls `PrepareAfkMedia` synchronously. Animated GIF
preparation performs demux, decode, pixel conversion, and bounded frame storage
before creating/uploading the Unity texture. Large valid GIFs can therefore
delay menu interaction even though memory is bounded.

**Correction constraint:** FFmpeg decode can move to a worker, but Unity texture
creation and upload must remain on the Unity main thread. This requires a
two-phase prepared-data handoff rather than wrapping the existing method in a
thread. It will only be changed in this pass if that separation remains narrow
and behavior-preserving; otherwise it will be documented as a deferred
performance item rather than patched unsafely.

### 9. UI callbacks lack a consistent contextual failure boundary

**Severity:** Medium

Some complex update loops and operations have local catches, while many BSML
button, toggle, and slider callbacks invoke multi-step native work directly.
Failures are therefore reported inconsistently and some can escape into Unity
event dispatch.

**Correction:** Add one reusable callback guard at UI registration boundaries.
Expected operation failures continue to use their existing return values and
specific messages; only unexpected native failures are converted into internal
diagnostics and queued user-visible errors.

### 10. Error dialogs do not have one lifetime-safe, frontmost owner

**Severity:** Medium

Feature-specific popups are created by menu code, but startup and background
failures do not have a common path that waits for a stable flow coordinator,
retains the shared prompt across transitions, and brings it to the front. A
background thread must also never present Unity UI directly.

**Correction:** Queue messages under a mutex and present them from a main-thread
tick only when Beat Saber's active flow and shared prompt are stable. Retain the
host and prompt with Unity-safe pointers until dismissal.

### 11. One logger comment is implementation-stale

**Severity:** Low

`TwitchService.cpp` describes a Paper compile-time formatting limitation. That
comment becomes incorrect once the private formatting façade is installed.

**Correction:** Replace it with the actual lifetime/formatting reason or remove
it if the code is self-explanatory.

## Explicitly deferred or constrained items

- Dependencies such as BSML may continue to use Paper2. This pass removes only
  SaberStage's direct dependency and cannot make third-party libraries
  independent of their own declared requirements.
- A native library that fails before Android can load `libsaberstage.so` cannot
  execute SaberStage code or show a SaberStage popup. Package/ELF validation is
  the prevention mechanism for SaberStage-owned dependencies; loader/mod-manager
  diagnostics remain responsible for failures in third-party dependencies.
- Broad architectural rewrites, feature redesigns, and speculative null checks
  are outside this pass.
- Full live/on-device behavior still requires Quest testing after this branch is
  built. Host tests and a successful ARM64 link do not prove Unity UI behavior.
