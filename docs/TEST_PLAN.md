# Test plan and release gates

## Host tests

- settings defaults, schema migrations, field-level repair, unknown/malformed input, per-subsystem reset, factory reset, and interrupted safe-save recovery;
- camera profile numeric validation and requested-resolution negotiation;
- independently authored Camera2-compatible fixtures: linear/eased interpolation, duration/hold, loop/end, song seek/restart/pause, invalid/oversized/nonfinite input;
- capture timeline pause mapping, PTS monotonicity, scheduler cadence/drop policy;
- state-machine illegal transitions and failure recovery;
- protocol version/packet/parser/fuzz limits and credential-redaction tests.
- private logger dependency: immutable revision/SHA lock validation, safe archive
  extraction, cached-input verification, no direct Paper2 QMOD dependency, no
  Paper2 `DT_NEEDED` entry or leaked wrapper symbols, and support-bundle
  inclusion of current/previous native logs.

## Prompt 2 device smoke gate

After host build/package succeeds: install on the explicitly connected development Quest; verify Beat Saber starts, SaberStage load/version/toolchain lines appear in `saberstage-native.log`, its menu entry appears, a settings document is created, restart reads it, subsystem/factory reset paths work, and repeated restart/exit does not crash. Trigger one recoverable internal test failure where available and verify the prompt appears in front of the active menu, remains clickable, and records context/source detail. Collect a support ZIP and verify both native log slots are present. Prompt 2 contains no camera/media behavior.

## Stage 1 device/media gates

On Quest 2 first: verify actual graphics backend and Unity native texture type; enumerate AVC/AAC codec capabilities; prove one GPU image reaches a MediaCodec input Surface with no CPU readback; prove one independent camera while HMD view stays normal; verify the docked and popout previews show the same menu and gameplay frame and that both are excluded from the captured frame (no panel and no recursive feedback in recordings); enable the movable recording panel and verify its full-surface grab area above the button band, the start (record glyph) and stop buttons pinned fully below the grab area with no hover-hint boxes, elapsed timer, `LOCAL` label and state coloring, the FPS row (Panel FPS Counters toggle rebuilds the panel with capture and headset rates), saved placement, scene persistence, and exclusion from the saved video; record game audio; exercise game pause versus record pause; inspect with `ffprobe` and decode representative frames; measure dropped frames, gameplay frame time, memory, thermal trend, file growth, and A/V drift over 5/30/90-minute sessions. Test low storage, forced stop, app exit, malformed config/script, recenter, tracking loss, map restart/seek, and at least ten repeated sessions.

Stress with common core mods, Chroma/Noodle content, particle-heavy maps, Replay installed, and concurrent hardware video decoding where available. Gameplay regression or crash blocks release even when media looks correct.

## Later-stage gates

Test Wi-Fi loss/reorder/reconnect, USB detach, companion interoperability on Windows/macOS/Linux, remuxed desktop files, OBS ingestion, receiver latency/device matrix, direct-service test broadcasts, credential clear/reinstall behavior, chat rate limits/reconnect, scene switches without encoder restart, and all-sinks failure isolation.

Device claims must include headset model, OS/runtime, Beat Saber build, mod stack, QMOD hash, settings, duration, logs, and media probe output. A build alone is never a device pass.
