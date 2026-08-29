# Prompt 17 — Direct Quest livestream architecture and proof of concept

> Implementation checkpoint: the right panel now has a functional `Live Stream` tab for Twitch, YouTube, Kick, and custom RTMP/RTMPS endpoints. `DirectLivestreamSink` uses bounded asynchronous queues, the required state machine, reconnect backoff, visible queue/drop health, session-only masked credentials, and a single Direct FFmpeg/MediaCodec H.264 encode shared with local safety recording. Host tests and the ARM64 build are required before deployment; 720p30/1080p30 service broadcasts, A/V sync, reconnect, certificate verification, memory, and gameplay cost still require explicit on-headset validation. Android Keystore persistence is intentionally not claimed; keys are currently never written to disk.

Add direct livestreaming from the Quest without requiring a PC.

Do not redesign or destabilize the completed companion Wi-Fi/USB architecture. This phase adds another bounded sink to the existing media fan-out.

## Research/verify first

Verify current requirements for:

- Twitch ingest;
- YouTube ingest;
- custom RTMP/RTMPS endpoints;
- TLS;
- keyframe interval;
- codec/audio/container requirements;
- authentication/stream keys.

Do not assume requirements from memory.

## Implement

Create a `DirectLivestreamSink` consuming encoded packets from the user-selected SaberStage camera or broadcast scene.

Requirements:

- secure RTMPS where appropriate;
- asynchronous network I/O;
- bounded queues;
- no Unity-thread blocking;
- no encoder-drain blocking;
- reconnect with bounded backoff;
- stream health stats;
- keyframe after connect/reconnect;
- explicit start/stop;
- failure isolation;
- credentials never logged.

## Credentials

Implement:

- masked key display;
- change;
- clear;
- secure storage using an appropriate current Android mechanism where practical;
- no plaintext credential dumps in diagnostics.

## State machine

Use:

```text
Offline
Connecting
Live
Reconnecting
Stopping
Failed
```

Recording state and streaming state are separate.

## Simultaneous local recording

If codec settings are compatible, test one encode feeding:

- local recording;
- direct livestream.

Do not start a second hardware encoder automatically.

## Validation

Test direct stream to a controlled endpoint/service at 720p30 and 1080p30.

Measure:

- gameplay frame impact;
- encoder pressure;
- network queue depth;
- reconnect behavior;
- memory;
- A/V sync.

Use the completed compositor and scene controls. Verify that avatar visibility, overlays, and scene changes do not rebuild the encoder or compromise gameplay.
