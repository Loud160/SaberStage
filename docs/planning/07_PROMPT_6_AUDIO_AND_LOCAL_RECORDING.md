# Prompt 6 — Implement synchronized audio and local recording

> Current implementation checkpoint: Primary-camera H.264, persistent HMD-positioned game-audio capture, unique partial files, Hollywood/FFmpeg MP4 finalization, retained failure artifacts, and ADB retrieval tooling are implemented. Start records immediately by default, and the same capture session survives menu, loading, gameplay, and results scene changes until stopped. Optional Gameplay Only mode arms in menus and stops after gameplay. Headset validation, A/V sync measurement, pause timing, long-duration behavior, low-storage handling, and recovery hardening remain pending.

Turn the video encoder path into a real local recorder.

This is the feature that makes the first SaberStage release useful: the saved file must contain the selected independent camera view, not the player's HMD mirror.

Implement:

- Beat Saber/game audio capture;
- audio encoding;
- shared A/V capture timeline;
- local recording sink;
- final common container such as MP4;
- safe session init/finalization;
- unique sanitized filenames;
- `.partial`/incomplete active state;
- low-storage/write handling;
- diagnostics.
- safe retrieval over USB/ADB using normal Quest storage conventions.

The default recording lifecycle is broadcast-like: pressing Start begins capture immediately, and ordinary scene changes never split or stop it. Menu time, song selection, loading, gameplay, pause menus, and results remain in the same file until the user stops. `Gameplay Only` is an optional persisted mode and defaults off.

The encoder still emits generic encoded packets. The local recording sink is a consumer.

Do not bury muxing inside CameraManager.

## Pause timing

Recording pause/resume must remove paused capture time from both audio and video.

The final timeline must remain continuous and monotonic.

Beat Saber gameplay pause remains independent.

## Set-it-and-forget-it

Remember:

- selected quality preset;
- bitrate;
- audio setting;
- output settings.

These restore automatically next launch.

## Validation

Test:

- menu recording;
- one continuous menu -> gameplay -> results recording;
- optional gameplay-only recording;
- gameplay recording;
- 20+ minute recording;
- start/end A/V sync;
- long-term drift;
- monotonic timestamps;
- playable output;
- ffprobe validation;
- abnormal stop behavior.

Do not implement networking yet.
