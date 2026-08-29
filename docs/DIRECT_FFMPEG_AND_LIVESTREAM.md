# Direct FFmpeg hardware recording and live streaming

## User-facing behavior

The right-side menu has `Record`, `Live Stream`, and `Files` tabs. Recording backend, resolution, rate, target/peak bitrate, CBR/VBR, hardware tuning priority, H.264 profile/level, keyframe interval, and AAC bitrate are changed under `Record` and apply to the next session. Every interactive control has an average-user hover explanation. Settings that Hollywood does not expose—peak bitrate, rate-control mode, tuning, profile, level, keyframe interval, and AAC bitrate—stay visible but are disabled while Hollywood is selected, so the UI does not imply they are being honored.

`Hollywood` preserves the established MediaCodec capture path. `Direct FFmpeg (Hardware)` uses SaberStage's private runtime and is required for live streaming. Both are hardware-video-only; SaberStage never starts a software H.264 encoder. 1440p and aggressive profile/level combinations are exposed as requested but remain capability-dependent and fail safely when MediaCodec rejects them.

`Go Live` requires a session stream key. If local recording is idle, it begins one continuous local safety recording and sends that same encoded video to the network sink. `Stop Stream` ends only network output. `Stop & Save` ends both and finalizes the local MP4. Local pause is disabled while live because a public broadcast cannot pause its timeline.

## Performance isolation

- The Unity audio callback performs only a bounded copy into a preallocated SPSC ring.
- WAV writes, PCM conversion, and live audio fan-out run on an audio worker.
- Encoded H.264 file writes run on a bounded writer worker, not an encoder callback.
- Network writes and AAC encoding run on the livestream worker.
- The Direct FFmpeg EGL bridge restores Unity's render context on every setup failure and tears its MediaCodec surface down only after the encoder worker has drained.
- Video and audio network queues are bounded; overload drops broadcast packets before blocking gameplay or local capture.
- The preview capture-exclusion renderer lists are rebuilt only when panels are attached, created, or removed—not for every spectator frame.
- A 90-frame refresh cadence replaces per-frame global AudioListener enumeration; the capture listener still follows the HMD pose each frame.

These changes address identified structural stalls, but they do not establish a Quest performance result. Quest 2/3 testing must measure camera-only, avatar-only, each backend at 720p30 and 1080p30, combined avatar/recording, and controlled test broadcasts. 1080p30 remains the conservative default; 60 FPS and 1440p require evidence before being recommended.

## Private runtime and licensing

`scripts/build-ffmpeg-hardware.ps1` invokes the reproducible WSL build. It downloads SHA-256-pinned FFmpeg 9.0.1 and Mbed TLS 3.6.7 sources, builds for Android API 29/ARM64, and stages only `libavformat`, `libavcodec`, and `libavutil` shared libraries. Enabled functionality is MediaCodec H.264, native AAC, H.264/WAV input, MP4/FLV output, and file/TCP/TLS/RTMP/RTMPS protocols. GPL, nonfree, and x264 features are machine-checked off. Mbed TLS 3 requires FFmpeg's LGPLv3 configuration.

The build also emits exact configuration, upstream licenses, source hashes, and the small SaberStage patch diff. FFmpeg receives a private `-saberstage9` SONAME suffix and `SABERSTAGE9` symbol version namespace so it cannot bind to Hollywood's runtime by accident.

## Security boundary

TLS certificate verification is enabled for RTMPS against Android's system trust directory. Stream keys are rejected when they contain whitespace/control characters, masked after entry, excluded from settings JSON and diagnostics, and cleared from owned memory on shutdown. They are session-only. Secure persistent storage needs a reviewed Android Keystore integration and is not silently approximated with plaintext or reversible local storage.

## Unverified device boundary

No headset was accessed for this work. Before release, device testing must establish:

- MediaCodec surface input, RGB orientation, color, and exact frame pacing;
- 720p30 and 1080p30 local Direct FFmpeg recording with desktop `ffprobe`/decode inspection;
- Hollywood regression behavior after moving audio and H.264 disk I/O off callbacks;
- Twitch, YouTube, Kick, and custom endpoint connection behavior using test channels;
- FLV H.264 sequence headers/access units, AAC, A/V sync, stop/trailer handling, TLS certificates, and reconnect;
- queue depth, dropped-media telemetry, memory growth, thermals, and gameplay frame rate with the avatar enabled;
- whether 1080p60 or 1440p modes are acceptable on each supported Quest generation.
