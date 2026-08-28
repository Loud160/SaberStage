# Prompt 12 — Add USB streaming

Add USB transport without creating a second media architecture.

First investigate/implement the simplest cross-platform approach using ADB port forwarding/reverse where appropriate.

Above the transport, keep the same protocol as Wi-Fi wherever possible.

USB is an optional lower-latency/reliability path for the same selected third-person camera and audio stream. It must not create a separate camera, encoder, or companion workflow.

The Avalonia companion should:

- detect adb/platform-tools;
- detect connected Quest;
- establish/remove owned tunnel;
- show connection state;
- avoid requiring manual adb commands;
- recover from disconnect/reconnect;
- clean up forwarding rules it owns.

Support:

- Windows
- macOS
- Linux

Do not depend on Meta's Windows desktop framework.

After ADB-tunneled USB is stable, separately evaluate whether a direct custom USB transport gives enough benefit to justify the complexity.

## Set-it-and-forget-it

If USB was previously selected and a known Quest is attached:

- companion should detect it;
- re-establish its tunnel;
- reconnect automatically;
- restore the last working media/control state.
