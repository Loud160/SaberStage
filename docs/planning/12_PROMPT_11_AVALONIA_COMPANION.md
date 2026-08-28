# Prompt 11 — Create the cross-platform SaberStage Companion

Create the desktop companion.

Technology:

- C#
- current supported .NET
- Avalonia

Target:

- Windows
- macOS
- Linux

Do not use a Windows-only Meta/Oculus runtime.

## Implement

- Quest discovery/manual connection;
- pairing;
- protocol negotiation;
- H.264 receive;
- synchronized audio receive;
- preview;
- full-window viewing of the selected third-person camera;
- local desktop recording with safe unique filenames and media validation;
- stream stats;
- start/stop remote stream;
- camera/profile selection;
- broadcast scene selection;
- bitrate/resolution/FPS control where supported;
- reconnect;
- diagnostics.

The companion receives gameplay rendered by Beat Saber running natively on Quest. It must not launch PC Beat Saber, turn the Quest into a PCVR headset, or require a PC capable of running PCVR.

The first companion milestone is complete when the user can connect, view the selected third-person camera with synchronized audio, and optionally record that stream on Windows, macOS, and Linux through the same product architecture.

Keep media decoding behind a platform abstraction instead of embedding OS-specific code throughout the UI.

## Set-it-and-forget-it UX

Remember:

- device;
- preferred transport;
- camera;
- stream settings;
- OBS output settings later;
- local desktop recording settings.

On launch:

```text
find known Quest
→ reconnect
→ restore previous working configuration
→ ready
```

Do not make the user re-enter ports/settings every session.

The companion must not require PC Beat Saber.

Design the UI so later OBS handoff and broadcast controls fit naturally, but do not show nonfunctional controls.
