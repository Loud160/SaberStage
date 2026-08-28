# Prompt 14 — Add OBS output to the SaberStage Companion

Add a robust cross-platform OBS workflow.

Prefer a standard media handoff that works on Windows, macOS, and Linux.

Do not make Spout2 the only output.

Evaluate:

- local SRT;
- RTP/RTSP;
- MPEG-TS;
- another FFmpeg-compatible local endpoint;
- optional platform-native outputs as secondary enhancements.

Goal:

```text
Quest
→ Wi-Fi/USB
→ Avalonia companion
→ stable local media source
→ OBS
```

Beat Saber continues to run on Quest. OBS receives SaberStage's selected third-person camera/scene through the companion; this is not a PCVR workflow.

Provide a setup flow that does not require users to understand codecs/ports.

## Set-it-and-forget-it

Once OBS integration is configured:

- companion restores the endpoint;
- settings persist;
- source remains stable across normal reconnects;
- user should normally launch companion + OBS and simply start playing.

Document exact OBS setup.

Do not make PC OBS a prerequisite for Quest-local recording or direct Quest livestreaming.
