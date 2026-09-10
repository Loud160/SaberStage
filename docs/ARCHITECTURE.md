# SaberStage architecture

Status: camera/editor/preview baseline plus a Direct FFmpeg/MediaCodec recording and multi-destination streaming integration. Device behavior remains gated by the proofs listed in `TEST_PLAN.md`.

## Product boundary and delivery stages

SaberStage is one clean-room Quest-native product: a Camera2-familiar third-person camera with recording and broadcast outputs. Beat Saber remains native on Quest; the optional companion is a receiver, recorder, and OBS handoff, not PCVR.

1. **Stage 1:** one independently rendered camera, movable HMD-only preview, synchronized local recording, and Quest 2 hardening.
2. **Stage 2:** one versioned Wi-Fi/USB protocol, Avalonia viewing/recording, receiver/TV experiments, and OBS handoff.
3. **Stage 3:** compositor/scenes, direct livestreaming, chat, and only Discord features supported by public APIs.

Later sections get stable service and configuration boundaries now, but no nonfunctional controls appear in the UI.

## Recommended ownership graph

```text
ApplicationRoot (created once from late_load; game-thread owned)
|-- SettingsService
|-- TrackingOriginService
|-- CameraManager
|   `-- CameraInstance[id] -> CameraProfile + CameraRig + MotionPipeline
|-- PreviewPanelController
|-- BroadcastCompositor
|-- CaptureController
|   `-- CaptureSession
|       |-- FrameScheduler
|       |-- VideoEncoder
|       |-- AudioCapture + AudioEncoder
|       |-- CaptureTimeline
|       `-- EncodedPacketFanout
|           |-- LocalRecordingSink
|           |-- CompanionSink
|           |-- TvReceiverSink
|           `-- DirectLivestreamSink
|-- ChatService
|-- SupportedDiscordIntegration
|-- Diagnostics
`-- UI
```

`ApplicationRoot` owns construction, start, and reverse-order shutdown. Services receive explicit dependencies; no service reaches into another global singleton. The first release creates one camera with stable ID `primary`. Profiles, preview selection, capture selection, and future protocol messages identify a camera by ID so adding cameras does not require a data-model rewrite.

## Camera model

`CameraProfile` persists semantic user intent:

- stable ID and display name;
- player-origin-relative position and rotation plus reference-frame mode;
- FOV and clipping choices;
- requested output width/height/FPS (a request, not an assumed allocation);
- visibility/culling preferences;
- smoothing and optional anchored-float parameters;
- optional validated movement-script assignment.

The runtime camera is reconstructed after tracking and scene anchors exist. It never owns muxers, sockets, credentials, or encoder threads. HMD cameras are never reparented or modified.

Placement is unrestricted within practical engine limits. Input must be finite. FOV is repaired to 10-170 degrees, dimensions to 320-4096 and even values, FPS to 15-60, near clip to 0.01-10 m, and far clip to a value greater than near and no more than 10,000 m. Position components accept -1,000 to +1,000 m and rotations are normalized. Invalid fields are repaired individually; a `Reset camera to visible default` action reconstructs a known player-relative view.

Requested resolution is negotiated by `CaptureController` against device codec capabilities, graphics-bridge constraints, sink constraints, and performance policy. The profile retains the request; session diagnostics record the actual render and encode sizes. Preview resolution may be lower and never silently changes the recording request.

## Motion composition

Every frame uses one deterministic pipeline:

```text
saved base pose
-> script pose override/interpolation (if active)
-> optional general transform smoothing
-> bounded anchored-float offset in the resulting local horizontal frame
-> tracking-origin transform
-> Unity camera transform
```

Only the final runtime pose changes. Neither scripts nor anchored float mutate the saved base.

Anchored float samples HMD yaw relative to the current player-forward anchor, dead-zones small motion, clamps it, maps it to a lateral target, and applies a critically damped time-based filter. Strength/range and response time are validated and persisted; it is off by default. A recenter rebinds the anchor and clears filter velocity without moving the semantic base.

The clean-room Camera2 compatibility reader accepts the documented public keyframe fields `syncToSong`, `loop`, and `frames`, with per-frame `transition`, `position`, `rotation`, `FOV`, `duration`, and `holdTime`. `Linear` and `Eased` are supported initially. Evaluation uses authoritative song time when synchronized and monotonic session time otherwise. Pause freezes song-synced evaluation naturally; restart/seek evaluates directly from the new time; non-looping scripts hold their final valid frame. A malformed, oversized, nonfinite, or unsupported script is rejected before activation and the base profile remains active. Parsing has byte, frame-count, duration, and numeric limits. Camera Plus conversion is not part of the initial compatibility contract.

## Preview

The preview has its own semantic placement, scale, visibility, camera ID, and reset-to-visible action. Both the docked editor surface and the persistent world-space surface use the same native UI `RawImage` path, the same alpha-independent preview material, and the same spectator texture assignment. Keeping the two consumers on one proven rendering path avoids the Quest-only blank popout failure caused by the earlier standalone `MeshRenderer` quad. Both monitors are excluded from broadcast output: the docked/floor surface through its existing exclusion, and the movable popout through the same `CanvasGroup` alpha path plus a `RawImage` cull snapshot. (The popout was temporarily left camera-visible as a recursion diagnostic; that state put an infinite picture-in-picture feed into recordings and is no longer used.) Each monitor owns its own preview material instance, and both prefer the embedded `SaberStage/VideoPreview` shader from the runtime shader bundle. The shader ignores the camera texture's bloom-weight alpha and carries guaranteed `STEREO_MULTIVIEW_ON` variants. A stock shader located with `Shader.Find` can silently lack its multiview variants in Beat Saber's build and then binds without error while rasterizing nothing in the headset, even though the mono spectator camera still sees it; this is the mechanism behind every earlier "popout blank in HMD while the floor works" attempt, including the original standalone quad. (Lesson imported from the author's Big Screen mod.) The embedded pass remains in the Transparent UI queue with depth writes disabled, and Canvas sibling order draws the live image after the dark panel backdrop instead of letting that backdrop cover it. `FloatingScreen` presents its UI face along local negative Z, so panels spawned in front of the HMD use the viewer's yaw rather than adding 180 degrees. `Unlit/Texture` remains only as a fallback when the bundle asset is missing. Hiding the panel releases its demand; if capture also is idle, no spectator render is scheduled. Grabbing the camera may temporarily enlarge the preview, matching the useful Camera2 workflow without copying implementation.

## Capture architecture

SaberStage gives its Direct FFmpeg/MediaCodec encoder the existing Primary camera's render texture, points preview consumers at that same texture, captures game audio through a persistent HMD-positioned Unity audio listener, and writes partial H.264/WAV files for local recordings. SaberStage's private offset-aware FFmpeg muxer avoids forcing independently started raw streams to the same zero timestamp. Start begins recording immediately by default. The persistent spectator encoder is retargeted when Beat Saber replaces its scene camera so a single recording continues through menus, loading, gameplay, and results until the user stops it. Optional Gameplay Only mode may arm in a menu and stop at the end of gameplay. The final MP4 is promoted only after successful muxing; partial inputs remain after failure.

An optional compact movable HMD-only control panel exposes glyph play/pause and stop actions plus elapsed time and the current `LOCAL`/`LIVE STREAM` output type. Its visibility and pose persist independently. An invisible native handle occupies the panel's outer padding rather than adding a visible grab bar or blocking the buttons, and the complete panel is excluded from spectator output.

The direct encoder/fan-out path supplies timestamped packets to compatible local, companion, and livestream sinks without starting a second video encoder.

The compositor produces a final Vulkan-compatible GPU image. A backend-specific bridge presents scheduled frames to an Android `MediaCodec` AVC encoder configured with an input `Surface`. Normal operation must not use CPU `ReadPixels`. Video and audio share one monotonic capture epoch; Direct FFmpeg preserves scheduled frame PTS (including dropped-frame gaps), measures the first audio/video arrival skew, trims or offsets audio at mux time, and bounds the audio tail to the picture timeline. Drain, mux, file, and network work occur off the game thread through bounded queues. When pressure rises, spectator frames are dropped before gameplay is delayed.

Encoded access units are normalized into immutable packet objects containing track, PTS/DTS where applicable, flags, codec configuration, and payload ownership. Fan-out gives each sink a bounded queue and independent failure state. One encode serves compatible sinks. A second encoder is never started silently and is allowed only after runtime capability and Quest 2 performance proof.

Recording states: `Idle -> Starting -> Recording <-> RecordingPaused -> Stopping -> Idle`, with optional `Armed` used only by Gameplay Only mode and `Failed` reachable from active transitions. Ordinary Beat Saber scene changes do not change recording state. Streaming independently uses `Offline -> Connecting -> Live -> Reconnecting -> Stopping -> Offline`, with `Failed`. Game pause is not recording pause. See `RECORDING_PIPELINE.md` and `STREAMING_ARCHITECTURE.md`.

## Persistence and set-it-and-forget-it behavior

Settings use a versioned document written to the mod's `ModData` directory. Startup is load, parse, migrate, field-validate/repair, safe-save if changed, then runtime reconstruction after tracking is ready. Unknown future fields are preserved where practical. Every subsystem has a reset and a factory reset exists. Fragile Unity object references and absolute transient scene transforms are never serialized.

Continuous slider edits update the in-memory model and live runtime
immediately, but their durable writes are coalesced. The main-thread menu tick
performs the delayed atomic save, retries a temporary failure at a bounded
interval, and shutdown flushes any pending change before subsystem ownership is
released. Explicit one-shot actions still save immediately. This preserves the
set-it-and-forget-it contract without repeatedly flushing and renaming the
settings file while a controller is moving a slider.

Reconnections use saved nonsecret endpoint identity and pairing metadata plus bounded backoff. Stream and OAuth secrets use Android Keystore-backed encryption when the mod environment can access it safely; plaintext fallback is not acceptable for production. A user can clear or replace every secret. Chat panel placement and provider identity persist separately from credentials.

## Diagnostics and error containment

SaberStage statically links a private Native Logger Quest instance. It writes
to logcat immediately and to a bounded asynchronous current/previous file pair;
it neither declares nor intercepts Paper2. The logger starts before ordinary
IL2CPP, dependency, subsystem, and hook setup so partial startup failures still
have a SaberStage-owned diagnostic path.

Native hook, runtime-driver, menu-flow, worker-completion, and teardown
boundaries convert unexpected C++ exceptions into contextual records. Each
record includes the operation, source file, line, function, exception detail,
and recovery result where available. One central error manager accepts reports
from any thread, but a process-lifetime main-thread driver is solely
responsible for presenting Beat Saber's shared dialog. It waits for a stable
front flow, retains Unity objects safely across frames, and reasserts frontmost
sibling order so the prompt cannot become an invisible input blocker.

This cannot recover faults that occur before Android loads
`libsaberstage.so`, native memory corruption, or failures inside another mod.
The build therefore validates SaberStage's ELF and package dependency boundary
in addition to its runtime catches.

## Failure containment and rejected alternatives

- Reject copying or porting existing camera or recording code; public projects are behavior and feasibility references only.
- Reject Meta MRC as the core: it needs external tooling, is not the desired local recorder, and does not provide the required unified packet fan-out.
- Reject an alternate recording-backend dependency: local recording and streaming share SaberStage's one Direct FFmpeg/MediaCodec implementation.
- Reject CPU readback/software H.264 as a production fallback.
- Reject a separate camera or encoder per destination.
- Reject modifying Discord, self-bots, user tokens, private APIs, root, or unsupported hooks.
- Reject direct Unity access from codec/network/chat workers and unbounded queues.

## Unresolved device/API questions

Before Stage 1 media implementation: confirm the exact Quest 2 Vulkan texture-to-encoder-surface bridge, encoder profiles/levels/resolutions, simultaneous codec limits, Unity audio tap point, AudioTrack/AAC path, sustained thermal cost, storage behavior, and long-session A/V drift. Before later stages: validate USB transport under Android application constraints, receiver latency/device coverage, OAuth/deep-link behavior in a patched app, and whether Android Keystore aliases remain stable across Beat Saber updates/repatching. Discord video injection is currently classified unsupported, not unresolved.

## Primary sources

- [Scotland2 lifecycle](https://github.com/sc2ad/scotland2)
- [Current QPM CLI releases](https://github.com/QuestPackageManager/QPM.CLI/releases)
- [Android MediaCodec](https://developer.android.com/reference/android/media/MediaCodec)
- [Android MediaMuxer](https://developer.android.com/reference/android/media/MediaMuxer)
- [Camera2 public repository](https://github.com/kinsi55/CS_BeatSaber_Camera2)
- [Android Keystore](https://developer.android.com/privacy-and-security/keystore)
