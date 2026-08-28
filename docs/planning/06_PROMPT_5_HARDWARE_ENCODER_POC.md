# Prompt 5 — Hardware encoder proof of concept

> Updated decision: the user explicitly approved using Hollywood or a locally built FFmpeg path. Stage 1 now uses Hollywood 1.2.2's proven Quest GPU-to-MediaCodec capture on SaberStage's existing Primary camera. The raw H.264 callback and lifecycle are implemented; headset content/performance validation remains pending. A later direct encoder replaces or extends this only when timestamped packet fan-out is required.

Build a contained GPU-to-hardware-H.264 proof of concept.

Do not implement production recording UI yet.

## Goal

```text
Spectator Camera
→ GPU texture
→ MediaCodec input Surface
→ hardware AVC
→ encoded H.264 packets
```

Encode the user-selected SaberStage camera. Keep preview and encoder demand as separate consumers so showing or hiding the preview does not unexpectedly start or stop recording resources.

## Requirements

- verify graphics backend at runtime;
- use GPU-native/native-texture/EGL path;
- avoid production CPU RGBA readback;
- query/select actual hardware AVC encoder;
- log codec name/profile/level/capabilities;
- configurable resolution/FPS/bitrate;
- honor the selected camera's requested render/output resolution when supported, with explicit validated negotiation rather than silent substitution;
- bounded encoder drain;
- drain off Unity thread;
- encoded packet abstraction;
- preserve codec config/extradata;
- keyframe request;
- clean EOS;
- deterministic teardown;
- safe failure if hardware encoding cannot start.

Test:

- 720p30;
- 1080p30;
- 1080p60 if supported.

For this phase an elementary H.264 output file is acceptable.

Validate with ffprobe/ffmpeg.

Measure incremental cost.

Do not add software H.264 fallback.

The packet abstraction must already be suitable for future:

- MP4 sink;
- RTMP/RTMPS sink;
- Wi-Fi/USB sink;
- Discord-compatible bridge if a supported mechanism later exists.
