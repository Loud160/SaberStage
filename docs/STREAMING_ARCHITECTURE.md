# Streaming and companion architecture

The encoded-packet fan-out is the only source for remote outputs. Wi-Fi and USB implement transports under one versioned session protocol; the Avalonia companion consumes the same protocol on Windows, macOS, and Linux without Meta software.

Protocol layers are discovery/pairing, authenticated control, stream negotiation, media packets, clock/health data, and bounded feedback. Unknown versions fail cleanly. Pairing stores a device identity and revocable secret; reconnect uses bounded exponential backoff with jitter and never blocks local recording or gameplay. USB must not be a separate media format.

The companion first proves live view and synchronized desktop recording, then exposes a stable OBS handoff. It may remux encoded H.264/AAC without transcoding when compatible. Network loss drops/reconnects that sink independently.

Direct livestreaming is now an asynchronous bounded sink with states `Offline`, `Connecting`, `Live`, `Reconnecting`, `Stopping`, and `Failed`. It accepts the same Direct FFmpeg MediaCodec H.264 packets used by the local safety recording, encodes game audio as AAC on its network worker, muxes FLV, and writes to provider RTMP/RTMPS ingest without starting a second video encoder. Twitch, YouTube, Kick, and custom endpoints are selectable. The service address and stream key use separate full-width fields with compact subheaders. The key is password-masked by default with an explicit `Show Stream Key` switch, is never logged, and deliberately remains memory-only until an Android Keystore-backed persistence design is implemented; the service endpoint and reconnect policy persist normally.

The video and audio queues are bounded. Overflow drops broadcast media and increments visible health counters instead of delaying the Unity thread, audio thread, encoder worker, local recording, or gameplay. Reconnect uses bounded exponential backoff and waits for the next scheduled H.264 keyframe before reopening output. TLS verification uses the Quest system certificate directory for RTMPS. Start/stop are explicit, and stopping the stream does not stop the local recording; stopping and saving the local session also ends its attached stream.

The current provider presets follow the services' documented ingest forms: Twitch ordinary RTMP, YouTube RTMPS, and Kick RTMPS. Service-side account setup, keys, and creator-dashboard stream configuration remain the user's responsibility. There is no OAuth account management, chat, broadcast-scene UI, or persistent credential storage in this milestone.

Sources: [Twitch broadcast requirements](https://dev.twitch.tv/docs/video-broadcast/), [Twitch ingest](https://dev.twitch.tv/docs/video-broadcast/reference/), [YouTube RTMPS ingestion](https://developers.google.com/youtube/v3/live/guides/rtmps-ingestion), and [YouTube live broadcast flow](https://developers.google.com/youtube/v3/live/guides/implementation/broadcasts-and-streams).
