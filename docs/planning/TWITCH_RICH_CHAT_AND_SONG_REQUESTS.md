<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors -->
<!-- Part of SaberStage. See LICENSE and LICENSE-ADDITIONAL-TERMS.md. -->

# Twitch rich chat and song requests — discussion and implementation plan

Date: 2026-09-02.

**Status: planning only; implementation is NOT authorized yet.** The user
requested this document and a commit, followed by discussion. Do not interpret
the implementation steps below as permission to start changing code, build a
new feature, deploy, connect an account, or send chat messages.

## 1. Agreed direction and boundaries

The user reviewed BeatSaberPlus as a reference for a richer standalone Quest
chat experience. The initial priority is readable rich Twitch chat and a song
request queue that the streamer can manage and play from inside the headset.

Confirmed requirements:

- Everything added in this pass must operate on the Quest itself. Normal
  connections to Twitch, emote providers, and BeatSaver are expected; a PC
  application, OBS, companion, or separately operated relay server is not.
- Use the existing **Chat Control** button on the current chat panel to open a
  separate popout menu for chat controls and settings.
- Do not put the new rich-chat/request settings in the main mod settings menu.
- Preserve the current chat panel's overall layout and interaction model;
  adapt its message rendering rather than replace the whole panel.
- Add rich message presentation resembling the useful parts of BS+ Chat.
- Add viewer song requests and a queue the streamer can inspect/manage/play.
- **Defer emote rain.** Performance and whether rain belongs in the headset,
  third-person camera, or both have not been decided.
- Leave YouTube and Kick exactly as they are. No new provider implementation,
  connection flow, destination-setting changes, or claims of new support.
- Do not change avatars, IK, camera settings, encoding, stream audio, or other
  unrelated controls as part of this work.

The specific UI organization, defaults, and phase boundaries below are
**proposals for discussion**, not additional requirements already approved by
the user. The narrower scope here takes precedence over the broader historical
[chat planning prompt](19_PROMPT_18_STREAM_CHAT_PANEL.md) for this effort only.

## 2. Existing SaberStage baseline

Repository: `C:/Users/Owner/source/repos/SaberStage`.
Branch at review: `logger-hardening-and-repo-audit`.
HEAD before this checkpoint: `83a682e` — native scrollbar repair checkpoint.

Source observations, not claims of new runtime verification:

| Area | What exists now | What this plan adds |
| --- | --- | --- |
| Authentication | Twitch device authorization, token refresh, Android Keystore storage | Reuse; request additional scopes only for approved new actions/events |
| Chat connection | Twitch IRC-over-TLS reader with PING/PONG and error reporting | Preserve structured metadata, recover connections with bounded retry |
| Message model | `sequence`, `author`, `text`; retained history capped at 128 | Stable message/user IDs, roles, badges, colors, emote spans and typed events |
| Display | Left-aligned/wrapped virtualized rows in a movable/resizable panel | Inline badge/emote rendering, styles, filtering without breaking scrolling |
| Panel controls | Resize/Lock Size, viewer count, Chat Control placeholder | Wire Chat Control to a real world-space menu |
| Chat Control | Currently an empty callback and explicitly non-interactable | Open/focus the popout; no dummy action or duplicate panel instances |
| Requests | No incoming command dispatcher, request queue, or map downloader | A bounded request service and headset queue UI |
| Outgoing messages | Existing map announcements and Twitch API sending path | Reusable, rate-limited command replies and request acknowledgements |

The pending work included in this checkpoint already extends maximum panel
dimensions to 240 x 200 canvas units, keeps default/reset dimensions 70 x 58,
and sizes the recycled text-row pool to 50. History remains 128 messages.
Do not inadvertently revert these changes when adding rich content.

The user confirmed the scrollbar appeared when the old panel filled. Follow-up
joystick wake-up, larger panel limits, icon retention, and AFK crash repairs
were subsequently implemented and deployed; their remaining on-headset
acceptance checks are recorded separately. This plan must not describe those
checks as completed or the proposed rich-chat features as already implemented.

### Current code entry points

- [TwitchService.hpp](../../include/saberstage/broadcast/TwitchService.hpp):
  message/snapshot model, account and worker ownership.
- [TwitchService.cpp](../../src/broadcast/TwitchService.cpp): `ParsePrivmsg`,
  chat connection, retry latch, OAuth, outgoing announcements, viewer count.
- [MenuController.cpp](../../src/ui/MenuController.cpp):
  `EnsureChatWorldPanel`, `UpdateChatWorldPanelLayout`,
  `RefreshChatWorldPanelScrollControls`, `TickChatWorldPanel`,
  `TickChatWorldPanelResize`, and `DestroyChatWorldPanel`; locate the
  `Chat Control` button by its label rather than assuming line numbers persist.
- [MenuController.hpp](../../include/saberstage/ui/MenuController.hpp):
  current world-panel handles and state.
- [ChatPanelScrollGeometry.hpp](../../include/saberstage/ui/ChatPanelScrollGeometry.hpp):
  logical content/page geometry and bounded row-pool sizing.
- [ChatPanelDiagnostics.cpp](../../src/ui/ChatPanelDiagnostics.cpp):
  existing scrollbar, visibility, content-height, and input diagnostics.
- [SettingsModel.hpp](../../include/saberstage/settings/SettingsModel.hpp),
  [SettingsModel.cpp](../../src/settings/SettingsModel.cpp), and
  [SettingsService.cpp](../../src/settings/SettingsService.cpp):
  defaults, validation, serialization, and migration points.
- [ErrorManager.cpp](../../src/ErrorManager.cpp): existing safe error queue;
  inspect its actual UI lifecycle before using it from a new popout.
- [qpm.json](../../qpm.json): no direct SongCore dependency is currently declared.

## 3. What the segmented strip in BS+ is for

The code contains a narrow **per-message platform-origin accent**, matching the
description of several small vertical sections down the left of the chat.
It is an indicator, not a scrollbar, button column, or unused settings area.

- `ChatMessageWidget.Awake` creates `m_Accent`, anchors it to the left edge,
  and disables its raycast target.
- `ChatMessageWidget.OnTextChanged` makes the accent match that message's
  height. Separate message accents therefore look like a segmented strip.
- `ChatFloatingPanelView.UpdateMessageStyle` enables it from
  `PlatformOriginColor` and takes its color from `message.Service.AccentColor`.
- The SDK Twitch provider supplies purple (`#9147FF`, alpha 0.75).

References: [message widget][bs-widget], [message styling][bs-chat-view],
[Twitch provider][sdk-twitch]. No screenshot of the exact strip being discussed
was supplied, so this is a code-backed match to the description, not visual
confirmation of the user's particular example.

BS+ also has separate prediction, poll, and viewer-status panels. These are
different from the per-message accent; do not confuse them with the scrollbar.

**Proposal:** omit the platform strip from the initial Twitch-only UI. Every
message would identify the same service, so it would consume width without
adding much information. Retain a provider identifier in the data model so a
future decision does not require reworking message identity.

## 4. Chat Control popout menu

### Entry point and lifetime

- Clicking the existing Chat Control button opens or focuses one world-space
  menu. Repeated clicks must not allocate duplicate menus or event subscriptions.
- Keep the original chat visible while the controls are open. Closing controls
  does not disconnect chat, stop streaming, clear requests, or reset placement.
- Give the menu a clear Close button and a grabbable background. Preserve the
  existing priority of button interaction, scrolling, resizing, and panel drag.
- Use the same flat-panel visual language: opaque black background, non-bloom
  blue border, readable text, blue enabled action buttons, honest disabled states.
- Spawn it facing the player and within reach. Proposed behavior is to remember
  its pose, but not reopen it automatically after restarting the game.
- Keep existing main-menu Show Chat/reset entry points available for recovery
  unless the user separately requests moving them. No new rich-chat or queue
  configuration should be inserted into the Live tab.

### Proposed contents

Use a small tab/section selector inside this popout, not new main-menu tabs:

| Section | Initial contents |
| --- | --- |
| Chat | Connection/status and Reconnect; appearance/emote/filter controls; reset appearance; clear displayed history with confirmation |
| Requests | Enable requests; Open/Closed state; queue; selected-map details; download/select, remove, move up/down; recent history |
| Request Settings | Allowed viewer roles, per-user/total limits, cooldown, duplicate policy, supported map filters, reply behavior |

These are proposals for grouping. Only expose functional controls; do not
create disabled placeholder pages for rain, other services, or a future rules
editor. Existing stream title and account configuration are not being moved.

### Layout and input contracts

- One authoritative measured content width per page. Text, controls, clipping,
  and row sizing must agree on that width after parent layout resolves.
- Labels left-align inside the visible area; buttons/sliders stay within its
  right edge. Long labels/tooltips/confirmations wrap. Do not use several nested
  auto-sizing groups with contradictory widths or manual offsets to mask them.
- Use rows for related actions where room permits. Test at the smallest
  supported menu size as well as the largest chat size.
- Retain the native scrollbar, page buttons, and HMUI joystick behavior. Pointer
  entry must wake the native scroll component without requiring an arrow click.
- Decorative images/emotes do not intercept pointer input. Opening controls must
  not cause click-through into the map, underlying menu, or the chat drag handle.
- The new menu must be safe in gameplay and while a map is paused. Never parent
  a gameplay-opened modal to an inactive settings-menu view. The prior stream
  pause crash demonstrates that an outer try/catch does not make that safe.

## 5. Rich Twitch message rendering

### Initial target

- Username colors, readable fallback colors, and broadcaster/moderator/VIP/
  subscriber badges when the provider supplies them.
- Inline Twitch emotes and channel/global 7TV, BTTV, and FFZ emotes.
- Text and emotes wrap as one message inside the actual viewport; short
  messages remain left-aligned and tall messages remain fully scrollable.
- A clear mention/highlight style and ordinary action-message handling.
- Optional command hiding without preventing the request service from seeing
  commands. Display filtering must happen after command/event dispatch.
- Honor message deletions and channel/user clears in retained and visible data.
- Missing/unsupported emotes fall back to readable text without blank rows,
  failed chat connections, or a synchronous retry on every frame.

**Animated inline emotes are not emote rain.** Proposal: provide Off / Static /
Animated emote display, with animation separately bounded and a static fallback.
The initial animation default is a discussion item, not decided here.

Simple follow/subscription/Bits/redeem notices are a possible follow-on within
chat; whether they belong in the first milestone remains open. Separate polls,
predictions, Hype Train widgets, moderation-management UI, and the full
ChatIntegrations rules editor are not prerequisites for rich messages/requests.

### Data and ownership requirements

- Extend plain-data messages with stable channel/user/message IDs, display
  names, roles, color, event/message kind, and ordered text/emote/badge runs.
- Do not trust display names, embedded text, or fake textual badges for role
  permissions. Use Twitch-supplied metadata tied to stable user IDs.
- Parse tags and escape sequences correctly. Preserve Unicode through parsing,
  length limits, display, and conversion of emote ranges; byte offsets are not
  interchangeable with displayed character positions.
- Generate supported TMP markup internally. Viewer text must not inject arbitrary
  tags, sprite references, control characters, or oversized layout instructions.
- Share one bounded message/event stream between display and requests. Hiding
  the panel must not disable requests if requests remain enabled. With no active
  consumer, unnecessary subscriptions and processing should stop.
- Keep the existing OAuth/Keystore implementation. A stream key is not chat
  authorization; never copy credentials into these documents, queue exports,
  debug messages, or ordinary settings fields.
- Add bounded reconnect/backoff and cancellation; distinguish authorization
  failure from temporary transport loss. Do not loop on a rejected token.
- If EventSub is added for approved notices, manage its keepalives, reconnects,
  subscription deduplication, and required scopes explicitly. Do not add it just
  to support the basic IRC `!bsr` request path.

### Image resources and rendering

- Deduplicate concurrent asset fetches; cache by provider/resource identity and
  version. Bound downloads, decoded pixels, texture memory, animation frames,
  disk cache size, and concurrent jobs, not just the number of visible widgets.
- Fetch/decode off the UI thread where the selected decoder permits. Create,
  upload, bind, and destroy Unity resources only on the Unity thread with a
  bounded upload budget. Ignore stale results after channel change or teardown.
- Retain Unity textures explicitly across scene changes; release them when no
  longer owned. The recent icon/AFK texture repairs are relevant reference work.
- Reuse visible row/image instances; recycle them safely. Do not instantiate an
  object for every message ever received or update offscreen animations.
- Recalculate layout only for changed content/width/style/resource completion,
  not continuously for all retained messages. Preserve the scroll anchor when
  an image arrives late, a row is deleted, or old history is evicted.
- Follow new messages only when already at the bottom. Scrolling upward must
  not be undone by incoming text or emotes. Offer a small return-to-live action
  if needed rather than forcibly moving the reader.
- Keep current chat/camera visibility policy. Do not turn this into a broadcast
  overlay or change render layers without a separate decision.

## 6. Standalone song requests

### Proposed viewer and streamer workflow

1. Streamer enables requests and deliberately opens the queue in Chat Control.
2. Viewer sends `!bsr <BeatSaver key>`; accepted URL forms, if any, are restricted
   to validated BeatSaver map links, not arbitrary download URLs.
3. Validate the sender, cooldown, limits, duplicate status, and map metadata.
4. Reply with acceptance/queue position or a short, specific rejection reason.
5. Streamer opens Requests to see song/artist, mapper, requester, duration,
   difficulty information, and compatibility/download state.
6. Selecting a request offers Download if missing, then Select Map/Play when
   allowed by the current game state. The viewer cannot interrupt a live map
   or force a download-and-launch transition by sending a message.
7. Track queued, selected, actually started, and completed/skipped states
   separately; selecting a row is not proof that its song was played.
8. `!link` answers with the current map's BeatSaver URL when it can be resolved;
   built-in, deleted, private, or unlisted maps receive an honest unavailable
   response. Do not fabricate links or star ratings.

The exact commands beyond `!bsr`/`!link`, whether selecting a map starts it or
only navigates to its difficulty selection, and when to remove it from the queue
are discussion items. Prefer an explicit streamer action over automatic play.

### Queue policy and persistence

- Proposed defaults: feature off; queue closed until the streamer opens it.
- Support roles, per-user limits, a total queue limit, cooldowns, duplicate
  prevention, and streamer removal/reordering. Separate display names from IDs.
- Persist queue/history/settings separately from avatar/player calibration
  profiles and separately from the 128-message display buffer.
- Partition saved requests by Twitch channel/account. Switching accounts must
  not expose or execute another channel's pending requests.
- Use bounded history and atomic saves; recover gracefully from a corrupt file.
  A restored queue must not silently reopen intake or replay old commands.
- Proposed first filters: duration and compatibility, with additional NPS,
  rating, mapper/map allow/block rules added only when their metadata semantics
  are clear. Missing data is not a zero rating; different difficulties may have
  different requirements and statistics.
- Preserve existing map announcements and use one outgoing rate limiter so
  command replies cannot flood chat or starve important notifications.

### Download and game integration

- Use BeatSaver metadata plus a Quest-native loader adapter. Do not call PC
  SongCore APIs or copy PC filesystem paths.
- Quest SongCore's `RuntimeSongLoader` exposes `RefreshSongs`, `SongsLoaded`,
  and `GetLevelByHash`; validate the matching release/API against the supported
  Beat Saber version before adding a dependency. Its current main branch is a
  feasibility reference, not proof of compatibility with the installed game.
- Download to a managed temporary location with cancellation, size/time limits,
  bounded concurrency, and progress. Avoid heavy extraction/library refresh in
  active gameplay; proposed initial policy is download on streamer selection.
- Validate archive paths, file counts, expanded size, required metadata, and
  map identity before installing. Block traversal and writes outside the chosen
  custom-song root. Do not treat the BeatSaver map hash as a ZIP-file checksum.
- Install completed maps atomically where practical; clean only owned partial
  downloads. Never delete or replace arbitrary user maps to repair a request.
- Resolve the resulting level through the loader and refresh/select it only in
  a valid menu lifecycle. Cancel obsolete UI callbacks during scene changes.
- Reject or clearly flag unavailable map formats/extensions per difficulty.
  Downloading a Noodle/Chroma/Vivify-dependent map cannot provide missing runtime
  support. Do not change any other mod or enable unsupported gameplay features.

## 7. Separation, performance, and diagnostics

Keep network/parsing, request policy/downloads, and UI presentation separable.
Use existing service ownership where appropriate, with small dedicated helpers
or controllers rather than putting the entire implementation into
`MenuController.cpp`. New module names are intentionally not prescribed yet.

- No Unity/IL2CPP references retained by network or decode workers. Hand off
  immutable/plain-data results and check lifetime/generation on application.
- No synchronous network waits, library-refresh waits, large JSON/archive
  processing, or whole-cache image decoding from a button/render callback.
- Disabling an optional feature stops its work; bounded lightweight dispatch
  can remain for other enabled consumers. No separate chat connection per tab.
- Retain detailed operation/error logs using SaberStage's custom logger, with
  throttling for repeated failures. Include operation, reason, HTTP/provider
  status, retry state, queue depth, and relevant nonsecret identifiers.
- Do not log OAuth tokens, authorization headers, stream keys, signed URLs,
  or unrestricted complete chat transcripts. Provide useful in-headset errors
  without opening a settings modal from a gameplay world-panel callback.
- Surface stalled/failed operations honestly. A download spinner or Connecting
  state must have timeout/cancel/retry behavior, not remain indefinitely.
- Quest 2 is the baseline. Measure CPU/GPU/frame-time and memory impact alongside
  normal streaming/maps before claiming overhead is unnoticeable.

## 8. Proposed implementation sequence after approval

1. **Lock decisions and preserve baseline.** Resolve the questions below, record
   approved defaults, capture current panel behavior, and create an implementation
   checkpoint. Recheck current source because other work may have occurred.
2. **Chat Control shell.** Safely open/close one popout from the existing button;
   validate flat layout, pointer blocking, grab behavior, scene lifetime, and
   controller-only usability before adding complex content.
3. **Structured chat and rich renderer.** Add metadata parsing and bounded asset
   handling; start with colors/badges/static emotes, then optional animation.
   Preserve message wrapping, history, native scrolling, and reconnect behavior.
4. **Request policy and commands.** Implement/test queue identity, validation,
   roles, limits, persistence, `!bsr`, `!link`, and rate-limited replies without
   allowing incoming commands to manipulate Unity directly.
5. **Headset queue manager and downloads.** Connect the loader/version-specific
   adapter and progress/cancel/menu selection. Test malformed, missing, and
   incompatible maps as well as ordinary requests.
6. **Acceptance and polish.** Verify in-headset controls, rich text at all panel
   sizes, long-session boundedness, performance, restart/reconnect behavior, and
   regressions. Add optional event notices only if approved for this milestone.

There is no emote-rain, other-platform, generic rule-editor, OBS/VoiceAttack,
gameplay-effect, or encoder phase in this initial implementation sequence.

## 9. Verification and acceptance checklist

Host/fixture coverage:

- [ ] IRC tags, escaping, malformed lines, Unicode/emote boundaries, missing
  metadata, message deletion/clear, duplicate events, and account changes.
- [ ] Viewer markup injection cannot alter layout; unavailable images fall back.
- [ ] Rich row heights and virtualized content size remain correct with mixed
  text/emotes, maximum-size chat, late assets, resize, and history eviction.
- [ ] Command permissions cannot be spoofed; filtered commands still reach the
  request service; rate limits, persistence recovery, and duplicate rules work.
- [ ] Bounded assets/jobs/queues, cancellation, stale callbacks, failed refresh,
  partial archives, traversal attempts, and offline services fail safely.
- [ ] Settings defaults/migration preserve current panel geometry and existing
  Twitch account, stream destinations, title, and audio/recording behavior.

Headset acceptance (cannot be replaced by source-string tests):

- [ ] Chat Control opens in menus, maps, and paused maps, facing the user; close,
  reopen, grab, and repeated clicks do not crash or leak panels/subscriptions.
- [ ] Existing scrollbar still appears on overflow; joystick scroll works when
  returning to the body without first clicking an arrow; grip/trigger behavior
  does not steal button or scrolling input.
- [ ] Messages stay left-aligned/wrapped; badges/emotes are readable and clipped
  inside the viewport; late assets do not shift the reader away from old messages.
- [ ] Request acceptance, rejection, reordering, download/cancel, compatible map
  selection, and `!link` work using only the headset/controller workflow.
- [ ] Hidden chat does not disable enabled requests; turning requests off stops
  intake without affecting the actual livestream or deleting the queue.
- [ ] Live reconnect, token refresh, scene changes, panel resize, and game restart
  preserve the intended state without duplicate requests or stale resources.
- [ ] Busy chat and extended streaming remain bounded; animation-off behaves as
  expected; no visible UI hitch from network/decode/download callbacks.
- [ ] Avatar/camera/recording settings and unrelated menu layouts are unchanged.

## 10. Decisions for the next discussion

| Question | Proposed starting point, not yet approved |
| --- | --- |
| Inline animation default? | Static emotes by default; optional animation with a bounded budget |
| Popout organization and saved placement? | Chat / Requests / Request Settings; remember pose, open only on demand |
| Download timing and play action? | Download when the streamer selects a request; navigate to map/difficulty selection rather than auto-start |
| Queue defaults/limits? | Disabled/closed initially; decide per-user count, total limit, cooldown, and duplicate horizon together |
| Queue retention/removal? | Preserve pending requests across restart but reopen intake explicitly; define when started/completed/skipped requests leave the queue |
| First-milestone extras? | Decide whether simple follow/sub/redeem notices and basic moderation belong now or after rich chat + requests |

A screenshot would identify the exact BS+ left strip if the accent described in
section 3 does not match what the user saw. This is not a blocker for the plan.

## 11. Reference material and checkpoint record

Reviewed BS+ commit: `632addd001d29a7beee14a52eadf87ccb2792a3a`
(`V6.4.5`, 2026-06-14). Its linked submodules at this revision are:

- ChatPlexSDK-BS: `857d6f53a39bc3ea9d0507b061d571fa71c8a3c0`.
- ChatPlexSDK: `9c209664b5a0c64f9c9c1e7e152b0ca6d1dab7ce`.

Local read-only reference clone used for inspection:
`C:/Users/Owner/AppData/Local/Temp/SaberStage-BeatSaberPlus-Review-20260902`.
This temporary directory may disappear. The pinned links below are the durable
references; do not require the temporary clone to build SaberStage.

- [BS+ rich message construction][bs-message-builder]: badges/emotes/style.
- [BS+ message widget][bs-widget] and [chat view][bs-chat-view]: pooling, row
  dimensions, accents, events, and renderer behavior.
- [BS+ request configuration][bs-request-config],
  [command dispatch][bs-request-commands], and
  [request manager][bs-request-manager]: policies, queue UI, download/select flow.
- [ChatPlex service registration][sdk-service]: shared service lifetime;
  inspected public code directly registers Twitch plus external-provider hooks.
- [Quest SongCore runtime loader][quest-loader]: candidate native integration.
- [Twitch EventSub WebSockets][twitch-eventsub] and
  [Twitch authorization scopes][twitch-scopes]: references for later approved
  notices/moderation; verify current requirements again when implementing.

Related SaberStage repair records:

- [Chat scrollbar and joystick investigation](../ai-assisted-development/reviews/CHAT_PANEL_SCROLLBAR_INVESTIGATION.md).
- [Recording panel icon repair](../ai-assisted-development/reviews/RECORDING_PANEL_AUDIO_ICON_REPAIR.md).
- [Paused-map stream/AFK crash repair](../ai-assisted-development/reviews/STREAM_PAUSE_CRASH_REPAIR.md).

The commit containing this plan also preserves those already-existing repairs
and their tests. They are not implementations of this plan. No C++, shader,
settings, or test source is edited as part of writing this document.
Device backups, diagnostics, and recordings stay outside this commit.

Checkpoint validation on 2026-09-02: the existing host build was up to date;
8/8 host suites and 54/54 tooling checks passed again. Prior ARM64/deployment
evidence is in the stream-pause repair record. No new device session was run
for this planning task, and no new rich-chat/request runtime behavior is claimed.

[bs-widget]: https://github.com/hardcpp/BeatSaberPlus/blob/632addd001d29a7beee14a52eadf87ccb2792a3a/Modules/BeatSaberPlus_Chat/ChatPlexMod_Chat/Components/ChatMessageWidget.cs
[bs-chat-view]: https://github.com/hardcpp/BeatSaberPlus/blob/632addd001d29a7beee14a52eadf87ccb2792a3a/Modules/BeatSaberPlus_Chat/ChatPlexMod_Chat/UI/ChatFloatingPanelView.cs
[bs-message-builder]: https://github.com/hardcpp/BeatSaberPlus/blob/632addd001d29a7beee14a52eadf87ccb2792a3a/Modules/BeatSaberPlus_Chat/ChatPlexMod_Chat/Utils/ChatMessageBuilder.cs
[bs-request-config]: https://github.com/hardcpp/BeatSaberPlus/blob/632addd001d29a7beee14a52eadf87ccb2792a3a/Modules/BeatSaberPlus_ChatRequest/CRConfig.cs
[bs-request-commands]: https://github.com/hardcpp/BeatSaberPlus/blob/632addd001d29a7beee14a52eadf87ccb2792a3a/Modules/BeatSaberPlus_ChatRequest/ChatRequest_Commands.cs
[bs-request-manager]: https://github.com/hardcpp/BeatSaberPlus/blob/632addd001d29a7beee14a52eadf87ccb2792a3a/Modules/BeatSaberPlus_ChatRequest/UI/ManagerMainView.cs
[sdk-service]: https://github.com/hardcpp/ChatPlexSDK/blob/9c209664b5a0c64f9c9c1e7e152b0ca6d1dab7ce/Chat/Service.cs
[sdk-twitch]: https://github.com/hardcpp/ChatPlexSDK/blob/9c209664b5a0c64f9c9c1e7e152b0ca6d1dab7ce/Chat/Services/Twitch/TwitchService.cs
[quest-loader]: https://github.com/raineaeternal/Quest-SongCore/blob/main/shared/SongLoader/RuntimeSongLoader.hpp
[twitch-eventsub]: https://dev.twitch.tv/docs/eventsub/handling-websocket-events/
[twitch-scopes]: https://dev.twitch.tv/docs/authentication/scopes/
