# Prompt 21 — Full-system Quest 2 performance and beta hardening

Audit SaberStage as a complete camera, recording, companion-output, and broadcast product.

Do not use this phase to add new headline features. Resolve lifecycle, performance, recovery, usability, and truthful-documentation problems in features already approved.

## Architecture and lifecycle audit

Review:

- application and subsystem ownership;
- scene and preview lifetimes;
- camera tracking/recenter behavior;
- worker shutdown and destruction order;
- locks and callbacks;
- GPU render targets and encoder surfaces;
- audio and shared capture timeline;
- muxing and partial-file recovery;
- per-sink bounded queues and failure isolation;
- Wi-Fi/USB/TV/OBS reconnect behavior;
- stream reconnect and credential storage;
- chat and any supported Discord authentication/reconnect;
- compositor source lifetime and scene switching;
- configuration versioning, migration, reset behavior, and atomic save;
- repeated-session memory growth;
- UI consistency across camera, preview, record, outputs, scenes, broadcast, and chat;
- comments and documentation.

Explicitly inspect for patch-on-patch complexity. When fixes have accumulated around one lifecycle or state machine, simplify its model instead of adding another conditional.

## Measurement matrix

Measure on Quest 2:

A. baseline Beat Saber;
B. spectator render and preview;
C. local recording;
D. recording with representative heavy gameplay and mod content;
E. Wi-Fi companion stream;
F. USB companion stream;
G. TV/receiver output if supported;
H. local recording plus one compatible remote sink;
I. compositor overlays and scene changes;
J. direct livestream;
K. local recording plus direct livestream when one encode can safely feed both;
L. chat panel;
M. any supported Discord integration;
N. hardware-decoded video mod concurrently;
O. heavy Chroma/Noodle/particle map;
P. the heaviest supported combination of recording, output, compositor, and chat.

Capture HMD frame timing, broadcast FPS, dropped frames, encoder pressure, per-sink queue depth, file/network throughput, A/V drift, memory, CPU/GPU indicators, and sustained thermal behavior.

Verify gameplay is favored under overload. Never silently software-encode, start an unapproved second encoder, grow an unbounded queue, or reduce HMD quality.

## Reliability and recovery

Test at minimum:

- 20 sequential local recordings;
- 20 companion connect/disconnect cycles per supported transport;
- 20 direct-stream start/stop cycles against a test endpoint where practical;
- menu → map → menu and repeated map transitions;
- preview and panel restoration;
- recording plus companion output;
- recording plus direct stream when compatible;
- scene switching while outputs remain active;
- gameplay pause independently from recording and stream state;
- chat reconnect;
- network loss and receiver loss;
- service rejection and invalid credentials;
- low storage and write failure;
- encoder initialization failure;
- Quest disconnect/reconnect;
- Beat Saber shutdown during every active output mode;
- Quest recenter;
- full game and companion restart with automatic restoration of the last valid configuration.

## Release truth

Update README, support documentation, limitations, and acceptance evidence to describe only behavior actually verified on the relevant platform and device.

Do not claim Discord video injection, broad TV compatibility, 1080p60, simultaneous outputs, or macOS/Linux behavior without direct evidence.

Finish with changed files, architectural decisions, tests, device results, media validation, performance measurements, known risks, and any intentionally deferred acceptance criteria.
