# Security policy

## Supported version

Security corrections currently target the newest SaberStage development build
for the documented Beat Saber Quest package version. Older game/mod
combinations may not receive backports.

## Reporting

When the repository is public, use GitHub private vulnerability reporting if
it is enabled. Otherwise contact the maintainer privately before publishing an
exploitable native-memory, path, archive, credential, OAuth, streaming, or
deployment-ownership issue.

Include the SaberStage build number, Beat Saber version, Quest model,
reproduction steps, and the smallest relevant excerpt from
`saberstage-native.log`. Do not attach stream keys, OAuth tokens, private
avatars, recordings, complete settings files, or unrelated headset logs.

## Relevant boundaries

Especially useful reports include native memory corruption, cross-thread Unity
access, unsafe worker shutdown, path traversal, unbounded media/network input,
secret disclosure, TLS verification bypass, source-deployment ownership
bypass, or a malformed VRM/movement script/settings document escaping its
documented limits.
