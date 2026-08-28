# Prompt 19 — Investigate and prototype supported local Discord integration on Quest

This phase is primarily a feasibility study.

The project wants useful integration with a **local Discord client running on the Quest**.

Potential goals:

1. Discord status/presence.
2. Discord text/message integration where officially supported.
3. Display selected Discord messages in the existing HMD chat panel where permitted.
4. Use the selected SaberStage camera/scene, including the integrated avatar when enabled, as the video source for Discord livestreaming **if a supported/current mechanism exists**.

Do not assume all four are possible.

## Research current capabilities

Verify current:

- Discord Android/Quest client behavior;
- Discord public SDK/API support;
- Discord Social SDK / Game SDK / Embedded SDK capabilities if relevant;
- voice/video/screen-share APIs;
- Android/Horizon OS MediaProjection;
- virtual displays;
- camera APIs;
- inter-process media sharing;
- whether an ordinary third-party app/mod can expose a synthetic camera/video source to Discord;
- Quest OS restrictions;
- Discord developer policies/terms relevant to client automation.

## Prohibited approaches

Do not:

- modify/patch/repackage Discord;
- hook private Discord internals;
- use self-bot techniques;
- automate a user account via private user token;
- require root;
- depend on undocumented private APIs.

## Deliverable

Create/update:

```text
docs/CHAT_AND_DISCORD_INTEGRATION.md
```

Include a matrix:

```text
Feature
Supported with public API?
Requires local Discord client?
Requires desktop companion?
Performance implications
Security/privacy implications
Recommendation
```

## Prototype only supported paths

If presence/status or chat integration is supported safely, build a minimal proof of concept.

If third-person video injection into Discord is supported:

```text
Broadcast compositor
→ existing encoded/raw media path
→ Discord-compatible bridge
→ local Discord client
```

Reuse the existing camera/compositor.

Do not substitute the HMD mirror when the requested source is the third-person camera or composed broadcast view.

Do not render another full scene camera.

If Discord requires a different video format/path, quantify cost before implementation.

If video injection is not supported:

- document that clearly;
- do not fake completion;
- propose safe alternatives such as desktop companion → Discord/OBS or another standards-based path;
- preserve the core architecture so a future supported API could be added without redesign.

Stop for review before any production Discord feature work.
