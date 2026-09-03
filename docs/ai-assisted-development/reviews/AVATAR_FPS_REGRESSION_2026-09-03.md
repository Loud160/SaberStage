# FPS regression investigation — September 3, 2026

## Report and comparison boundary

After installing library SHA-256
`e671c8d3d14f4d836f195da11e183781ac89c7e23d2df78f5bf5750788e55572`,
the user reports upper-40s/low-50s FPS with avatar springs and outlines off,
versus 71.5–71.8 FPS before the changes. Medium springs previously sustained
low/mid-60s. The user confirmed both tests streamed with identical stream
settings. Do not dismiss this as a different streaming workload.

The deployed library includes both the avatar performance/lifetime changes
and the previously pending rich-chat work. Investigate both; do not assume
the spring math is the only possible source of a regression.

## Captured evidence

- Private archive: `diagnostics/avatar-fps-regression/SaberStage-Support-Logs-20260903-021035.zip`.
- Extracted files: `diagnostics/avatar-fps-regression/session-021035/`.
- BigScreen benchmark history copied read-only to
  `diagnostics/avatar-fps-regression/bigscreen-performance-history.log`.
  BigScreen source and device settings remain unchanged.
- Same map, Rhythm Of Love V8C, recent benchmark results include 46.2 FPS
  at 02:04:31, 49.9 at 02:05:57, and 50.8 at 02:07:23. The history does not
  record SaberStage quality settings; correlate with the native log.
  Earlier entries include 71.3 at September 2 23:54:03, 64.6 at 23:55:57,
  and 71.2 at 23:59:50, independently supporting the reported before/after
  difference (without assuming which individual quality switch was set).
- At 02:05:08, SaberStage reports 147 solves over 5.01 seconds, last native
  solve 36.2 microseconds, pose write 67.6 microseconds, zero spring work,
  zero collision tests. Later off-state samples agree. This rules out
  springs continuing to simulate after the off switch in that interval.
- Material updates report outlines=0 and outlinedRenderers=0. The saved
  settings also disable springs, collisions, expressions, and outlines.
- Chat is enabled. Chat diagnostics show one entry, 50 pooled rows, no
  overflow, and no ongoing content-size rewrites. Rendering/layout work
  outside the explicit native timers is not included in avatar solve times.

## Confirmed source defect

`MenuController::TickChatWorldPanel` assigns the first pooled text row's
sprite asset every Unity frame, including `nullptr` before any emote loads.
This line was introduced with rich chat. The first row is also the active
measurement/display row, not an unused detached probe.

Unity's TMP `spriteAsset` setter unconditionally marks geometry and layout
dirty, without an equality early-out. Consequently idle chat requests text
and canvas rebuild work at headset rate. See the upstream implementation:
[Unity TMP_Text.cs](https://github.com/Unity-Technologies/uGUI/blob/main/com.unity.ugui/Runtime/TMP/TMP_Text.cs).
This confirms unnecessary work, not the exact number of milliseconds it
contributes on this Quest/game build.

## Narrow correction and verification plan

1. Remove sprite-asset assignment from the per-frame chat tick.
2. Bind the asset only during message reflow/row reuse, before preferred-size
   measurement. Guard the setter with a pointer comparison, including null.
   Preserve atlas-revision invalidation, animations, scroll geometry, and UI
   layout. Do not add another dirty/rebuild path.
3. Add a regression assertion preventing the setter/binder returning to the
   per-frame tick and requiring guarded binding before measurement.
4. Build/test without deploying over the user's running test. Preserve the
   prior binary locally for comparison.
5. Ask for chat-off versus chat-on on the installed build. This isolates the
   confirmed idle UI defect without changing the avatar or streaming settings.
   A successful build is not evidence that this recovers the entire FPS loss.

No scheduling rollback, renderer merging, shader changes, or speculative
changes to GC ownership are part of this correction. If the controlled test
does not recover FPS, continue with engine skinning/render timing or a
single-variable scheduling comparison; do not claim a root cause prematurely.

## Correction status

- Implemented guarded `RichChatRenderer::BindSpriteAsset`, called only during
  row reuse and reflow before preferred-size measurement. Removed the idle
  tick's unconditional setter. No UI dimensions/scroll behavior changed.
- New repository invariant test failed against the deployed source and passes
  with the correction. The complete tooling suite passes 57/57 and existing
  native host suite passes 14/14. These tests are not an FPS measurement.
- Original deployed library/package are retained in
  `diagnostics/avatar-fps-regression/deployed-baseline/` for comparison.
- Quest ARM64 build, private-logger boundary, QMOD verification, and diff checks
  passed. No corrected build deployed in this pass.
  Corrected library SHA-256:
  `e8527c37427218f36c5554f5ed54c6fb518cf1593db7d60083bda7ba74195ea7`.
  Corrected QMOD SHA-256:
  `9b062b58ac5b29c11b61dcf30015d4e368adf1ac4912bdf7a4bc5ab3989f1104`.
- The user confirmed that hiding chat restores over 70 FPS on the installed
  build. This isolates the regression to the chat-enabled path; the corrected
  build still needs the same benchmark with chat enabled to verify recovery.

## Follow-up: Chat Control surface covers its own controls

The user reports a blackish-brown Chat Control panel with invisible but
clickable controls, including an invisible Close button. Source inspection
found a full-size background using `SaberStage/NonBloomUI` (queue 3020), while
the working chat panel uses BSML's normal `UINoGlow`/`Custom/CustomUI` material
(queue 3000, recorded by `ChatPanelDiag`). An opaque later-queue pass can
paint over the earlier UI despite correct sibling order. Its raycast flag is
false, explaining why underlying controls can still be clicked. The surface
factory already disables FloatingScreen's optional extra background; there
is no need to add another canvas or counteracting overlay.

Correction scope:

- Keep the full-size backdrop on the same stock image/material path used by
  the working chat panel; make its first-sibling placement explicit.
- Preserve existing positions, sizes, text, Close actions, and grab collider.
- Apply the shared fix to Chat Controls and Song Requests. Keep the cover
  image's non-bloom material separate from the full-panel backdrop.
- Log background/control material queues, hierarchy order, and Close-button
  bounds on page creation only, without per-frame scans or chat text logging.
- Add a regression invariant, rebuild/test, and leave deployment to explicit
  user authorization. Headset visual verification remains required.

Implemented the shared background correction and creation-only
`ChatSurfaceLayers` logging, including the Close-button/label material queues.
The new invariant failed against the old background assignment and passes with
the correction. The full tooling suite now passes 58/58; native host tests pass
14/14. No avatar, main-menu, chat viewport, scrollbar, or shader logic changes
were needed for this follow-up.

Next headset checks, after an authorized installation:

1. With chat enabled, open Chat Control. Verify captions, switches, sliders,
   navigation, and Close are visible against the backdrop. Visit Appearance,
   Requests, and Moderation; close/reopen without losing the saved placement.
2. Open Request manager and check its buttons, Close, and cover artwork.
   Inspect `ChatSurfaceLayers` records for the actual background and control
   queues/order if any part remains hidden. Diagnostics do not log chat content.
3. Repeat the identical streaming benchmark with chat enabled, springs and
   outlines off, then with chat hidden. Recovery with chat enabled is still
   unverified; do not conflate a successful build with an FPS measurement.

Combined idle-chat/layering build: ARM64 compile, private-logger ELF check,
QMOD verification, and diff checks passed. No deployment or game restart was
performed. Library SHA-256:
`fced552165210fc3bcd3f94164570425eaada43cf16f0bf8191840277a6cb282`.
QMOD SHA-256:
`17b0043c6057ee5f0e2e1c1199bc63044ea7aa3bd8f771525e332b0d57ac0eaa`.
