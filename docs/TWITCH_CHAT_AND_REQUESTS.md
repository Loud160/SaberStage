<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors -->
<!-- Part of SaberStage. See LICENSE and LICENSE-ADDITIONAL-TERMS.md. -->

# Twitch chat and song requests

This is a development implementation for standalone Quest and Beat Saber
1.40.8. It needs no OBS, PC helper, or relay. On-headset appearance, controller
interaction, live provider behavior and performance remain acceptance tests;
passing a build does not establish those results.

## Starting from the headset

1. Connect Twitch through the existing Live Stream tab and open the chat panel.
2. Select **Chat Control** on that panel. This opens a separate movable menu;
   chat remains visible. No new rich-chat settings are added to the Live tab.
3. **Chat settings** controls dimensions, font, order, platform accent, colors,
   badges, emotes, viewer count, command filters and notices. Static emotes are
   the default. Animated emotes are optional and deliberately bounded.
4. In **Request settings > General**, enable requests and choose queue limits.
   **Commands** changes each supported command's permission. Clicking the
   permission cycles Everyone, Subs/VIPs, Moderators, Broadcaster and Disabled.
   **Cooldown** controls BSR and queue-query cooldowns separately, per viewer or
   shared across the channel.
5. Open **Request manager** and select **Open queue**. Enabling requests alone
   does not silently reopen intake after a restart.

Request manager is independent of the chat/settings panel. Its background can
be grabbed, its size can be adjusted with **Panel size**, and **Reset panel**
restores placement/size. Closing a window does not close intake or delete data.
Turning requests off stops intake; existing queue/history stay saved.

## Rich chat, notices and moderation

- Twitch name colors, badges, inline Twitch/7TV/BTTV/FFZ emotes, action messages,
  mention highlighting and the purple platform-origin accent are supported.
  The accent is not the scrollbar. The existing native scrollbar and
  controller-hover scrolling are retained.
- Subscription/gift notices and Bits messages come from IRC. Optional follows
  and channel-point redemptions use a separate EventSub connection, with bounded
  retries and duplicate-event suppression. Missing optional permissions do not
  disable ordinary chat or request intake.
- **Moderation** has message sending and shortcuts in the main pane, and recent
  active users plus target actions on the right. Select a target, choose a
  ten-minute timeout, ban, or deletion of that user's selected/latest retained
  message, then confirm. An account switch invalidates the old target.
  Actions are only reported successful after Twitch accepts them.
- Existing connected accounts must authorize again for moderation/follow/
  redemption permissions. Newly requested scopes are `moderator:manage:chat_messages`,
  `moderator:manage:banned_users`, `moderator:read:followers` and
  `channel:read:redemptions`; existing chat-send/title scopes remain in use.
  Tokens continue to use the existing Android Keystore storage.
- **Retry connections** resets chat/image/notice retries without stopping the
  media stream or clearing the queue. Provider errors are logged without tokens,
  authorization headers, signed reconnect URLs, or complete chat transcripts.

## Commands and defaults

| Command | Action | Default permission |
| --- | --- | --- |
| `!bsr <key or https://beatsaver.com/maps/key>` | Validate and queue a map | Everyone |
| `!bsrhelp` | Request instructions | Everyone |
| `!link` | Current map's BeatSaver link, when available | Everyone |
| `!queue` | Pending count and first four requests | Everyone |
| `!queuestatus` | Intake status and pending count | Everyone |
| `!wrong` / `!oops` / `!wrongsong` | Remove your last non-playing request | Everyone |
| `!open` / `!close` | Open/close intake | Moderators/broadcaster |

Initial configurable defaults: 25 pending total, two per viewer, one extra for
VIPs and one extra for subscribers, 30-second BSR cooldown, 10-second queue-query
cooldown, 20-minute duration limit and 100 history entries. Pending capacity can
be set from 1–200; history from 1–500. Lowering a limit never discards an already
accepted pending item. A separate two-second per-user anti-flood ceiling bounds
incoming commands; authorized Close bypasses it. Replies and existing map
announcements share a sender limiter of one message every two seconds.

Queue/history duplicate checks, role restrictions, duration and compatibility
checks happen before acceptance. Allowlisting bypasses duration/compatibility
policy, not a missing gameplay extension. Blocklisting prevents new viewer
requests. No arbitrary URL downloads or title-based web searches are accepted.

## Selecting and playing a request

Use **Queue / History / Allowlist / Blocklist**, select a map, then use the
adjacent contextual **Download** or **Play** action. The selection shows a small
cover thumbnail, metadata and per-difficulty hints. There are no per-row Play
buttons. **Skip**, **Add to queue**, list membership and move-to-top act on stable
request/map IDs, not the current recycled row number.

Downloads require an explicit streamer action. A worker downloads, extracts,
checks paths/CRC/content hash and atomically publishes the map; status includes
download bytes/percentage and cancellation. **Play opens native difficulty
selection and never automatically starts gameplay.** After a download finishes,
SongCore refreshes asynchronously; select Play when ready. Downloads started in
the menu are cancelled if gameplay begins before installation completes, and
library refresh/navigation wait for a valid menu state.

Quest SongCore **1.1.26** is a required dependency for this target version. A
different SongCore or game version is not implied compatible. A map requiring
unavailable Noodle/Vivify or other extensions does not become playable merely
because its ZIP downloaded. The game/loader remains responsible for difficulty
availability. Download/selection and actual gameplay attempts are separate:
start, completed, failed and quit events update request history honestly.

## Recovery, privacy and resource limits

Queues are partitioned by Twitch channel ID in `ChatRequests` beside
`settings.json`. Two alternating checksummed snapshots preserve a previous
known-good version; files and directories are flushed before acceptance is
acknowledged. A failed save closes intake and does not send an acceptance reply.
Restart restores pending/order and marks a formerly playing item Interrupted;
it never silently reopens intake. Both-invalid snapshots fail closed.

Saved request data includes Twitch user/message IDs, requester display names
and map metadata. It does not include OAuth tokens or unrestricted chat history.
Map partials live in `.saberstage-map-downloads` beside SongCore's song directory,
outside its scanner. Retry/cancellation only cleans the owned hash-specific
partial; installed/user maps are never replaced or deleted as a repair.

| Resource | Bound |
| --- | --- |
| Retained chat / live row pool | 128 messages / 50 rows |
| Main chat maximum size | 240 × 200 canvas units |
| Request/moderation row pool | Eight recycled buttons per list |
| Rich image atlas | One 1024² RGBA texture, 4 MiB, 256 slots |
| Image source / dimensions | 2 MiB / maximum 1024² |
| Image work / completed handoff | 32 pending / eight completed |
| Animated image | Up to 16 sampled frames at 10 FPS; long/unsupported loops use static fallback |
| Image uploads | At most one completed rich asset per quarter-second |
| Request cover | Separate 64² texture, one current selection, no emote catalog |
| ZIP / expanded map | 64 MiB / 128 MiB, maximum 512 entries |
| Download timeout | 180 seconds, with cancellation and progress |

Decoding/network/storage never run in Unity callbacks. Optional image work
stops when unused. There is no disk emote cache. At capacity, unavailable assets
retain readable text; native stencil/rect clipping and multiview rendering use
the separate embedded `SaberStage/ChatSprite` shader. These bounds are not a
claim of measured Quest 2 frame-time overhead.

## Reference and remaining acceptance

The UI/workflow reference is [BeatSaberPlus at the pinned revision](https://github.com/hardcpp/BeatSaberPlus/tree/632addd001d29a7beee14a52eadf87ccb2792a3a),
with the details and approved exclusions in the
[implementation plan](planning/TWITCH_RICH_CHAT_AND_SONG_REQUESTS.md).
This implementation preserves its logical left/main/right composition,
appearance columns, General/Commands/Cooldown, selected-map manager actions,
and moderation separation. It is **not yet verified full visual parity**.

Actual standalone adaptations: one flat host per multi-pane surface; saved
world placement and uniform request-panel scaling; RGB native color pickers;
bounded static-first images and small cover thumbnails; permission cycling for
the eight supported command groups; fixed request-help/queue shortcuts instead
of a PC web-configuration editor. The right settings pane uses existing Quest
grab/resize behavior rather than adding PC environment-follow/lock controls.
Layout dimensions are source-derived, not validated against PC/Quest captures.

Emote rain, general integration rules, polls/predictions, custom rule/shortcut
editors, new YouTube/Kick support and gameplay effects are not included. Native
headset tests must still cover panel layout, scroll/grab/resize, image masking,
live moderation/notices, loader selection, force-close recovery and performance.
See the [verification record](ai-assisted-development/reviews/TWITCH_CHAT_IMPLEMENTATION.md).
