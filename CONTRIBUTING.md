# Contributing to SaberStage

SaberStage is still in early development and no final project license or
inbound contribution terms have been selected. Discuss a proposed contribution
with the maintainer before submitting code; opening a pull request does not by
itself create a license grant beyond rights you already hold.

## Engineering expectations

- Keep changes narrow and preserve the documented Beat Saber target.
- Explain why lifetime, thread, ownership, recovery, and platform-specific
  behavior is necessary. Avoid comments that merely repeat the syntax.
- Keep Unity, IL2CPP, and BSML object access on the game thread. Workers may
  produce owned data and diagnostics, but must hand Unity work back to the main
  thread through an explicit boundary.
- Use bounded queues and joinable owned workers. Never make gameplay wait for
  file, network, codec-drain, or mux work.
- Preserve atomic settings and media promotion. Do not delete user recordings,
  avatars, movement scripts, or other user-owned files.
- Do not weaken source-deployment receipts, QMOD validation, secret redaction,
  TLS verification, archive validation, or native dependency checks.

## Validation

Before proposing a change:

1. Run `qpm scripts host-test`.
2. Build the Quest ARM64 library and QMOD through the documented scripts.
3. Keep the Native Logger Quest and FFmpeg inputs pinned and hash-verified.
4. Update relevant documentation and test invariants.
5. State clearly what was tested on a Quest and what remains build-only.

Do not commit generated game headers, build/cache output, QMODs, copyrighted
media, Beat Saber files, credentials, stream keys, OAuth tokens, private
headset logs, device backups, or diagnostic recordings.

See [Build and deploy](docs/BUILD_AND_DEPLOY.md),
[Lifetime and threading](docs/LIFETIME_AND_THREADING.md), and
[Test plan](docs/TEST_PLAN.md).
