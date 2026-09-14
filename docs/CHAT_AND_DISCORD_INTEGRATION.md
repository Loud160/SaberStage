# Chat and Discord integration boundary

`ChatProvider` exposes authenticate/connect/disconnect, bounded incoming messages, connection state, and optional send. `ChatService` sanitizes/rate-limits into a bounded model and marshals UI updates to the game thread. The movable HMD chat panel persists semantic placement, scale, opacity, font size, message limit, provider identity, and auto-hide choice; it is excluded from broadcast by default.

Twitch should prefer supported EventSub/API paths over legacy IRC where feasible; YouTube uses the live-chat API and its prescribed polling/streaming behavior. OAuth tokens are secrets, never logs. See [Twitch chat](https://dev.twitch.tv/docs/chat/) and [YouTube live chat](https://developers.google.com/youtube/v3/live/docs/liveChatMessages).

## Discord capability matrix

| Goal | Current supported/public conclusion |
|---|---|
| Presence/status | Feasible candidate through Discord Social SDK/Rich Presence, subject to SDK distribution and mod-host integration tests |
| Account linking/deep link | Public Android OAuth2/PKCE/deep-link flow exists; patched Beat Saber manifest/activity compatibility needs proof |
| Discord messages/chat | Social SDK exposes approved communication capabilities with production access/rate-limit requirements; only user-authorized, documented scopes are acceptable |
| Discord voice | Social SDK supports game-integrated voice subject to platform and production requirements |
| Launch Discord | Android intent/deep link may be offered when a compatible local client exists |
| Consume Discord data | Only via public SDK/API, OAuth scopes, and user consent |

The implementation does not modify or hook Discord, use private Discord APIs,
automate the client, require root, or impersonate an Android camera.

Sources: [Discord Social SDK](https://discord.com/developers/docs/social-sdk/index.html), [communication feature requirements](https://docs.discord.com/developers/discord-social-sdk/core-concepts/communication-features), [mobile account linking](https://docs.discord.com/developers/discord-social-sdk/development-guides/account-linking-on-mobile), and [OAuth2 policy](https://discord.com/developers/docs/topics/oauth2).
