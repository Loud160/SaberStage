# Test plan and release gates

## Host tests

- settings defaults, schema migrations, field-level repair, unknown/malformed input, per-subsystem reset, factory reset, and interrupted safe-save recovery;
- camera profile numeric validation and requested-resolution negotiation;
- independently authored Camera2-compatible fixtures: linear/eased interpolation, duration/hold, loop/end, song seek/restart/pause, invalid/oversized/nonfinite input;
- capture timeline pause mapping, PTS monotonicity, scheduler cadence/drop policy;
- state-machine illegal transitions and failure recovery;
- protocol version/packet/parser/fuzz limits and credential-redaction tests.

## Prompt 2 device smoke gate

After host build/package succeeds: install on the explicitly connected development Quest; verify Beat Saber starts, SaberStage load/version/toolchain lines appear, its menu entry appears, a settings document is created, restart reads it, subsystem/factory reset paths work, and repeated restart/exit does not crash. Prompt 2 contains no camera/media behavior.

## Stage 1 device/media gates

On Quest 2 first: verify actual graphics backend and Unity native texture type; enumerate AVC/AAC codec capabilities; prove one GPU image reaches a MediaCodec input Surface with no CPU readback; prove one independent camera while HMD view stays normal; validate preview culling and lifecycle; record game audio; exercise game pause versus record pause; inspect with `ffprobe` and decode representative frames; measure dropped frames, gameplay frame time, memory, thermal trend, file growth, and A/V drift over 5/30/90-minute sessions. Test low storage, forced stop, app exit, malformed config/script, recenter, tracking loss, map restart/seek, and at least ten repeated sessions.

Stress with common core mods, Chroma/Noodle content, particle-heavy maps, Replay installed, and concurrent hardware video decoding where available. Gameplay regression or crash blocks release even when media looks correct.

## Later-stage gates

Test Wi-Fi loss/reorder/reconnect, USB detach, companion interoperability on Windows/macOS/Linux, remuxed desktop files, OBS ingestion, receiver latency/device matrix, direct-service test broadcasts, credential clear/reinstall behavior, chat rate limits/reconnect, avatar asset/IK budgets, scene switches without encoder restart, and all-sinks failure isolation.

Device claims must include headset model, OS/runtime, Beat Saber build, mod stack, QMOD hash, settings, duration, logs, and media probe output. A build alone is never a device pass.
