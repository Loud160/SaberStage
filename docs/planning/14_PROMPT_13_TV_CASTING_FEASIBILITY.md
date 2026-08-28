# Prompt 13 — Investigate direct third-person viewing on TVs and receiver devices

Investigate whether SaberStage can send the selected camera or composed view directly from Quest to a compatible TV or lightweight receiver without requiring a PCVR session.

This is not the same as Meta's built-in first-person casting. The desired output is SaberStage's independent camera, including the third-person view and, later, the integrated avatar or selected broadcast scene.

## Research before implementation

Verify current practical options such as:

- standards-based receiver protocols supported by common TVs;
- local-network streaming to browser-based or installable receiver applications;
- Chromecast, Google Cast, AirPlay, DLNA, WebRTC, or other appropriate technologies;
- codec, audio, latency, discovery, pairing, and security requirements;
- whether a small receiver application is required;
- cross-vendor TV compatibility;
- Quest 2 performance cost;
- whether the existing encoded H.264/audio stream can be reused.

Do not claim compatibility based only on protocol documentation. Test on representative real receivers when available.

## Architecture requirements

TV output must be another bounded consumer of the existing encoded-media pipeline:

```text
Selected Camera / Broadcast Scene
            ↓
      Hardware Encoder
            ↓
   Encoded Packet Fan-out
            ↓
       TV/Receiver Sink
```

Do not:

- capture the HMD mirror when the user selected the third-person camera;
- render a second expensive gameplay camera solely for TV output;
- block gameplay, recording, or companion streaming on a slow receiver;
- depend on Meta's Windows software;
- describe this as equivalent to Meta casting unless it actually uses the same supported mechanism.

## User experience

The desired flow is:

```text
Choose TV/receiver once
→ reconnect or rediscover automatically when appropriate
→ choose the SaberStage camera/scene
→ start viewing
```

Provide clear recovery for unavailable receivers and network changes. Persist only safe device identity and user preferences.

## Deliverable

Document supported and rejected approaches, prototype the safest practical route if one exists, measure Quest 2 impact, and stop for review before production TV-output work.
