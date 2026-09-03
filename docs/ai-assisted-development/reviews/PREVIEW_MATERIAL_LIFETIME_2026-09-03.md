# Preview material lifetime correction — September 3, 2026

## Scope and evidence

Fix and deploy the map-exit crash documented in
`diagnostics/map-exit-crash-20260903/FINDINGS.md` and `tombstone_02.txt`.
The installed build used an invalid cached movable-preview material in
`SetPreviewTexture` after returning from GameCore and unloading unused assets.
The panel was off, but its material was still updated every frame.

The user has explicitly directed that future SaberStage-caused crash reports
include diagnosis, correction, validation, and redeployment without requiring
another request. Preserve unrelated work and leave other mods read-only.

## Implementation

1. Retain the floor, floating-feed, and floating-border material wrappers with
   SafePtrUnity. Create into the root before further Unity calls. Protect these
   cached standalone assets with DontUnloadUnusedAsset, rather than relying on
   DontDestroyOnLoad. Release/destroy them explicitly on preview shutdown.
2. Clear the material's video texture when its panel detaches/closes. Keep the
   small material caches rooted for reuse; do not retain an obsolete camera
   render target while a preview is off.
3. Bind only to existing active preview surfaces, and only when their feed
   reference changes. No material setter should run for an absent panel.
   Avoid repeatedly tearing down an already absent floating preview.
4. Log material creation/release and actual panel closure. Existing camera
   scene-transition logs supply scene context without another per-frame scan.
5. Add source regression checks for ownership, native-asset retention, gated
   binding, and texture clearing. Run all host/tooling tests, the Android build,
   and ELF/QMOD validation. Stop Beat Saber before receipt-guarded deployment
   and verify installed hashes. Leave the game closed for the user's test.

Do not change panel geometry, shaders, avatar behavior, recording settings,
render demand, quality choices, or the previous slider-lifetime fix.

## Headset follow-up

Test movable preview on/off and floor menu open/close, then enter/leave multiple
maps with the previews disabled. Reopen each preview after a map. Repeat with
the movable preview left enabled to cover retained materials during cleanup.
Automated tests cannot simulate Unity's actual managed/native asset reclamation;
on-headset confirmation is required.

## Results

Implemented in `src/preview/PreviewManager.cpp`:

- All three cached materials now use SafePtrUnity and DontUnloadUnusedAsset;
  the factory roots each wrapper before making further Unity calls.
- Shutdown releases each owned material explicitly, with lifecycle log entries.
- Closing either preview clears its material's feed reference. Texture rebinding
  now requires a live, active surface and a changed texture reference.
- An already closed movable preview no longer runs teardown/cache rebuilding
  every tick. Material ownership is fixed independently of this work reduction.
- Added a regression invariant in `tests/ToolingTests.py`; updated the existing
  material-assignment assertion for the explicit safe-pointer access.

Validation: 15/15 host suites passed; 65/65 tooling tests passed; diff whitespace
check passed; ARM64 build, private logger ELF boundary, and QMOD verification
passed. No shader, layout, settings, or avatar changes were made in this pass.

Deployed to Quest 2 `1WMHH840QJ1046`. Beat Saber was stopped and verified stopped
before the receipt-guarded update; all four installed library hashes matched.
The game remains closed for the user's headset test. Map-exit runtime behavior
has not yet been reverified on-device.

- Library SHA-256:
  `b2a70fe948fba62aca18d57622fa88683ddf11819dc381431102002ccccdfedb`
- QMOD SHA-256:
  `0c06b75e7ee3f9bbb59db3bf30d651111ace54d7c94def8411c8966319a21b49`
- Build log: `diagnostics/map-exit-crash-20260903/build-fix.txt`
- Deployment log: `diagnostics/map-exit-crash-20260903/deploy-fix.txt`
