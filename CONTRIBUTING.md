# Contributing to SaberStage

SaberStage is distributed outbound under **GPL-3.0-only**, with additional
terms and an interoperability permission under GPLv3 section 7. See
[LICENSE](LICENSE) and
[LICENSE-ADDITIONAL-TERMS.md](LICENSE-ADDITIONAL-TERMS.md).

## Inbound MIT license grant

By intentionally submitting a contribution to SaberStage, you license that
contribution to **Loud160 (AKA Whisp)**, the SaberStage project, and its
maintainer under the [MIT License for inbound contributions](INBOUND_LICENSE.md)
in addition to any license applicable to the distributed SaberStage project.

This separate inbound MIT grant permits the maintainer to use, copy, modify,
merge, publish, distribute, sublicense, relicense, dual-license, sell, grant
exceptions for, and otherwise exercise the rights granted by the MIT License
over the submitted contribution. SaberStage's outbound GPL-3.0-only plus
section 7 licensing does not restrict the maintainer's separate rights received
from contributors under this inbound MIT grant. When exercising that separate
inbound MIT license, the maintainer is not required to apply SaberStage's GPLv3
section 7 attribution requirements to the maintainer's independent use of the
contributor material.

This is a license grant, not a copyright assignment. Contributors retain any
copyright ownership they otherwise hold. By opening or submitting a pull
request, the contributor acknowledges these inbound contribution terms.

## Developer Certificate of Origin 1.1

Every contribution must also be certified under the
[Developer Certificate of Origin 1.1](DCO.txt). Sign each commit with:

```text
git commit -s
```

which adds:

```text
Signed-off-by: Contributor Name <email@example.com>
```

The DCO sign-off certifies provenance and the contributor's right to submit the
change. It does **not** create or grant the inbound MIT license. The inbound MIT
grant above is a separate condition of intentional submission.

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
  movement scripts or other user-owned files.
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
