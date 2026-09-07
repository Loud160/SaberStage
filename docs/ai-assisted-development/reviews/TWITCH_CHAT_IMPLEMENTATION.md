<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors -->
<!-- Part of SaberStage. See LICENSE and LICENSE-ADDITIONAL-TERMS.md. -->

# Standalone Twitch chat implementation record

Authorization: 2026-09-02, implement the agreed
[plan](../../planning/TWITCH_RICH_CHAT_AND_SONG_REQUESTS.md).
Checkpoint `7628a89` preserves the deployed recording repairs and the
full approved plan. No new device operations are authorized during this work.

## Execution boundaries

- Twitch only; no rain, generic integration rule engine, OBS/PC companion,
  YouTube/Kick changes, or unrelated camera/recording/UI changes.
- Preserve the native chat scrollbar, joystick hover wake-up, grabbable surface,
  128-message retention, 50-row pool, and maximum 240 x 200 panel size.
- New controls enter through Chat Control, not the main Live tab. Requests
  have an independent panel. The pinned PC left/main/right view composition,
  settings columns, Queue/History/Allowlist/Blocklist, and selected-map actions
  are the layout/workflow reference. No per-row download/play buttons.
- Separate plain-data protocol/policy/storage/network code from Unity-owned
  panels. No network, archive extraction, or durable file writes in UI callbacks.
- Confirm acceptance only after durable queue storage. Closed intake on restart;
  stable channel/user/request identity; recover interrupted requests honestly.
- Preserve secure token handling; no secrets or full chat transcripts in logs.

## Implementation inventory

| Area | Implementation / source entry points |
| --- | --- |
| Structured messages | `ChatProtocol.hpp/.cpp`: IRCv3 tags, UTF-8 emote boundaries, badges, roles, colors, action/sub/gift/Bits messages, stable identity, deletion/clear events, escaping and bounded retention. |
| Rich assets | `ChatAssets.cpp`, `ChatImageDecoder.cpp`, `RichChatRenderer.cpp`: Twitch badges/emotes plus FFZ/BTTV/7TV global/channel catalogs, deterministic overrides, static default, optional bounded animation, atlas/pinned-row lifetime, fallback text, platform accent and mention colors. |
| Optional notices | `NoticeProtocol.cpp`, `TwitchNotices.cpp`: direct EventSub WebSockets for follows/redemptions, handshake validation, fragmented/control-frame parsing, keepalive, bounded retries, reconnect handover and stable-ID deduplication. IRC remains the sole source for sub/gift/Bits events. |
| Account / moderation | `TwitchService.cpp`: new OAuth scopes, generation-guarded workers, confirmed timeout/ban/message deletion, shared sender rate limit, six bounded IRC retries and explicit chat-only retry. Existing Keystore persistence remains unchanged. |
| Controls / manager | `ChatControls.cpp`: separate flat settings/moderation and request hosts, appearance columns/colors, General/Commands/Cooldown, tooltips, confirmation targets, Queue/History/Allowlist/Blocklist, selected cover/details and contextual actions. Native list pools contain eight rows. |
| Queue / commands | `SongRequests.cpp`, `SongRequestService.cpp`: per-channel two-slot durable snapshots, save-before-ack, closed restart, interrupted-attempt recovery, eight command groups, configurable limits/permissions/cooldowns, stable-ID mutations and close-during-lookup cancellation. |
| Downloads | `ChatNetwork.cpp`, `MapArchive.cpp`, `MapDownload.cpp`: HTTPS host/redirect/size/time limits, explicit cancellable download, ZIP path/CRC/extraction checks, content SHA1 validation, file/directory flushing and atomic publication outside the scanner until ready. |
| Native song integration | SongCore 1.1.26 pinned in QPM; `ChatControls.cpp` refreshes asynchronously and opens native difficulty selection without auto-start. `main.cpp` reports actual song start and completion/failure/quit, independently of panel selection. |
| Rendering bundle | Separate `SaberStageChatSprite.shader` with stereo variants and UI stencil/rect clipping. The current runtime bundle contains the chat sprite, non-bloom UI, and video-preview shaders. |
| Existing chat integration | `MenuController.cpp` binds rich text to the existing 50-row pool, remeasures on style/asset changes, retains reader anchors, preserves native scroll/hover/grab behavior and recreates rows with saved font/background settings. No main Live/camera menu layout changes. |

Policy defaults are implementation choices, not previously user-specified
numbers: requests disabled, intake closed, 25 pending (1–200), two per viewer
(1–20), one extra each for VIP/subscriber, 30-second BSR and 10-second queue-query
cooldowns (0–600), 20-minute duration maximum and 100 retained history entries
(1–500). Lowering capacity preserves existing pending items. Command permissions
default to Everyone except Open/Close (moderator/broadcaster). A two-second
per-user input ceiling and one outbound message per two seconds are separate
from configurable command cooldowns.

## Resource and lifetime review

- No network, decoding, ZIP processing or durable queue commits on Unity's
  thread. UI polls plain snapshots/revisions, with cached request selection
  and data so a 500-item history is not copied/rebound every rendered frame.
- One 1024² RGBA rich atlas (4 MiB, 256 64px slots); unused-entry LRU eviction;
  currently retained messages pin their entries. Provider images are capped
  at 2 MiB and 1024² source dimensions, 32 pending jobs, eight ready images,
  16 sampled frames and 10 FPS. Long/unsupported animation becomes static.
  Uploads admit at most one completed asset each quarter-second. A selected
  map cover uses one separate 64² texture without another emote atlas/catalog.
- Catalogs, jobs, replies, deduplication and user cooldown maps are bounded.
  Source/account generations discard obsolete work. No disk emote cache and
  no unrestricted chat transcript are written. Queue records necessarily
  retain requester identity and map metadata; the user guide identifies this.
- Ordinary queued commands cannot crowd out actual start/finish events.
  Unaccepted viewer jobs are shed first, with eight reserved game-event slots.
  A pathological event backlog closes intake and logs the condition.
- Partial panel creation/close unregisters the capture-exclusion root before
  destroying Unity objects. Callbacks have weak lifetime guards; hierarchy
  rebuilding/destruction is deferred until Tick. Targeted moderation retains
  stable IDs and rejects an account switch before confirmation.
- ZIP staging cannot overwrite installed/user maps, follow intermediate
  symlinks, escape its own directory or publish unverified content. The content
  hash follows this pinned SongCore version's Info/difficulty ordering. A
  download that encounters gameplay is cancelled; refresh/navigation wait for
  a valid menu state. These device integrations still need runtime acceptance.

## Verification record

Local-only verification; no ADB, headset changes, live account actions or real
moderation/EventSub subscriptions were performed in this implementation turn.

- Host CMake/CTest: **13/13 suites passed**. Actual protocol/policy/store/worker
  code is compiled, not just source-string assertions. Added suites cover
  messages, asset catalogs, EventSub envelopes/frames, requests and archives;
  existing settings/camera/recording/chat geometry tests remain green.
- Request fixtures include two-slot corruption fallback, interrupted Playing,
  ignored partial snapshots, account isolation, save-before-ack, a real
  filesystem commit failure, close while a resolver is blocked, duplicate
  delivery, permission distinction, allowlist requeue, actual failed attempt
  and completion while 64 viewer commands occupy the backlog.
- Asset fixtures run the real catalog worker with offline transport/decoder
  adapters: provider/channel precedence, optional FFZ animated URLs, static
  coalescing, unsupported modifier fallback, eight-image handoff and generation
  cancellation. They do not test Android decoding or Unity atlas rendering.
- Asset-worker and request-worker suites additionally passed **20 consecutive
  runs each**, including the stalled-resolver/command-backlog cases.
- Tooling: **56/56 checks passed**. Three existing source assertions were
  updated for the legitimate empty-body DELETE/PATCH path and two-unit accent
  gutter used equally by row layout and TMP measurement. Tooling success does
  not establish visual correctness.
- Public, unauthenticated host probes returned HTTP 200 for FFZ, BTTV and 7TV
  global catalogs and sampled FFZ/BTTV/7TV image URLs. This checks current service
  shapes/reachability, not Quest TLS, OAuth, channel catalogs or decoder output.
  FFZ's separate animation field is documented by its
  [official API](https://api.frankerfacez.com/docs/?urls.primaryName=API+v1).
- Unity Android bundle rebuild succeeded at that checkpoint. The later
  camera-only product cleanup reduced the current runtime bundle to
  `saberstage-chat-sprite`, `saberstage-non-bloom-ui`, and
  `saberstage-video-preview`.
- Final ARM64 integration link including reopen/cleanup changes succeeded;
  `verify-native-library.py` passed the private-logger ELF boundary check.
  Normal QPM packaging rebuilt its dependency-locked target. A final incremental
  native build then included the staged map-root directory flush, followed by
  `qpm qmod zip --skip_build` against that verified binary and
  `tests/VerifyPackage.py`; **all passed on 2026-09-03**. The latter checks exact
  payload/ELF bytes, private runtime files, loader entry points, no direct Paper2
  dependency and the exact SongCore 1.1.26 manifest pin.

Final local artifacts (not deployed):

| Artifact | SHA-256 |
| --- | --- |
| `build/libsaberstage.so` | `afb5e330ac931c1cb8d749e5fad697377af0554652968228711471cda33c9492` |
| `SaberStage.qmod` | `6e2fc16270318a1f6dd16547bea9a1fdbd6d38815e9e2296e8c2e90b664fb46d` |

Private local evidence: `diagnostics/chat-host-tests-final.log`,
`diagnostics/chat-tooling-final.log`, `diagnostics/chat-release-build.log`, and
`diagnostics/chat-final-map-flush-build.log`, `diagnostics/chat-qmod-final.log`,
`tools/runtime-shaders/Build/unity-runtime-shaders.log`. These are local build/test
outputs, not a reason to commit diagnostics or device backups.

## UI reference, adaptations and outstanding acceptance

Read-only source reference:
`C:/Users/Owner/AppData/Local/Temp/SaberStage-BeatSaberPlus-Review-20260902`.
The approved plan preserves immutable upstream commit links if this temporary
clone disappears.

The controls use the PC logical left/main/right composition, appearance
columns, General/Commands/Cooldown, selected-map manager actions and separated
moderation. Actual adaptations are documented, not hidden as verified parity:
one flat Quest host per multi-pane view, uniform request-panel scaling, native
RGB rather than alpha color pickers, small bounded thumbnails, permission
cycling for eight supported command groups, and fixed help/status shortcuts
rather than a PC web-configuration editor. Existing Quest grab/resize controls
remain; PC environment-follow/lock controls were not added. Generic rule/
shortcut editors, emote rain, polls/predictions, gameplay effects and additional
YouTube/Kick support are not part of this implementation.

**Full rendered PC UI parity is not verified.** No actual PC/Quest comparative
captures were obtained. The following require a separately authorized build
installation and headset session, not more source-string tests:

1. Facing/layout/clipping of all views, open/close/reopen, coexistence, blue
   clickable buttons, thumbstick scrolling, grabbing and size/reset persistence.
2. Main chat overflow scrollbar/hover scrolling remains correct; rich sprites
   wrap, animate, clip in both eyes, preserve the reader anchor and do not bloom.
3. Account reauthorization, live notices, reconnect/token refresh, confirmed
   moderation/denied permissions, and no duplicate or stale-channel messages.
4. Request intake/replies, all manager tabs, archive cancellation/failure,
   installed-map lookup, SongCore refresh and native difficulty selection.
5. Authorized force-close during queue writes/downloads and request attempts,
   with recovery inspected after restart and no loss of acknowledged pending
   requests from ordinary interruption. Hardware/filesystem corruption remains
   distinguishable from ordinary interruption.
6. Busy chat/long streams with static versus animated assets: measure Quest 2
   frame time/memory. Bounds alone are not a measured no-overhead guarantee.

The [user/developer guide](../../TWITCH_CHAT_AND_REQUESTS.md) contains the
headset workflow, defaults, storage behavior, permissions and resource limits.
