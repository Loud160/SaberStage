# Prompt 10 — Implement Wi-Fi streaming to the desktop companion

Implement Wi-Fi transport using the approved versioned protocol.

Evaluate transport based on:

- latency;
- reliability;
- reconnect;
- packet loss;
- implementation complexity;
- desktop cross-platform support;
- OBS handoff later.

Possible technologies may include RTP/SRT/other standard transports, but choose based on measured/project needs.

Requirements:

- asynchronous send;
- bounded queues;
- reconnect;
- keyframe after join/reconnect;
- connection stats;
- no Unity-thread socket I/O;
- no encoder-drain blocking;
- LAN-appropriate pairing/security;
- sanitized logs.

Test:

- 720p30;
- 1080p30;
- 1080p60 where practical;
- temporary receiver loss;
- local recording + Wi-Fi stream;
- direct Internet stream + Wi-Fi remote stream only if one encoder/output profile safely permits it.

At this stage the primary receiver is the SaberStage Companion. Do not add Quest-native Internet livestreaming yet.

Measure Quest 2 impact.
