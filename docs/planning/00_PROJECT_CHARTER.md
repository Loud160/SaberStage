# SaberStage Project Charter — Quest-Native Beat Saber Camera, Recorder, and Broadcast System

## Purpose

Build **SaberStage**, a new clean-room native Meta Quest Beat Saber mod whose core product is a Camera2-familiar independent spectator/broadcast camera with recording and output capabilities.

The player must continue to play Beat Saber normally through the headset's first-person VR cameras while the mod renders a separate camera that can be used for:

1. local on-Quest recording from the selected third-person camera;
2. Wi-Fi/USB streaming to a cross-platform desktop companion for viewing and recording;
3. OBS integration through that companion;
4. direct third-person viewing on compatible TVs/receivers where a practical supported approach exists;
5. lightweight Beat-Saber-specific broadcast composition features similar to the useful parts of OBS;
6. direct livestreaming from Quest to services such as Twitch, YouTube, or a custom RTMP/RTMPS endpoint;
7. in-game stream-chat display;
8. integration with a **local Discord client running on the Quest** where supported, including chat/status integration and, only if technically feasible through supported/current APIs, using the selected third-person broadcast view as a Discord livestream source.

The optional **SaberStage Companion** will be written in **C#/.NET with Avalonia** and must run on:

- Windows
- macOS
- Linux

The project must not require Meta/Oculus Windows desktop software for its core recording or streaming architecture.

Beat Saber continues to run natively on Quest. The companion receives and processes SaberStage output; it does not turn the headset into a PCVR client and must not require a computer capable of running PCVR.

## Product identity and compatibility goal

SaberStage should work and look recognizably like the PC Camera2 experience wherever Quest constraints permit. Existing Camera2 users should not have to relearn camera concepts, placement, smoothing, preview, persistence, or ordinary operation merely because they moved to native Quest.

This is behavioral and workflow compatibility, not source compatibility. Quest-specific differences are acceptable when required by performance, platform APIs, interaction design, or safety, but they must be deliberate, documented, and easy to understand.

SaberStage is for players who cannot or do not want to run Beat Saber through PCVR but still want third-person gameplay presentation comparable to PC creators.

---

# Product philosophy: set it once, then forget it

A primary product requirement is the same quality that makes well-designed PC Camera2 workflows pleasant:

> **Once configured correctly, the system should restore itself on every Beat Saber launch and normally require no setup at all.**

Normal use should look like:

```text
Launch Beat Saber
→ camera is already correct
→ recording settings are already correct
→ broadcast scene is already correct
→ livestream service/profile is already correct
→ chat panel is already positioned/configured
→ press Record or Go Live
```

The technical complexity belongs inside the implementation, not in the user's daily workflow.

Set-it-and-forget-it does **not** mean stripped down. Users should retain meaningful control over every camera, recording, output, and broadcast setting that can be exposed safely. Normal workflows use sensible durable defaults; specialized and low-level controls belong in coherent advanced sections.

Manual reset/recenter/reconfigure actions must exist as recovery tools, but normal operation should not depend on them.

---

# Clean-room requirement

This is **not** a port, fork, translation, or code copy of Camera2, Camera Plus, ReeCamera, MRCPlus, Replay, Hollywood, LIV, or any other existing project.

Other projects may be studied to understand:

- user-visible camera behavior;
- Quest/Unity/Android platform constraints;
- media encoding and timing;
- streaming concepts;
- tracking constraints;
- camera UX expectations.
- familiar user-visible terminology and workflows.

Do not copy their implementation, class structure, algorithms, configuration files, internal naming conventions, source code, or comments. Familiar user-visible behavior and terminology may be reproduced when that improves compatibility and usability.

Maintain:

```text
docs/PROVENANCE.md
```

Record public projects and documentation consulted and what conceptual/platform knowledge was obtained.

The implementation must be independently engineered.

---

# Public attribution

Use:

**Loud160 (AKA Whisp)**

Do not insert a legal/real-world name into project attribution.

The approved product and repository name is **SaberStage**. This original
planning restriction deferred the license decision to the owner; the owner
subsequently selected GPL-3.0-only with the repository's additional GPLv3
section 7 terms.

---

# Current tooling must be verified

Before scaffolding, determine the current supported Quest Beat Saber development environment.

Verify current:

- Beat Saber target version;
- Scotland2;
- beatsaber-hook;
- custom-types;
- BSML/menu stack;
- QPM/qpm-rust;
- QMOD schema;
- Android NDK/toolchain;
- Quest graphics backend(s);
- Android media APIs;
- networking/TLS libraries appropriate to Quest;
- Discord/Quest capabilities relevant to local-client integration.

Do not blindly reuse versions from older mods.

**Quest 2 is the baseline performance target.**

---

# Delivery sequence and first working release

The implementation order is part of the product strategy.

## Stage 1 — First working Quest release

The first release must provide:

- one Camera2-familiar independent camera that can be placed anywhere practical and configured in detail;
- adjustable camera transform, FOV, and requested render/output resolution;
- optional smooth anchored-float motion driven by the player's left/right look direction;
- clean-room compatibility with supported Camera2 movement scripts;
- persistent camera profiles;
- an optional movable/resizable HMD-only preview panel that can be placed anywhere practical in the game;
- GPU-native hardware video encoding;
- synchronized game audio;
- reliable local recording of the selected third-person view;
- safe controls, storage, retrieval, and media validation;
- Quest 2 performance and repeated-session validation.

This stage is not complete merely because a camera renders or the project compiles. It must produce useful playable recordings on a real Quest and restore configuration after restart.

## Stage 2 — Companion and remote viewing

After Stage 1 is stable:

- define the versioned remote media/control protocol;
- add Wi-Fi and USB output;
- build the cross-platform SaberStage Companion for viewing and desktop recording;
- investigate direct selected-camera output to compatible TVs/receivers;
- add a stable cross-platform OBS handoff.

## Stage 3 — Full broadcast production

After the capture and remote-output foundations are stable:

- add Quest-native broadcast scenes and lightweight OBS-like production controls;
- add direct Quest livestreaming;
- add in-HMD stream chat;
- research and implement only supported Discord capabilities;
- perform full-system Quest 2 hardening.

The UI and configuration architecture must anticipate all three stages from the beginning. Unfinished features must not appear as fake controls, but completed later sections must fit naturally rather than feeling bolted on.

---

# Core product architecture

The long-term system should conceptually look like:

```text
                     Beat Saber simulation
                              │
                              ▼
                Selected SaberStage Camera
                              │
                              ▼
                    Broadcast Compositor
                              │
                              ▼
                      GPU RenderTexture
                              │
                              ▼
                   Hardware A/V Encoder
                              │
                    Encoded Media Packets
                              │
        ┌───────────────┬───────────────┬────────────────┐
        │               │               │                │
        ▼               ▼               ▼                ▼
 Local Recording  Companion Stream  TV/Receiver   Direct Livestream
       MP4          Wi-Fi / USB       if supported     RTMP/RTMPS/etc.
                         │                               │
                  Avalonia Companion              Twitch/YouTube
                    │            │
                    ▼            ▼
             Desktop Record     OBS

Additional control/data integrations:
    ├── Twitch/YouTube chat → in-game chat panel
    ├── future Discord chat/status integration
    └── future Discord video integration if technically feasible
```

No output mode should require a PC unless that output mode explicitly targets the desktop companion/OBS. The companion is an optional receiver/offload path, never a requirement for native Quest gameplay or local recording.

---

# One camera/compositor, multiple outputs

Do not create separate camera systems for recording, companion streaming, TV output, direct livestreaming, or Discord video.

Conceptually:

```text
Camera / BroadcastScene
        ↓
GPU frame
        ↓
EncoderSession
        ↓
EncodedPacketFanout
   ├── LocalRecordingSink
   ├── CompanionStreamingSink
   ├── TvReceiverSink (if supported)
   ├── DirectLivestreamSink
   └── DiscordMediaSink (only if supported)
```

When output requirements are codec-compatible, one hardware encode should feed multiple sinks.

Important:

- Local recording and livestreaming may eventually want different bitrate/resolution/GOP/container requirements.
- Do not assume one encoder can satisfy every combination.
- Prefer one encoder when compatible.
- If simultaneous outputs need different settings, first constrain them to a common profile.
- Only consider a second hardware encoder after runtime capability checks and Quest 2 testing show it is safe.
- Never silently start a second encoder.

---

# Major subsystem boundaries

Exact names may change, but responsibilities must remain clear.

```text
ApplicationRoot
 ├── SettingsService
 ├── TrackingOriginService
 ├── CameraManager
 │    ├── CameraProfile
 │    ├── CameraRig / Motion
 │    └── BroadcastRenderController
 ├── PreviewPanelController
 ├── BroadcastCompositor
 │    ├── BroadcastScene
 │    └── BroadcastSource(s)
 ├── CaptureController
 │    └── CaptureSession
 │          ├── FrameScheduler
 │          ├── VideoEncoder
 │          ├── AudioCapture/Encoder
 │          ├── CaptureTimeline
 │          └── EncodedPacketFanout
 │                ├── LocalRecordingSink
 │                ├── CompanionStreamingSink
 │                ├── TvReceiverSink
 │                ├── DirectLivestreamSink
 │                └── SupportedDiscordMediaSink
 ├── ChatService
 │    ├── TwitchChatProvider
 │    ├── YouTubeChatProvider
 │    └── FutureDiscordChatProvider
 ├── FutureDiscordIntegration
 ├── Diagnostics
 └── UI
```

Do not collapse these into a giant all-purpose manager.

---

# Camera system

The camera system owns:

- spectator-camera lifecycle;
- one user camera/profile in the first working release;
- stable camera identity and profile ownership that can expand to additional cameras later without redesign;
- static/follow/smoothed camera behavior;
- player-relative transforms;
- FOV;
- requested render/output resolution;
- optional anchored-float motion;
- Camera2-compatible movement-script evaluation;
- scene integration;
- render output consumed by preview/capture;
- spectator visibility/culling;
- saved camera/profile switching.

It must not own:

- MP4 writing;
- sockets;
- stream credentials;
- Discord state;
- MediaCodec drain threads.

The HMD cameras must remain independent.

User-visible camera behavior and terminology should remain meaningfully familiar to Camera2 users. Quest constraints may change implementation and expose documented limitations, but they are not a reason to invent an unnecessarily different workflow.

The first working release needs only one user camera. Do not spend Quest resources rendering unused cameras, but do not hard-code global singleton assumptions into profiles, persistence, preview selection, capture consumers, or future protocol identity. Additional cameras may be added later.

Camera placement should be unrestricted within practical engine/platform limits. Safe defaults, reset-to-visible behavior, and validated numeric ranges must protect recovery without imposing arbitrary creative placement limits.

---

# Camera motion and Camera2 script compatibility

SaberStage must support three composable sources of camera state:

1. the saved base placement/profile;
2. an optional smooth anchored-float layer;
3. an optional Camera2-compatible movement script.

## Anchored float

When enabled, anchored float keeps the user's saved camera placement as its base and applies smooth bounded side-to-side motion as the player looks left or right. The result should feel similar to the useful presentation behavior associated with LIV, while being independently engineered for SaberStage.

Requirements:

- disabled by default unless later usability testing justifies another default;
- explicit toggle;
- smooth, damped motion rather than direct head-jitter following;
- configurable strength/range and smoothing where useful;
- no mutation of the saved base placement while the effect runs;
- deliberate behavior across Quest recenter, pause, map transition, and restart;
- no effect on HMD camera transforms.

## Camera2 movement scripts

Research and document the current public Camera2 movement-script format and observable behavior. Implement a clean-room compatibility layer so a supported Camera2 script can be brought to SaberStage without the user manually rewriting it.

Scripts should be able to control supported time-varying camera properties such as:

- position;
- rotation;
- FOV;
- other compatible documented camera properties that make sense on Quest.

Use authoritative song time and define deterministic pause, restart, practice/seek, map transition, and script-end behavior. Validate scripts before activation, bound all values and work, report unsupported properties clearly, and fall back safely to the user's base profile if evaluation fails. Do not allow a malformed script to crash Beat Saber or move the HMD cameras.

Compatibility means file/schema and observable-behavior interoperability, not copied Camera2 parsing or runtime code. Record format research in `docs/PROVENANCE.md` and maintain independently created compatibility fixtures/tests.

The architecture must define a deterministic composition/precedence rule for base placement, movement scripts, smoothing, and anchored float. Enabling two motion features must never cause them to fight over the same transform or produce order-dependent results.

---

# Movable preview panel

The optional preview panel is an HMD-only consumer of the selected SaberStage camera. It owns its own placement, rotation, scale, visibility preference, selected camera, persistence, and reset-to-visible behavior.

The preview must not appear in output by default, alter HMD camera transforms, create an encoder, or keep expensive rendering active when hidden and no other consumer requires it.

---

# Broadcast compositor

The broadcast compositor owns the final presentation **before encoding**.

The first implementation may be only the game camera.

The architecture must later support low-overhead Beat-Saber-specific sources such as:

- game camera;
- song title;
- mapper;
- difficulty;
- score;
- combo;
- energy;
- static text;
- image/logo;
- stream status;
- chat overlay if desired for viewers;
- simple alert widgets later.

Do **not** attempt to port OBS Studio itself to Quest.

Do not build a generic browser-source/plugin engine unless there is later evidence that it is needed.

The goal is an **OBS-like broadcast scene system specialized for Beat Saber**, not a general-purpose production suite.

---

# Broadcast scenes

Allow the architecture to grow into saved scenes such as:

- Gameplay
- Starting Soon
- BRB
- Results
- Minimal
- Cinematic

Scene switching must not require rebuilding the encoder.

Multiple camera presets may exist, but avoid continuously rendering multiple expensive full scene cameras unless measurements justify it.

---

# Set-it-and-forget-it configuration rules

Persist **user intent**, not fragile runtime coordinates.

Bad:

```text
cameraWorldYaw = 173.2°
cameraWorldPosition = arbitrary scene coordinates
```

Better:

```text
camera = 2.8m behind current player forward
camera height = 1.6m
look at player root
```

On each launch:

```text
load config
→ validate/migrate
→ establish current tracking/reference space
→ recreate camera from semantic profile
→ restore requested resolution, anchored-float settings, and selected movement script
→ restore stream/chat/broadcast settings
→ ready
```

---

# Tracking, forward direction, and recenter behavior

Quest recenter/reference-space changes must be handled deliberately.

Separate concepts:

- tracking space;
- current player forward;
- scene/world space;
- camera-relative offsets;
- player tracking anchors.

Investigate current Quest/OpenXR/Unity behavior for recenter/reference-space changes.

Desired behavior:

> If the player uses the normal Quest recenter/reset-view action, the player-relative camera should remain logically aligned without requiring a separate recalibration ceremony.

Provide manual recovery actions such as:

- Recenter Camera to Current Forward

but normal operation should not need them.

---

# Reset/recovery requirements

Every persistent subsystem needs a clear reset path:

- Reset Current Camera Profile
- Reset Preview Panel
- Reset Recording Settings
- Reset Companion/Receiver Settings
- Reset Stream Settings
- Reset Broadcast Scene
- Reset Chat Panel
- Clear/Change Stream Credentials
- Reset Discord Integration
- Factory Reset Mod

No initial setup decision may become an irreversible hidden state.

---

# Configuration migration

Version persistent configuration.

At startup:

1. load;
2. validate;
3. migrate;
4. repair only invalid fields when safe;
5. preserve unrelated valid settings;
6. log migration/repair;
7. continue.

Do not make users manually delete configuration files after normal upgrades.

---

# Recording state machine

Use an explicit state model equivalent to:

```text
Idle
Starting
Recording
RecordingPaused
Stopping
Failed
```

Beat Saber pause and recording pause are independent.

If the game pauses while recording remains active, capture continues.

Recording is broadcast-like by default: pressing Start records immediately and continues through menus, loading, gameplay, results, and later in-game navigation until explicitly stopped. An optional persisted `Gameplay Only` mode may arm in menus and stop after gameplay, but it defaults off. Ordinary scene changes must not split the media session; future livestreaming follows the same continuous lifecycle.

If recording itself is paused, omit the paused interval from the final media timeline.

Audio and video use the same logical capture timeline.

---

# Direct livestream state machine

Use a separate explicit model such as:

```text
Offline
Connecting
Live
Reconnecting
Stopping
Failed
```

Streaming requirements:

- start/stop from menus;
- safe stop from pause menu;
- optional in-map shortcut;
- reconnect with bounded backoff;
- network loss must not freeze gameplay;
- request/produce keyframe after reconnect;
- no credentials in ordinary logs;
- status visible but unobtrusive;
- configured service/profile persists across launches.

Do not auto-go-live at game launch unless the user explicitly enables such a feature later.

A future `BRB` behavior should be a broadcast scene/state transition, not the same thing as pausing recording.

---

# Direct livestream services

The Quest should eventually stream directly without a PC.

Research current ingest requirements for:

- Twitch;
- YouTube;
- custom RTMP/RTMPS endpoints.

Do not hard-code assumptions that may change.

Likely initial transport is RTMP/RTMPS with an appropriate mux/container, but verify current service requirements.

Requirements:

- secure TLS where supported/required;
- validated endpoint;
- masked stream key;
- secrets never logged;
- explicit clear/change credentials;
- appropriate secure storage where practical;
- connection test;
- reconnect;
- stream health stats;
- service-compatible keyframe interval.

---

# In-game stream chat panel

The mod must eventually support showing live stream chat **inside Beat Saber** while the player is playing.

The chat panel is primarily an HMD/player tool and should **not appear in the broadcast output by default**.

Initial providers:

- Twitch chat;
- YouTube live chat.

Design a provider abstraction so later providers can be added without changing the UI model.

Conceptually:

```text
ChatProvider
  ├── Connect/Auth
  ├── ReceiveMessage
  ├── ConnectionState
  └── SendMessage (optional later)
       ↓
ChatService
       ↓
bounded message model
       ↓
Quest HMD Chat Panel
```

Requirements:

- configurable panel position/rotation/scale;
- player-relative or world-relative placement;
- opacity;
- font size;
- maximum visible messages;
- automatic reconnect;
- bounded history;
- rate limiting;
- emote strategy may be added later;
- no uncontrolled allocations in hot paths;
- no chat/network callbacks directly manipulating Unity objects from background threads;
- messages marshalled to the game thread for UI update;
- hide/show shortcut;
- optional auto-hide outside gameplay;
- persistent placement/settings;
- Reset Chat Panel;
- chat should reconnect automatically on later launches when appropriate, without forcing the user through setup again.

Authentication tokens must be treated as secrets and never written to ordinary logs.

---

# Discord integration — research before implementation

A later phase should integrate useful features with a **local Discord client running on the Quest**, if the current Quest/Horizon OS and Discord APIs make this possible safely.

Possible goals include:

- Discord status/presence;
- selected Discord chat/messages surfaced in the in-game panel where permitted;
- stream/session status;
- eventually using the third-person broadcast camera as the video source for a Discord livestream.

However, **do not assume that a normal Android/Quest Discord client exposes an API for arbitrary external video-source injection.**

Before implementation Codex must research current:

- Discord Android/Quest client capabilities;
- Discord public SDK/API capabilities;
- Quest/Horizon OS media-sharing APIs;
- Android MediaProjection/virtual-display/camera-source capabilities;
- whether a third-party app can legally/technically present a synthetic video source to Discord without root, unsupported hooks, or invasive client modification;
- Discord terms/policies relevant to automated client control or modified-client behavior.

If supported public APIs cannot feed the broadcast camera into Discord:

- do not hook/patch/modify the Discord APK;
- do not use self-bot/private-user-token techniques;
- do not invent an unsafe unsupported solution;
- document the limitation;
- evaluate whether a standards-based local stream or future companion can provide an acceptable alternative.

The core camera/encoder architecture must not depend on Discord-specific behavior.

---

# Future Discord video source goal

If a supported mechanism exists, the desired media path is:

```text
Broadcast Camera / Compositor
            ↓
      Hardware Encoder
            ↓
     Discord-Compatible
       Media Bridge
            ↓
 Local Discord Client on Quest
            ↓
        Discord Live
```

Prefer reusing the same broadcast camera and capture timeline.

Do not render another third-person camera solely for Discord if the existing broadcast frame can be reused.

If Discord requires raw frames or a different codec/path:

- quantify the cost;
- protect gameplay;
- avoid CPU readback if possible;
- do not silently start an expensive second media pipeline.

---

# Hardware encoding

Hardware video encoding is mandatory.

Initial preferred codec:

**H.264 / AVC**

Initial presets:

- 720p30;
- 1080p30;
- 1080p60 when measured safe.

Quest 2 default should be conservative until measured.

Never silently fall back to software H.264 during gameplay.

---

# GPU-native capture path

Production path:

```text
Unity spectator/broadcast RenderTexture
→ native GPU texture / graphics bridge
→ MediaCodec input Surface
→ hardware H.264
```

Avoid normal-path:

```text
GPU
→ ReadPixels/raw RGBA
→ CPU conversion
→ encoder
```

Verify the actual graphics backend and isolate backend-specific bridging.

---

# Frame scheduling

Broadcast FPS is independent from HMD refresh rate.

If recording/streaming at 30 FPS, do not render the spectator camera every 72/90 Hz gameplay frame.

Requirements:

- monotonic time;
- bounded queues;
- deterministic PTS;
- stable A/V sync;
- drop broadcast frames before delaying gameplay;
- never catch up by rendering multiple expensive spectator frames in one game frame.

---

# Player view vs broadcast view

The HMD and broadcast output should support different priorities.

```text
HMD:
performance + latency first

Broadcast:
presentation first
```

Eventually the user may choose to reduce visual work in the HMD while keeping richer presentation in the broadcast:

- particles;
- mirrors;
- bloom;
- environment detail;
- video surfaces/effects;
- broadcast overlays.

Do not silently alter other mods' settings.

Remember that camera culling removes render cost but not necessarily simulation/update cost.

---

# Audio

Capture synchronized game audio.

Requirements:

- game audio;
- no microphone initially;
- common video-friendly audio format;
- shared timeline with video;
- long-session A/V stability;
- correct pause/resume timing;
- safe flush/finalization.

---

# Local recording

Use the current standard Quest Beat Saber ModData/storage conventions.

Recordings must:

- use unique sanitized filenames;
- never silently overwrite;
- use active partial/incomplete state;
- finalize safely;
- handle low storage/write errors;
- never delete unrelated files;
- be retrievable over USB/ADB;
- validate with ffprobe/ffmpeg.
- begin immediately when Start is pressed and continue across ordinary scene changes by default;
- offer an optional Gameplay Only mode that defaults off.

The first working SaberStage release must prove that the selected independent camera—not the HMD mirror—is present in the saved file with synchronized game audio.

---

# Companion and remote output

The SaberStage Companion is an optional receiver and processing offload for users who want a larger preview, desktop recording, or OBS without running Beat Saber as PCVR.

Requirements:

- Windows, macOS, and Linux from one cross-platform architecture;
- no Meta/Oculus Windows runtime;
- selected third-person camera or broadcast scene with synchronized audio;
- viewing and desktop recording before OBS integration is considered complete;
- Wi-Fi and USB as transports for the same versioned protocol;
- persistent pairing and reconnection where safe;
- transport failure isolated from gameplay and local recording.

---

# TV and lightweight receiver output

Investigate direct local-network viewing of SaberStage's selected camera/scene on compatible TVs or lightweight receiver devices.

This is not ordinary Quest HMD-mirror casting. Prefer reusing the existing encoded media stream through standards-based or small receiver-app approaches. Do not promise broad compatibility until representative devices have been tested, and do not add a second full render or block gameplay for a slow receiver.

---

# Threading

Required principles:

- Unity/IL2CPP access on the correct game thread unless explicitly safe elsewhere;
- encoder draining off the Unity thread;
- file/network/chat I/O off the Unity thread;
- bounded queues;
- no detached workers that outlive referenced state;
- join workers before resource destruction;
- no join while holding locks the worker needs;
- avoid callbacks while holding subsystem locks.

Every long-lived worker must have documented:

- owner;
- start condition;
- data touched;
- locks;
- stop signal;
- join behavior;
- destruction order.

---

# Error philosophy

Use a coherent policy.

```text
low-level recoverable failure
→ typed/result status with context

feature-level failure
→ stop/degrade the feature safely

user-action failure
→ concise in-game notice + diagnostics

persistent serious failure
→ centralized error record

native invalid state
→ prevent before unsafe call
```

A recorder/stream/chat/Discord integration failure must not crash Beat Saber if it can reasonably be isolated.

---

# Diagnostics

Log practical session-level information:

- mod version;
- game version;
- Quest model;
- graphics backend;
- selected codecs;
- recording/stream settings;
- camera profile;
- camera requested/output resolution;
- anchored-float state and approved parameters;
- movement-script identity, validation result, and lifecycle events without dumping untrusted script contents by default;
- broadcast scene;
- start/pause/resume/stop;
- stream connect/reconnect/disconnect;
- chat provider state;
- frame counts;
- dropped frames;
- encoder pressure/errors;
- audio errors;
- mux errors;
- A/V drift;
- output path;
- final duration.

Never log:

- stream keys;
- OAuth tokens;
- Discord tokens;
- private credentials.

Do not emit huge per-frame logs by default.

---

# Testing philosophy

Do not call a feature complete because it compiles.

Use:

1. host-side tests for pure logic;
2. Quest device tests for platform/Unity behavior;
3. media validation on desktop;
4. realistic mod-stack tests;
5. repeated-session tests.

Stress workloads should eventually include:

- Chroma;
- Noodle Extensions;
- particle-heavy maps;
- hardware video decode from another mod;
- Replay installed;
- direct livestream;
- chat panel;
- Wi-Fi/USB output.

---

# Reference projects — research only

PC camera behavior:

- Camera2 — `https://github.com/kinsi55/CS_BeatSaber_Camera2`
- ReeCamera — `https://github.com/Reezonate/ReeCamera`

Quest camera/rendering:

- CameraUtils-Quest — `https://github.com/Reezonate/CameraUtils-Quest`
- MRCPlus — `https://github.com/Raemien/MRCPlus`

Quest recording:

- HollywoodQuest — `https://github.com/Fernthedev/HollywoodQuest`
- Replay — `https://github.com/Metalit/Replay`

Quest tooling:

- Quest mod template — `https://github.com/Lauriethefish/quest-mod-template`
- Scotland2 — `https://github.com/sc2ad/scotland2`
- Quest porting guide — `https://github.com/Fernthedev/beatsaber-quest-porting-guide`

Also use current official documentation for:

- Android MediaCodec;
- MediaCodecInfo;
- MediaFormat;
- MediaMuxer;
- EGL/input Surface;
- TLS/networking;
- Twitch/YouTube ingest/chat APIs;
- Discord public SDK/API documentation;
- Quest/Horizon OS/OpenXR tracking/recenter behavior.

---

# Engineering rules

1. Do not copy Camera2 or another mod.
2. Do not stack patches indefinitely; simplify broken invariants/state machines.
3. Prefer explicit ownership.
4. Prefer RAII for native resources.
5. Document non-obvious threading/lifetime rules.
6. Never touch Unity objects from arbitrary workers.
7. Never let storage/network/chat I/O stall gameplay.
8. Never use unbounded queues.
9. Never silently software-encode video.
10. Avoid GPU→CPU pixel readback in the production path.
11. Validate Quest behavior on real hardware.
12. Quest 2 is a first-class performance target.
13. Recording and streaming consume the same encoded-media architecture.
14. Desktop companion must be cross-platform by design.
15. Direct Quest livestreaming must not require the desktop companion.
16. Chat integration must not leak credentials or block gameplay.
17. Discord integration must use supported/public mechanisms; do not patch the Discord client.
18. Keep style/comments consistent.
19. After every phase, summarize changed files, decisions, tests, device results, risks, and the exact next step.
20. Implement one user camera first, but preserve stable identity and ownership seams for additional cameras later.
21. Keep saved base placement separate from temporary and scripted motion layers.
22. Camera2 script compatibility must be clean-room, deterministic, bounded, and safe for untrusted input.
23. Scripted motion, smoothing, and anchored float must have one documented composition order.

---

# Final architectural framing

Do not think of this as:

> "a Quest port of Camera2."

Think of it as:

> **SaberStage: a Camera2-familiar native Quest camera whose first useful release records third-person gameplay locally, whose next stage streams that view to cross-platform companions and compatible receivers, and whose final stage provides Quest-native broadcast scenes, direct livestreaming, chat, and supported Discord integration.**

Camera2 is a deliberate behavioral and UX reference. SaberStage must be independently implemented around Quest constraints while preserving the familiar, full-control, set-it-and-forget-it experience from the beginning.
