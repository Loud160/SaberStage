# Chat and Discord integration boundary

`ChatProvider` exposes authenticate/connect/disconnect, bounded incoming messages, connection state, and optional send. `ChatService` sanitizes/rate-limits into a bounded model and marshals UI updates to the game thread. The movable HMD chat panel persists semantic placement, scale, opacity, font size, message limit, provider identity, and auto-hide choice; it is excluded from broadcast by default.

Twitch should prefer supported EventSub/API paths over legacy IRC where feasible; YouTube uses the live-chat API and its prescribed polling/streaming behavior. OAuth tokens are secrets, never logs. See [Twitch chat](https://dev.twitch.tv/docs/chat/) and [YouTube live chat](https://developers.google.com/youtube/v3/live/docs/liveChatMessages).

## Discord capability matrix

| Goal | Current supported/public conclusion |
|---|---|
| Presence/status | Feasible candidate through Discord Social SDK/Rich Presence, subject to SDK distribution and mod-host integration tests |
| Account linking/deep link | Public Android OAuth2/PKCE/deep-link flow exists; patched Beat Saber manifest/activity compatibility needs proof |
| Discord messages/chat | Social SDK exposes approved communication capabilities with production access/rate-limit requirements; only user-authorized, documented scopes are acceptable |
| Discord voice | Social SDK supports game-integrated voice subject to platform and production requirements; it is separate from livestream video |
| Launch Discord | Android intent/deep link may be offered when a compatible local client exists |
| Consume Discord data | Only via public SDK/API, OAuth scopes, and user consent |
| Feed SaberStage video and selected stream audio into the local Discord client's stream/screen share | Implemented through the separate `SaberStage Camera` Android activity and Android 14's user-selected single-app sharing flow; this is app-window sharing with capturable app audio, not a Discord SDK video-source API |
| Control the local Discord client's livestream | **Unsupported:** no public supported automation contract was found |

Android `MediaProjection` still does not register a synthetic camera device or
give SaberStage control of Discord. Instead, the separately installed helper
provides an ordinary resizable app window that the user explicitly chooses in
Discord's Android 14 app-sharing picker. SaberStage sends its already encoded
third-person video and existing game/microphone/TTS stream mix to that window
over authenticated loopback. The helper hardware-decodes video and publishes
the PCM mix through a capture-enabled Android `AudioTrack`. Discord continues
to own user consent, app selection, capture, transport, and stream controls.

SaberStage opens Discord first and the helper's source activity second. This
ordering keeps both Horizon OS tasks available when Discord builds its
single-app source list; reopening Discord from the application library after
the helper is visible may replace the helper's root task. Android package
visibility is not treated as authoritative because an installed sideloaded
helper can be hidden from `PackageManager`. Only an explicit helper launch that
returns `ActivityNotFoundException` is reported as a missing installation.

After the authenticated protocol handshake, the helper reports the exact AVC
decoder selected from Android's `MediaCodec` registry. SaberStage displays that
decoder name, acceleration classification, negotiated resolution, and frame
rate in its own Live Stream status area. Keeping the diagnostic out of the
helper window prevents it from appearing in Discord's captured picture.

The implementation does not modify or hook Discord, use private Discord APIs,
automate the client, require root, or impersonate an Android camera. Direct
Twitch/custom RTMP streaming remains independent from this optional path. See
[Discord screen source](DISCORD_SCREEN_SOURCE.md) for the lifecycle and test
boundary.

Meta's experimental Horizon OS Virtual Camera Publisher is recorded as a
future alternative, not part of the current implementation. See the
[deferred virtual-camera concept](planning/24_FUTURE_HORIZON_OS_VIRTUAL_CAMERA_PUBLISHER.md)
for the compatibility gates that must be proven before replacing the helper.

Sources: [Discord Social SDK](https://discord.com/developers/docs/social-sdk/index.html), [communication feature requirements](https://docs.discord.com/developers/discord-social-sdk/core-concepts/communication-features), [mobile account linking](https://docs.discord.com/developers/discord-social-sdk/development-guides/account-linking-on-mobile), and [OAuth2 policy](https://discord.com/developers/docs/topics/oauth2).
