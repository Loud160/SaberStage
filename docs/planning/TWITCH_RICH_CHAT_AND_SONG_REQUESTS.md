<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors -->
<!-- Part of SaberStage. See LICENSE and LICENSE-ADDITIONAL-TERMS.md. -->

# Twitch rich chat and song requests — discussion and implementation plan

Date: 2026-09-02.

Decision update: incorporates the user's numbered answers following checkpoint
`1d12b80` and the subsequent instruction to copy BS+'s full chat-related UI
workflow, design, and layout. That latest instruction supersedes the earlier
custom screen/grouping proposals; it is not merely a request for similar styling.

**Status: implementation authorized by the user on 2026-09-02.** Starting
checkpoint: `7628a89`. The user is testing the preceding recording build;
do not deploy, stop the game, or perform live account actions without a new
go-ahead. Track implementation and verification in
[the implementation record](../ai-assisted-development/reviews/TWITCH_CHAT_IMPLEMENTATION.md).

Build update, 2026-09-03: the source implementation is packaged for headset
acceptance. See the [usage guide](../TWITCH_CHAT_AND_REQUESTS.md) and the linked
record for exact functionality, standalone UI adaptations, passed host/build
checks and outstanding runtime/PC-layout comparison. No new device deployment
or full UI-parity claim is implied by this update.

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
- Reproduce BS+'s chat-related UI workflow, design, and layout for the supported
  features. Preserve SaberStage's working scrolling, controller input, resource
  bounds, and scene safety while changing the visual composition as required.
- Add the rich message presentation used by BS+ Chat.
- Add viewer song requests and a queue the streamer can inspect/manage/play.
- Show requests in their **own movable/resizable world panel**, visible beside
  chat. Use the PC request manager's list, selected-map detail area, and contextual
  Download/Play action, not the earlier proposal for buttons on every row.
  The queue is not a settings tab or a replacement for the chat display.
- Put adjustable chat/request settings in the Chat Control popout menu, not in
  the main mod menu or mixed into the request-list rows.
- Default inline emotes to static display, with an option for animation.
- Let the streamer configure how many requests may be pending. The pending
  limit is a settings value, not a fixed hard-coded allowance.
- Download on streamer selection, then navigate to the map's difficulty
  selection. Do not automatically start the map.
- Preserve pending requests across crashes/restarts. The user explicitly
  identified this as important because the Quest game can crash frequently.
- Include both stream-event notices and basic moderation in the initial scope,
  following the PC mod's established user workflows with Quest-native plumbing.
- **Full PC UI parity is the target, not inspiration.** Copy the supported
  screens' navigation, layout, grouping/order, labels, styling, selection/action
  sequences, and feedback. The latest instruction overrides earlier custom
  layout choices wherever they conflict. Existing Chat Control/world-panel
  entry points are Quest hosting requirements, not permission to redesign the
  contents of the PC views.
- **Defer emote rain.** Performance and whether rain belongs in the headset,
  third-person camera, or both have not been decided.
- Leave YouTube and Kick exactly as they are. No new provider implementation,
  connection flow, destination-setting changes, or claims of new support.
- Do not change camera settings, encoding, stream audio, or other
  unrelated controls as part of this work.

The confirmed choices above and in section 10 supersede earlier proposals.
Remaining numeric defaults and unavoidable platform adaptations are marked as
proposals, not additional requirements already approved by the user. Layout is
no longer an open-ended design exercise. The narrower feature scope here
takes precedence over the broader historical
[chat planning prompt](19_PROMPT_18_STREAM_CHAT_PANEL.md) for this effort only.

### Adaptation principle

The pinned BS+ chat, request, and moderation screens are the UI specification.
Reproduce their screen structure, navigation/back behavior, sections, relative
proportions, alignments, control order, selection state, dialogs, and action
feedback. Do not invent replacement tabs, flatten a multi-pane view into an
unrelated form, or duplicate selected-item actions on every list row.

Use the existing Chat Control button to enter these controls and host the
request manager independently beside chat. Where BS+ uses left/main/right
views, preserve that logical and visual arrangement in the Quest world-space
host. Do not use the previous requirement to resemble SaberStage's current
chat panel as a reason to override the PC layout. Conversely, visual parity is
not permission to regress working scrolling, clipping, grabbing, persistence,
or scene lifetime, or to change unrelated main-menu tabs.

Adapt the implementation rather than redesigning the experience. Necessary
differences may include physical canvas scale/controller input, Quest-native
APIs, rendering/memory budgets, standalone configuration instead of a PC web
tool, and intentionally excluded features. Record each actual difference with
its reason; do not silently remove approved features or invent limitations.
Use accurate SaberStage identity/help destinations, not another product's
branding or nonfunctional external-configuration links.

The earlier custom settings-tab table, per-row Download/Play buttons, proposed
omission of the platform strip, and mandatory custom color/alignment rules are
superseded. Keep confirmed functional requirements such as crash-safe queues,
static emotes by default, and no automatic gameplay start. UI parity does not
require copying PC-specific dependencies or unsafe persistence behavior.

Capture the actual PC screens and interaction states before implementing each
view. Compare the Quest result against them for placement, proportions,
readability, and workflow, including selected/empty/loading/error states.
Reading source or passing a build alone cannot establish visual parity.

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
| Display | Left-aligned/wrapped virtualized rows in a movable/resizable panel | PC chat presentation, inline badges/emotes, styles and filters without breaking scrolling |
| Panel controls | Resize/Lock Size, viewer count, Chat Control placeholder | Wire Chat Control to a real world-space menu |
| Chat Control | Currently an empty callback and explicitly non-interactable | Open/focus the popout; no dummy action or duplicate panel instances |
| Requests | No incoming command dispatcher, request queue, or map downloader | A bounded request service and headset queue UI |
| Outgoing messages | Existing map announcements and Twitch API sending path | Reusable, rate-limited command replies and request acknowledgements |

The pending work included in this checkpoint already extends maximum panel
dimensions to 240 x 200 canvas units, keeps default/reset dimensions 70 x 58,
and sizes the recycled text-row pool to 50. History remains 128 messages.
Do not inadvertently revert these changes when adding rich content. Preserve
the usable physical size range and input behavior while mapping the PC design
to Quest canvas units; do not paste PC numeric dimensions into a different
coordinate system or freeze the old internal layout against the new instruction.

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

**Updated decision:** retain the PC platform-origin accent and its appearance
toggle. The earlier suggestion to omit it because this release is Twitch-only
is superseded by the full-UI instruction. It remains a per-message indicator,
not a substitute for the functional scrollbar. Retain provider identity in the
message model even though this effort only implements Twitch.

## 4. Chat settings popout and separate request panel

### Entry point and lifetime

- Clicking the existing Chat Control button opens or focuses one world-space
  menu. Repeated clicks must not allocate duplicate menus or event subscriptions.
- Keep the original chat and any open request panel visible while the settings
  menu is open. Closing any of these panels does not disconnect chat, stop
  streaming, clear requests, or reset the other panels' placement.
- Give the menu a clear Close button and a grabbable background. Preserve the
  existing priority of button interaction, scrolling, resizing, and panel drag.
- Use the PC view's visual hierarchy, styling, controls, and relative layout.
  Map them to flat Quest world surfaces without unwanted bloom, unreadable
  contrast, pointer pass-through, or misleading enabled/disabled states. Do not
  impose the earlier bespoke black/blue-panel design over the PC reference.
- Spawn it facing the player and within reach. Proposed behavior is to remember
  its pose, but not reopen it automatically after restarting the game.
- Keep existing main-menu Show Chat/reset entry points available for recovery
  unless the user separately requests moving them. No new rich-chat or queue
  configuration should be inserted into the Live tab.

### PC screen map — replaces the custom settings-tab proposal

Use the corresponding source views and actual rendered screens, not a new
Chat / Request Settings / Notices / Panel Controls tab design. Preserve the
supported controls' order and grouping in their PC screen. The following map
identifies the source of truth; it is not a replacement layout sketch.

| Surface | PC source and structure to reproduce |
| --- | --- |
| Chat appearance/settings | [SettingsMainView][bs-chat-settings-main] contains the two appearance columns: dimensions/font/order/platform accent and colors. [SettingsLeftView][bs-chat-settings-left] provides information/reset actions; [SettingsRightView][bs-chat-settings-right] provides movement/viewer/command/event toggles. Preserve these groups and their PC alignments. |
| Request configuration | The request module's [settings views][bs-request-settings] define its configuration layout and policy grouping. Keep these separate from the live queue manager, entered through Chat Control's settings host. |
| Request manager | [ManagerViewFlowCoordinator][bs-request-flow] composes left/main/right views. [ManagerMainView][bs-request-manager] has Queue / History / Allowlist / Blocklist above a list and adjacent selected-map details/actions. [ManagerLeftView][bs-request-left] holds tools/intake operations; [ManagerRightView][bs-request-right] holds map information/link actions. |
| Moderation | [ModerationViewFlowCoordinator][bs-moderation-flow] composes left/main/right views and navigation to shortcuts. Use its selected-user/message context and the existing [main][bs-moderation-main] / [target-action][bs-moderation] structure for approved moderation actions. |
| Chat display/notices | [ChatFloatingPanelView][bs-chat-view] and [ChatMessageWidget][bs-widget] define message layout, badges/emotes, accents, highlights, and notice presentation. |

For example, the PC appearance view uses two columns with centered setting
labels. Do not apply a universal left-align-every-label rule from the old plan
to that view. Ordinary chat message text still stays left-aligned and wraps.
Preserve the request list/details proportions rather than stretching every
button to the whole panel or adding competing nested width calculations.

The PC web-configuration dependency needs a native settings equivalent for
standalone operation; it is not an excuse to redesign controls already present
in-headset. Any required new Quest-only setting belongs with its corresponding
PC feature group. Do not add fake pages for rain, other services, or a generic
rules editor, or move unrelated stream title/account settings into these views.

### Separate request-list panel

- Create an independently movable/resizable flat world-panel host for the PC
  request-manager composition. Save its position and size independently;
  provide recovery/reset without changing the manager's internal workflow.
- Keep it open beside chat while the streamer talks to viewers. Opening this
  panel must not replace chat or require leaving settings open.
- Reproduce Queue / History / Allowlist / Blocklist, list selection, and the
  adjacent selected-map detail area. Keep the PC empty-selection prompt,
  cover/details, contextual primary action, and secondary action behavior.
  Download/Play belongs to the visibly selected map, not every row. Selection
  and async actions must use stable request/map identity, not pooled row index.
- As in the PC manager, the selected-map primary action reads Download for a
  missing map and Play when installed/usable. Show bounded progress, cancel/retry,
  and a specific error when needed. Navigation requires a safe selection state.
  The Play action takes the streamer to difficulty selection, not into gameplay.
- The streamer-selected download/select flow can continue to difficulty
  selection once its download and library refresh succeed. Browsing a row or
  receiving a viewer command alone must not start network installation or play.
- Keep tools, intake, history/requeue, and selected-map information/actions in
  the corresponding PC panes. Adjustable policy values remain in the request
  settings views. Distinguish closing this window from closing request intake.
- Reuse bounded visible rows and preserve scrollbar/joystick support. Derive
  this panel's pool from its own row heights; do not blindly reuse the
  chat text pool's 50-row count or assume arbitrary request rows fit its geometry.
- Reuse the same Twitch connection and Quest panel infrastructure, not an
  unrelated duplicate of chat's header or controls. Follow the PC request view.

### Layout and input contracts

- One authoritative measured content width per page. Text, controls, clipping,
  and row sizing must agree on that width after parent layout resolves.
- Reproduce each PC view's intended alignment/grouping inside its visible area;
  buttons/sliders cannot extend beyond its right edge. Long labels/tooltips/
  confirmations wrap. Do not use several nested auto-sizing groups with
  contradictory widths or manual offsets to mask them.
- Preserve the PC row/column structure and scale the host coherently. Test at
  the smallest readable supported menu size and the largest chat size; do not
  replace the design with a new row-first or column-first arrangement.
- Retain the native scrollbar, page buttons, and HMUI joystick behavior. Pointer
  entry must wake the native scroll component without requiring an arrow click.
- Decorative images/emotes do not intercept pointer input. Opening controls must
  not cause click-through into the map, underlying menu, or the chat drag handle.
- The new menu must be safe in gameplay and while a map is paused. Never parent
  a gameplay-opened modal to an inactive settings-menu view. The prior stream
  pause crash demonstrates that an outer try/catch does not make that safe.
- Apply these contracts to the settings, requests, and moderation views.
  List selection and selected-map action buttons must take input before the
  grabbable background; adding a list must not make the whole body ungrabbable
  or suppress its native scroll.

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

**Animated inline emotes are not emote rain.** Static by default with optional
animation is confirmed. Use the PC feature's settings group and presentation
where available; any necessary Quest animation-budget control belongs there,
not in a newly invented settings hierarchy. Keep animation separately bounded
and provide a static fallback when a format or budget does not support it.

### Stream notices and basic moderation — included

The user approved both of these for the initial implementation, not merely a
future placeholder. Reproduce the PC mod's UI/workflow rather than invent
a general scripting system or rely on OBS/Streamlabs for the operation.

- Display follow, subscription/gift, Bits, and channel-point redemption notices
  in the rich chat flow, with useful names/details and emotes/badges where
  supplied. Keep visibility/filter settings in Chat Control.
- Reuse the shared Twitch connection/account, request only the necessary scopes,
  and explain missing permissions or channel-ineligible events. Do not claim
  that connecting a stream key authorizes these features.
- Deduplicate provider events and avoid showing the same subscription/gift as
  separate duplicate notices from IRC and EventSub. Reconnect must not replay
  already-handled notices or reset ordinary message scrolling.
- Provide basic user/message moderation: select the relevant user/message,
  then timeout, ban, or delete with clear target information and confirmation
  for destructive actions. Keep the familiar PC selected-user/action workflow.
- Preserve the PC moderation view/layout and target/action sequence, adapting
  input to controllers without requiring a PC keyboard, a new main-menu tab,
  or a separate bot program. Settings belong in Chat Control; actions belong
  with the selected target. Do not replace it with a custom moderation form.
- Use Twitch's supported moderation APIs with the connected user's actual
  permissions. Do not blindly copy a UI's slash-command string as proof of a
  working backend. Show success only after the service confirms the action;
  report failures without falsely hiding content or disconnecting the stream.
- Apply provider-confirmed deletions/clears to visible and retained messages,
  including offscreen rows. Avoid selecting a recycled row's new occupant when
  an asynchronous moderation result arrives; target stable message/user IDs.

References: [PC moderation target/actions][bs-moderation],
[PC moderation controls][bs-moderation-main], and
[Twitch moderation API guidance][twitch-moderation]. Separate polls,
predictions, Hype Train widgets, and the full ChatIntegrations rules editor
remain outside this initial scope; approval of notices/moderation is not
approval of all unrelated BS+ modules.

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
- Add EventSub/subscriptions needed by the approved notices and manage
  keepalives, reconnects, deduplication, and required scopes explicitly.
  Keep ordinary IRC messages/requests independent where possible: unavailable
  notice permissions must not break a working basic `!bsr` request path.

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

### Viewer and streamer workflow

1. Streamer enables/configures requests in Chat Control and opens the separate
   request-list panel. Request intake is a distinct open/closed operation.
2. Viewer sends `!bsr <BeatSaver key>`; accepted URL forms, if any, are restricted
   to validated BeatSaver map links, not arbitrary download URLs.
3. Validate the sender, cooldown, limits, duplicate status, and map metadata.
4. Save an accepted request safely, then reply with acceptance/queue position.
   Rejections receive a short, specific reason. Do not acknowledge an accepted
   request that only exists in unsaved memory.
5. The separate request panel uses the PC list and adjacent selected-map
   details/actions. Show song/artist, mapper, requester, duration, difficulty,
   and compatibility in the corresponding list/detail/information areas.
   Selecting a row changes the displayed target; it does not start a download.
6. When the streamer chooses the download/select action, download if missing,
   refresh the library, and navigate to that map's difficulty selection when
   allowed by the current game state. Already installed maps skip downloading.
   Do not auto-start a map, interrupt active gameplay, or let an incoming viewer
   command force a download-and-launch transition.
7. Track queued, selected, actually started, and completed/skipped states
   separately; selecting a row is not proof that its song was played.
8. `!link` answers with the current map's BeatSaver URL when it can be resolved;
   built-in, deleted, private, or unlisted maps receive an honest unavailable
   response. Do not fabricate links or star ratings.

Download-on-selection and navigation to difficulty selection are confirmed.
Keep a request marked as playing during its actual attempt; move it to history
when the attempt ends, recording completed/failed/quit. Provide Requeue for a
retry and retain explicit remove/skip actions. Merely browsing, downloading, or
selecting a difficulty must not mark the request completed.
The exact command aliases beyond `!bsr`/`!link` can follow the PC conventions.

### Queue policy and persistence

- Proposed defaults: feature off; queue closed until the streamer opens it.
- Expose the maximum pending-request count as a user-adjustable value in Request
  Settings. Do not hard-code the previously suggested 25 requests as policy.
  Keep finite validated limits for safe memory/storage use; the UI range and
  initial default still need choosing. Lowering the setting below the current
  count stops further intake; it must not silently discard existing requests.
- Support roles, per-user limits, a total queue limit, cooldowns, duplicate
  prevention, and streamer removal/reordering. Separate display names from IDs.
  The previously suggested 2-per-viewer and 30-second cooldown are not confirmed
  constants. Keep policy adjustable and distinguish overall pending capacity
  from any per-viewer allowance.
- Persist queue/history/settings separately from camera and recording settings
  profiles and separately from the 128-message display buffer.
- Partition saved requests by Twitch channel/account. Switching accounts must
  not expose or execute another channel's pending requests.
- Persist every accepted/reordered/removed/state-changed request through a
  serialized, bounded persistence path. Do not depend on closing the panel,
  stopping the stream, or a clean game shutdown to save the queue.
- Use crash-safe atomic snapshots and/or a bounded journal with a known-good
  recovery copy. Define and test the durable commit point; an in-memory change
  or buffered write alone is not enough to promise crash recovery.
- Keep persistence off the Unity thread. Report storage/permission/disk-full
  failures honestly and delay acceptance acknowledgement until durable storage
  succeeds; a save failure must not destroy the previous valid queue.
- Restore pending requests and ordering after a crash without replaying old
  commands or silently reopening intake. Restore an interrupted playing item
  as recoverable/interrupted, not completed or silently removed.
- Recover interrupted downloads separately from queue identity: retain the
  request, clean only its owned partial files, and offer download/retry again.
  Preserve successfully installed maps and bounded played/failed/quit history.
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
  active gameplay; download on streamer selection is the agreed policy.
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

1. **Inventory the PC UI and preserve the baseline.** Record defaults, capture
   current SaberStage behavior, and create an implementation checkpoint. Recheck
   current source because other work may have occurred. Capture the pinned PC
   chat/request/moderation screens and important interaction states; map each
   supported control, navigation path, and view to its Quest implementation.
   Record specific required deviations, not a new design proposal.
2. **PC-layout panel shells.** Host the PC settings views from Chat Control and
   its request-manager arrangement in the separate world panel. Validate actual
   layout/proportions, simultaneous visibility, independent placement/size,
   pointer blocking, grab behavior, scene lifetime, and controller-only use.
   Do not first build custom placeholder layouts and layer fixes over them.
3. **Structured chat and rich renderer.** Add metadata parsing and bounded asset
   handling; start with colors/badges/static emotes, then optional animation.
   Preserve message wrapping, history, native scrolling, and reconnect behavior.
4. **Notices and moderation.** Add approved Twitch event subscriptions and
   familiar selected-user/message actions, permission/error UI, deletion/clear
   handling, and bounded event processing. These are part of the first release
   of this work, not deferred until a general rule engine exists.
5. **Request policy and commands.** Implement/test queue identity, validation,
   configurable limits, roles, crash-safe persistence, `!bsr`, `!link`, and
   rate-limited replies without incoming commands manipulating Unity directly.
6. **Headset queue manager and downloads.** Populate the PC-style list, selected
   map details, and contextual Download/Play/secondary actions. Connect the
   version-specific loader and progress/cancel/difficulty selection. Test
   malformed, missing, incompatible, and interrupted requests, plus changes
   in selection while an operation is pending.
7. **Acceptance and polish.** Verify in-headset controls, rich text at all panel
   sizes, long-session boundedness, performance, restart/reconnect behavior, and
   regressions, including notices, moderation, and crash recovery.

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
- [ ] Configurable pending limits survive restart; lowering a limit never
  discards existing requests; per-viewer and total limits have distinct meaning.
- [ ] Crash fixtures cover interrupted writes, acknowledgement-before-save
  prevention, corrupt snapshot/journal recovery, disk-full failures, interrupted
  play/download, and no duplicate or silently lost acknowledged requests.
- [ ] Notice deduplication, missing OAuth scopes, denied moderation, stale row
  targets, successful timeout/ban/delete, and provider clears are covered.
- [ ] Bounded assets/jobs/queues, cancellation, stale callbacks, failed refresh,
  partial archives, traversal attempts, and offline services fail safely.
- [ ] Settings defaults/migration preserve saved panel placement/usable size
  ranges and existing Twitch account, stream destinations, title, and audio/
  recording behavior while applying the requested internal PC layout.

Headset acceptance (cannot be replaced by source-string tests):

- [ ] Chat Control opens in menus, maps, and paused maps, facing the user; close,
  reopen, grab, and repeated clicks do not crash or leak panels/subscriptions.
- [ ] Chat, settings, and requests coexist. The independent request panel can
  be moved/resized/scrolled. Its selected-map details/actions target the correct
  request after tab/selection changes and row recycling. Policy controls stay
  in their corresponding PC settings views rather than cluttering list rows.
- [ ] Existing scrollbar still appears on overflow; joystick scroll works when
  returning to the body without first clicking an arrow; grip/trigger behavior
  does not steal button or scrolling input.
- [ ] Messages stay left-aligned/wrapped; badges/emotes are readable and clipped
  inside the viewport; late assets do not shift the reader away from old messages.
- [ ] Request acceptance, rejection, reordering, download/cancel, compatible map
  difficulty selection without auto-start, and `!link` work using only the
  headset/controller workflow. Closing a panel does not clear the queue.
- [ ] Force-close/crash recovery in an explicitly authorized test preserves
  acknowledged pending requests/order, restores interrupted items, and leaves
  intake closed until deliberately reopened. No clean shutdown is required.
- [ ] Notices and basic moderation work directly from Quest with the connected
  Twitch account; permission/failure messages are accurate and no PC helper is
  required. Actions affect the intended user/message, not a recycled UI row.
- [ ] Hidden chat does not disable enabled requests; turning requests off stops
  intake without affecting the actual livestream or deleting the queue.
- [ ] Live reconnect, token refresh, scene changes, panel resize, and game restart
  preserve the intended state without duplicate requests or stale resources.
- [ ] Busy chat and extended streaming remain bounded; animation-off behaves as
  expected; no visible UI hitch from network/decode/download callbacks.
- [ ] Camera/recording settings and unrelated menu layouts are unchanged.
- [ ] Compare actual Quest captures/interaction against the PC chat, settings,
  request manager, notices, and moderation views. Verify screen structure,
  grouping/order, alignment/proportions, styling/labels, selection/action flow,
  navigation, and empty/loading/error states, not merely recognizable colors.
  Record necessary Quest/supported-scope differences explicitly. Do not claim
  full parity based only on source review or successful tests/builds.

## 10. Confirmed decisions and remaining defaults

The user's numbered answers and latest full-UI instruction resolve the major
choices. The latest instruction takes precedence over earlier layout proposals:

| Decision | Confirmed direction |
| --- | --- |
| Inline emotes | Static by default; optional animation. Emote rain remains deferred. |
| Pending requests | Streamer-adjustable pending-request allowance, not a fixed limit. |
| Download/Play | Streamer selection downloads as needed and navigates to the map's difficulty selection; no automatic gameplay start. |
| Queue recovery | Persist across crashes/restarts; accepted requests must not rely on graceful shutdown to survive. Keep attempt history/requeue behavior. |
| Panel separation | Requests have their own world-panel host visible beside chat, using the PC manager's list plus selected-map details/actions. Settings views are hosted through Chat Control. |
| Notices/moderation | Include both now and adapt the established PC workflows to standalone Quest. No OBS/Streamlabs/PC companion requirement. |
| UI workflow, design, and layout | Copy the full supported PC UI, not just its general look. Preserve its grouping/order, proportions, alignment, navigation, selection/actions, and styling; only required standalone/scope adaptations differ. |
| Superseded proposals | No custom Chat/Requests/Notices/Panel Controls tab scheme, no Download/Play on every row, no omission of the PC platform accent, and no universal custom color/alignment rules overriding PC views. |

Not yet specified numerically: initial pending limit, its exposed range,
per-viewer allowance, cooldown, and duplicate-history horizon. These should be
configurable sensible defaults, not represented as user-approved numbers.
Quest world-panel opening/remembering/reset behavior and physical canvas scale
still need implementation details. The moderation/request/settings layout is
specified by the PC reference, not an unresolved choice for a custom design.
General rules, polls/prediction controls, and rain are still deferred.

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
- BS+ chat settings: [left][bs-chat-settings-left],
  [main][bs-chat-settings-main], and [right][bs-chat-settings-right] views are
  the appearance/filter/reset grouping and alignment references.
- [BS+ request configuration][bs-request-config],
  [command dispatch][bs-request-commands], and
  [request manager][bs-request-manager]: policies, queue UI, download/select flow.
- BS+ requests: [manager composition][bs-request-flow],
  [tools][bs-request-left], [map information][bs-request-right], and
  [settings view directory][bs-request-settings] define its multi-pane layout
  and separate configuration screens. Compare rendered states as well as code.
- [BS+ moderation target/actions][bs-moderation] and
  [moderation controls][bs-moderation-main]: reference workflow for the approved
  controller-accessible user/message moderation features; the
  [flow coordinator][bs-moderation-flow] defines composition/back navigation.
- [ChatPlex service registration][sdk-service]: shared service lifetime;
  inspected public code directly registers Twitch plus external-provider hooks.
- [Quest SongCore runtime loader][quest-loader]: candidate native integration.
- [Twitch EventSub WebSockets][twitch-eventsub] and
  [Twitch authorization scopes][twitch-scopes]: references for approved
  notices/moderation; verify current requirements again when implementing.

Related SaberStage repair records:

- [Chat scrollbar and joystick investigation](../ai-assisted-development/reviews/CHAT_PANEL_SCROLLBAR_INVESTIGATION.md).
- [Recording panel icon repair](../ai-assisted-development/reviews/RECORDING_PANEL_AUDIO_ICON_REPAIR.md).
- [Paused-map stream/AFK crash repair](../ai-assisted-development/reviews/STREAM_PAUSE_CRASH_REPAIR.md).

The original plan checkpoint `1d12b80` also preserves those already-existing
repairs and their tests. They are not implementations of this plan. This
subsequent decision update edits only this document; no C++, shader, settings,
or test source is edited. Device backups, diagnostics, and recordings remain
outside the documentation change.

Checkpoint validation on 2026-09-02: the existing host build was up to date;
8/8 host suites and 54/54 tooling checks passed again. Prior ARM64/deployment
evidence is in the stream-pause repair record. No new device session was run
for this planning task, and no new rich-chat/request runtime behavior is claimed.

[bs-widget]: https://github.com/hardcpp/BeatSaberPlus/blob/632addd001d29a7beee14a52eadf87ccb2792a3a/Modules/BeatSaberPlus_Chat/ChatPlexMod_Chat/Components/ChatMessageWidget.cs
[bs-chat-view]: https://github.com/hardcpp/BeatSaberPlus/blob/632addd001d29a7beee14a52eadf87ccb2792a3a/Modules/BeatSaberPlus_Chat/ChatPlexMod_Chat/UI/ChatFloatingPanelView.cs
[bs-chat-settings-left]: https://github.com/hardcpp/BeatSaberPlus/blob/632addd001d29a7beee14a52eadf87ccb2792a3a/Modules/BeatSaberPlus_Chat/ChatPlexMod_Chat/UI/SettingsLeftView.cs
[bs-chat-settings-main]: https://github.com/hardcpp/BeatSaberPlus/blob/632addd001d29a7beee14a52eadf87ccb2792a3a/Modules/BeatSaberPlus_Chat/ChatPlexMod_Chat/UI/SettingsMainView.cs
[bs-chat-settings-right]: https://github.com/hardcpp/BeatSaberPlus/blob/632addd001d29a7beee14a52eadf87ccb2792a3a/Modules/BeatSaberPlus_Chat/ChatPlexMod_Chat/UI/SettingsRightView.cs
[bs-message-builder]: https://github.com/hardcpp/BeatSaberPlus/blob/632addd001d29a7beee14a52eadf87ccb2792a3a/Modules/BeatSaberPlus_Chat/ChatPlexMod_Chat/Utils/ChatMessageBuilder.cs
[bs-request-config]: https://github.com/hardcpp/BeatSaberPlus/blob/632addd001d29a7beee14a52eadf87ccb2792a3a/Modules/BeatSaberPlus_ChatRequest/CRConfig.cs
[bs-request-commands]: https://github.com/hardcpp/BeatSaberPlus/blob/632addd001d29a7beee14a52eadf87ccb2792a3a/Modules/BeatSaberPlus_ChatRequest/ChatRequest_Commands.cs
[bs-request-manager]: https://github.com/hardcpp/BeatSaberPlus/blob/632addd001d29a7beee14a52eadf87ccb2792a3a/Modules/BeatSaberPlus_ChatRequest/UI/ManagerMainView.cs
[bs-request-flow]: https://github.com/hardcpp/BeatSaberPlus/blob/632addd001d29a7beee14a52eadf87ccb2792a3a/Modules/BeatSaberPlus_ChatRequest/UI/ManagerViewFlowCoordinator.cs
[bs-request-left]: https://github.com/hardcpp/BeatSaberPlus/blob/632addd001d29a7beee14a52eadf87ccb2792a3a/Modules/BeatSaberPlus_ChatRequest/UI/ManagerLeftView.cs
[bs-request-right]: https://github.com/hardcpp/BeatSaberPlus/blob/632addd001d29a7beee14a52eadf87ccb2792a3a/Modules/BeatSaberPlus_ChatRequest/UI/ManagerRightView.cs
[bs-request-settings]: https://github.com/hardcpp/BeatSaberPlus/tree/632addd001d29a7beee14a52eadf87ccb2792a3a/Modules/BeatSaberPlus_ChatRequest/UI
[bs-moderation]: https://github.com/hardcpp/BeatSaberPlus/blob/632addd001d29a7beee14a52eadf87ccb2792a3a/Modules/BeatSaberPlus_Chat/ChatPlexMod_Chat/UI/ModerationRightView.cs
[bs-moderation-main]: https://github.com/hardcpp/BeatSaberPlus/blob/632addd001d29a7beee14a52eadf87ccb2792a3a/Modules/BeatSaberPlus_Chat/ChatPlexMod_Chat/UI/ModerationMainView.cs
[bs-moderation-flow]: https://github.com/hardcpp/BeatSaberPlus/blob/632addd001d29a7beee14a52eadf87ccb2792a3a/Modules/BeatSaberPlus_Chat/ChatPlexMod_Chat/UI/ModerationViewFlowCoordinator.cs
[sdk-service]: https://github.com/hardcpp/ChatPlexSDK/blob/9c209664b5a0c64f9c9c1e7e152b0ca6d1dab7ce/Chat/Service.cs
[sdk-twitch]: https://github.com/hardcpp/ChatPlexSDK/blob/9c209664b5a0c64f9c9c1e7e152b0ca6d1dab7ce/Chat/Services/Twitch/TwitchService.cs
[quest-loader]: https://github.com/raineaeternal/Quest-SongCore/blob/main/shared/SongLoader/RuntimeSongLoader.hpp
[twitch-eventsub]: https://dev.twitch.tv/docs/eventsub/handling-websocket-events/
[twitch-scopes]: https://dev.twitch.tv/docs/authentication/scopes/
[twitch-moderation]: https://dev.twitch.tv/docs/chat/moderation
