# Streaming and companion architecture

The encoded-packet fan-out is the only source for remote outputs. Wi-Fi and USB implement transports under one versioned session protocol; the Avalonia companion consumes the same protocol on Windows, macOS, and Linux without Meta software.

Protocol layers are discovery/pairing, authenticated control, stream negotiation, media packets, clock/health data, and bounded feedback. Unknown versions fail cleanly. Pairing stores a device identity and revocable secret; reconnect uses bounded exponential backoff with jitter and never blocks local recording or gameplay. USB must not be a separate media format.

The companion first proves live view and synchronized desktop recording, then exposes a stable OBS handoff. It may remux encoded H.264/AAC without transcoding when compatible. Network loss drops/reconnects that sink independently.

Direct livestreaming is another sink with states `Offline`, `Connecting`, `Live`, `Reconnecting`, `Stopping`, and `Failed`. Initial candidates are RTMPS/RTMP with service-compatible H.264/AAC and keyframe interval. Twitch and YouTube endpoint/auth requirements are queried or configured through supported APIs; stream keys are masked and never logged. Reconnect requests a fresh keyframe and uses bounded queues/backoff.

Sources: [Twitch broadcast requirements](https://dev.twitch.tv/docs/video-broadcast/), [Twitch ingest](https://dev.twitch.tv/docs/video-broadcast/reference/), [YouTube RTMPS ingestion](https://developers.google.com/youtube/v3/live/guides/rtmps-ingestion), and [YouTube live broadcast flow](https://developers.google.com/youtube/v3/live/guides/implementation/broadcasts-and-streams).
