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
- never allow provider failures to affect recording, streaming, or gameplay.

If Discord messages can legally/technically be consumed, allow them to appear in the same HMD chat panel through a `DiscordChatProvider`.

Provider-specific behavior must not leak into the panel implementation.

## Set-it-and-forget-it

Once Discord integration is configured:

- restore the selected behavior on launch;
- reconnect where appropriate;
- keep camera orientation correct;

Provide:

- Disconnect Discord
- Clear Discord credentials
- Reset Discord settings

Do not patch or modify Discord under any circumstances.
