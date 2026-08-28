# Prompt 20 — Production Discord integration, only for capabilities proven supported

Execute this prompt only after Prompt 19 has established what is actually supported through public/current mechanisms.

Do not implement any Discord capability that the feasibility study marked unsupported.

## Supported chat/status features

For capabilities proven supported:

- integrate them through the existing ChatService/DiscordIntegration boundaries;
- persist non-secret preferences;
- secure tokens/credentials;
- reconnect automatically where appropriate;
- provide clear disconnect/reset;
- never allow Discord failures to affect recording/streaming/gameplay.

If Discord messages can legally/technically be consumed, allow them to appear in the same HMD chat panel through a `DiscordChatProvider`.

Provider-specific behavior must not leak into the panel implementation.

## Discord video streaming

Only if a supported method exists for feeding the broadcast camera to a local Discord client:

- reuse the existing broadcast camera/compositor;
- preserve the selected scene and integrated-avatar visibility;
- reuse the capture timeline;
- reuse encoded media where compatible;
- avoid second full scene render;
- avoid CPU readback where possible;
- implement bounded buffering;
- isolate Discord failure;
- preserve local recording/direct streaming.

If Discord requires a materially different codec/resolution path:

- validate hardware capability;
- measure Quest 2 cost;
- do not silently start a second encoder;
- expose clear limitations.

## Set-it-and-forget-it

Once Discord integration is configured:

- restore the selected behavior on launch;
- reconnect where appropriate;
- keep camera/avatar orientation correct;
- require no repeated calibration.

Provide:

- Disconnect Discord
- Clear Discord credentials
- Reset Discord settings

Do not patch or modify Discord under any circumstances.
