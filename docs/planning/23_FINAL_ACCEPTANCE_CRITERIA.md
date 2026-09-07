# SaberStage Acceptance Criteria

Acceptance is staged. Later product goals do not prevent an earlier stage from being released, but no stage may be described as complete without direct evidence for its own criteria.

## Stage 1 — First working native Quest release

```text
✓ Player plays Beat Saber normally in first person.
✓ Independent spectator camera renders a configurable third-person view.
✓ HMD camera transforms remain untouched.
✓ Camera concepts and ordinary configuration are meaningfully familiar to Camera2 users.

✓ SaberStage uses one real native left-side menu panel with a compact in-panel header, native segmented camera tabs, and native scrollable settings pages; center and right remain available for later camera and recording/streaming panels.
✓ First release uses one user camera while preserving a clean path to additional cameras later.
✓ User can place that camera anywhere practical and adjust position, rotation, FOV, requested resolution, smoothing, and other appropriate detailed controls.
✓ Optional anchored-float mode moves the camera smoothly side to side from look direction without modifying the saved base placement or following head jitter directly.
✓ Supported Camera2 movement scripts run without manual rewriting and can deterministically control compatible properties such as position, rotation, and FOV from song time.
✓ Malformed or partly unsupported scripts fail safely with clear diagnostics and never affect HMD camera transforms.
✓ Optional HMD-only preview panel can be shown, moved, rotated, resized, hidden, and reset.
✓ Preview does not appear in saved output by default.
✓ Camera and preview restore correctly every launch.
✓ Normal Quest recenter does not require routine camera recalibration.
✓ Hardware H.264 encoding is used without silent software fallback.
✓ Game audio is synchronized to the selected third-person camera recording.
✓ 1080p30 local recording works on Quest 2.
✓ Start/Pause/Resume/Stop work from appropriate contexts.
✓ Beat Saber pause and recording pause are independent.
✓ Recordings finalize safely, are retrievable, play correctly, and pass media validation.
✓ Heavy modded maps and concurrent hardware video decode have been tested.
✓ Repeated recordings and game sessions do not leak or crash.
```

## Stage 2 — Companion and remote viewing

```text
✓ Versioned media/control protocol preserves selected camera identity and synchronized A/V.
✓ Wi-Fi streaming works to the SaberStage Companion.
✓ USB streaming works through a cross-platform-supported path without a second media architecture.
✓ Companion runs on Windows, macOS, and Linux.
✓ Companion can view and locally record the selected third-person stream.
✓ Companion does not require PC Beat Saber, PCVR-capable hardware, or Meta's Windows runtime.
✓ Pairing, reconnection, and last-working settings restore without repeated manual setup.
✓ OBS can consume a stable cross-platform companion output.
✓ Direct TV/receiver output has a documented feasibility result.
✓ Any claimed TV compatibility has been tested on representative real devices.
✓ Remote receiver failure does not freeze gameplay or poison local recording.
```

## Stage 3 — Full broadcast production

```text
✓ Broadcast scenes switch without rebuilding the encoder.
✓ Lightweight Quest-native overlays and production controls fit the existing SaberStage UI coherently.
✓ Direct Quest livestreaming works without a PC.
✓ Stream credentials are protected and never exposed in normal logs.
✓ Network loss does not freeze gameplay.
✓ Local recording and direct streaming share one encode when profiles are compatible.
✓ Twitch/YouTube chat can be displayed in an HMD-only panel.
✓ Chat restores/reconnects without repeated placement or setup.
✓ Discord integration uses only supported/public mechanisms.
✓ If Quest-local Discord video injection is supported, it reuses the selected SaberStage camera/scene without a second full scene render.
✓ If Discord video injection is unsupported, the limitation is documented and no private API, client patch, self-bot, or unsafe workaround is used.
✓ Full-system Quest 2 stress and repeated-session tests pass for every supported combination.
```

## Architecture and product acceptance

```text
✓ Camera, preview, compositor, capture, output, chat, and Discord responsibilities remain separated.
✓ One camera/compositor and encoded-media architecture feeds compatible sinks.
✓ No unbounded queue or uncontrolled hot-path allocation is accepted.
✓ Gameplay is favored over capture or receiver quality under overload.
✓ No core Quest feature requires a PC.
✓ No companion feature requires Meta's Windows desktop runtime.
✓ Unfinished features do not appear as fake controls.
✓ Advanced control remains available without making normal operation complicated.
✓ Documentation distinguishes verified behavior, platform limitations, and future design.
```

The finished product should feel like an appliance without behaving like a stripped-down tool:

> **Set it up once, retain full control when wanted, launch Beat Saber later, and SaberStage is already ready to present, record, or broadcast the selected third-person view.**
