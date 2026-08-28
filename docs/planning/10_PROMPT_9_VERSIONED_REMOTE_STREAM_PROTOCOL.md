# Prompt 9 — Define the versioned remote streaming/control protocol

The first working Quest-only camera/preview/local-recording release is now stable.

Design the protocol for Wi-Fi/USB streaming to the future Avalonia companion.

Do not change camera or encoder ownership.

## Packet fan-out

```text
Encoded A/V
  ├── LocalRecordingSink
  ├── DirectLivestreamSink
  └── RemoteStreamingSink
```

Requirements:

- bounded buffering per sink;
- slow desktop receiver cannot block gameplay/local recording/direct stream;
- codec config can be delivered on join;
- receiver can request a keyframe;
- connect/disconnect does not restart camera unnecessarily;
- same capture timeline;
- stream-only/local-only/both modes.

## Protocol layers

Separate:

- discovery;
- authentication/pairing;
- control;
- media data;
- telemetry;
- camera commands;
- broadcast scene commands;
- chat status where useful.

The protocol must identify the selected SaberStage camera/scene and must never silently substitute the HMD mirror for the requested third-person output.

Version the protocol.

Document compatibility negotiation.

Do not make it Windows-specific.

Do not use Meta MRC.
