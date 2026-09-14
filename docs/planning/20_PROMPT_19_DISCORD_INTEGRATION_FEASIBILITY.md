# Prompt 19 — Investigate and prototype supported local Discord integration on Quest

This phase is primarily a feasibility study.

The project wants useful integration with a **local Discord client running on the Quest**.

Potential goals:

1. Discord status/presence.
2. Discord text/message integration where officially supported.
3. Display selected Discord messages in the existing HMD chat panel where permitted.

Do not assume all three are possible.

## Research current capabilities

Verify current:

- Discord Android/Quest client behavior;
- Discord public SDK/API support;
- Discord Social SDK / Game SDK / Embedded SDK capabilities if relevant;
- approved presence, account-linking, and message APIs;
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

Stop for review before any production Discord feature work.
