# Prompt 18 — Add live stream chat to an in-game HMD panel

Implement a stream-chat system that allows the player to read live chat while playing.

Initial providers:

- Twitch;
- YouTube Live.

Design the provider boundary so other chat systems can be added later.

## Architecture

```text
ChatProvider
  ↓
ChatService
  ↓
bounded thread-safe message queue/model
  ↓
game-thread UI adapter
  ↓
HMD Chat Panel
```

Network/provider callbacks must not manipulate Unity objects directly.

## Chat panel

Support:

- configurable position;
- rotation;
- scale;
- opacity;
- font size;
- number of visible messages;
- player-relative/world-relative placement;
- show/hide;
- configurable controller shortcut;
- optional auto-hide in menus/non-gameplay;
- HMD-only by default;
- optional future broadcast-overlay use through a separate compositor source.
- placement and visual language consistent with SaberStage's movable preview and persistent panel controls.

## Behavior

- bounded history;
- reconnect;
- rate limiting;
- avoid high allocation churn;
- sanitize display;
- handle deleted/removed messages where provider API supports it;
- handle channel changes cleanly.

## Authentication

Treat OAuth/API tokens as secrets.

Never log tokens.

Use appropriate secure storage.

## Set-it-and-forget-it

After initial authentication/configuration:

- reconnect automatically when appropriate;
- restore panel position/settings;
- restore selected channel/account;
- require no manual panel placement each launch.

Add:

- Reconnect Chat
- Reset Chat Panel
- Clear Chat Credentials

Test chat during heavy gameplay and verify it does not affect Unity frame timing materially.
