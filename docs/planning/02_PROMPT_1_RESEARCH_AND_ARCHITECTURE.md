# Prompt 1 — Research current Quest tooling and write the architecture plan

Perform a pre-implementation engineering study.

Do **not** implement the mod yet.

## Research current public state

Verify current:

- Quest Beat Saber mod target/version/tooling;
- Scotland2;
- beatsaber-hook;
- custom-types;
- BSML;
- QPM/qpm-rust;
- QMOD schema;
- graphics backend;
- Quest/Android MediaCodec surface-input hardware AVC encoding;
- game-audio capture approaches;
- MP4 muxing;
- RTMP/RTMPS streaming requirements;
- Twitch ingest/chat APIs;
- YouTube ingest/live-chat APIs;
- Camera2 user-facing behavior;
- Camera2 menu structure, terminology, configuration workflow, camera placement, smoothing, preview expectations, and persistence behavior;
- current public Camera2 movement-script file/schema, timing model, supported animated properties, interpolation/easing behavior, assignment workflow, and failure behavior;
- LIV's observable anchored/floating camera behavior as a UX reference only;
- ReeCamera;
- CameraUtils-Quest;
- MRCPlus;
- HollywoodQuest;
- Replay;
- Quest/OpenXR recenter/reference-space behavior;
- Android secure credential storage options;
- local Discord client availability/capabilities on Quest;
- current Discord public SDK/API capabilities relevant to presence, chat, voice/video, streaming, screen share, or external video-source injection;
- whether Android/Horizon OS provides a supported way for one app to present an arbitrary synthetic video source to Discord;
- standards-based or receiver-app approaches for sending SaberStage's selected third-person view to common TVs without Meta's Windows software.

Do not copy source implementation from another camera/recording/avatar mod.

## Discord feasibility must be explicit

Do not assume the third-person camera can be injected into the Quest Discord client.

Determine whether this is possible using supported/public mechanisms.

Explicitly distinguish:

- Discord presence/status integration;
- Discord message/chat integration;
- launching/deep-linking Discord;
- consuming Discord data;
- feeding third-person video into Discord;
- controlling Discord livestreaming.

If arbitrary video-source injection would require:

- modifying Discord;
- private APIs;
- self-bot/user-token automation;
- root;
- unsupported hooking;

mark that approach rejected.

Document supported alternatives.

## Create documents

Create:

```text
docs/ARCHITECTURE.md
docs/PROVENANCE.md
docs/PRODUCT_AND_UX_COMPATIBILITY.md
docs/RECORDING_PIPELINE.md
docs/STREAMING_ARCHITECTURE.md
docs/BROADCAST_COMPOSITOR.md
docs/AVATAR_ARCHITECTURE.md
docs/TV_OUTPUT_FEASIBILITY.md
docs/CHAT_AND_DISCORD_INTEGRATION.md
docs/TRACKING_AND_RECENTER.md
docs/LIFETIME_AND_THREADING.md
docs/TEST_PLAN.md
```

## Architecture must define

- application ownership root;
- camera ownership/lifecycle;
- tracking/recenter layer;
- camera profile model;
- one-camera first-release scope with future multi-camera identity/ownership seams;
- arbitrary practical placement and numeric validation/recovery rules;
- requested camera render/output resolution and encoder/preview negotiation;
- anchored-float input, smoothing, limits, persistence, and recenter behavior;
- clean-room Camera2 movement-script compatibility layer and deterministic song-time evaluation;
- explicit composition order for base placement, scripts, smoothing, and anchored float;
- Camera2-familiar menu and settings model;
- movable HMD-only preview-panel ownership and lifecycle;
- broadcast compositor and sources;
- broadcast scenes;
- frame scheduler;
- capture state machine;
- direct livestream state machine;
- capture timeline;
- hardware encoder;
- audio path;
- encoded packet fan-out;
- local recording sink;
- direct livestream sink;
- companion Wi-Fi/USB sink;
- chat provider abstraction;
- HMD chat panel;
- stream credential storage;
- Discord integration boundary;
- integrated avatar ownership, tracking, calibration, persistence, and broadcast-visibility boundary;
- Avalonia companion boundary;
- companion-side viewing and recording;
- TV/receiver output boundary;
- thread ownership;
- lock rules;
- shutdown order;
- failure containment.

## Third-party dependency review

For every substantial dependency proposed, document:

- reason;
- required/optional;
- license;
- maintenance status;
- Quest compatibility;
- whether it blocks cross-platform future work;
- whether it gives the project access to encoded packets before file/network output.

Pay particular attention to Hollywood: evaluate whether depending on it helps or constrains the encoded-packet/fan-out architecture needed for local recording + direct stream + desktop stream.

## Set-it-and-forget-it design

The architecture must explicitly show how configuration restores correctly on every launch and how tracking/recenter changes avoid breaking camera/avatar orientation.

Explain:

- persisted semantic intent;
- runtime reconstruction;
- config migration;
- reset paths;
- automatic reconnection behavior;
- credential persistence/security;
- chat-panel persistence.

## Product and milestone structure

The architecture must preserve the complete long-term menu and service structure without exposing nonfunctional placeholder controls.

Define three delivery stages:

1. camera, movable preview, synchronized local recording, and first-release Quest 2 hardening;
2. versioned Wi-Fi/USB transport, cross-platform companion viewing/recording, TV-output feasibility, and OBS handoff;
3. integrated avatar, Quest-native compositor/scenes, direct livestreaming, chat, and only supported Discord capabilities.

Behavioral and workflow familiarity with Camera2 is intentional. Clean-room means independently engineered source and internals; it does not require inventing an unfamiliar user experience.

## Finish

Recommend an architecture, include rejected alternatives, list unresolved hardware/API questions, and stop for review before implementation.
