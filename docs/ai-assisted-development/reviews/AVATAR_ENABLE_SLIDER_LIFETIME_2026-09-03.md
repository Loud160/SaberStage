# Avatar-enable slider lifetime correction — September 3, 2026

## Evidence and scope

User authorized fixing the diagnosed avatar-enable crash and deploying the
result. Preserve all existing work. Do not change UI geometry, avatar behavior,
camera motion, shaders, credentials or any other mod/dependency.

Evidence: `diagnostics/avatar-enable-crash-20260903/FINDINGS.md`, support ZIP,
and `tombstone_01.txt`. At 04:22:40 EDT, Black Heart had loaded/bound successfully.
The subsequent settings rebuild faulted in BSML's `SliderSetting::TextForValue`
while the native Torso Width slider was being cloned, before its new wrapper
existed. A stale native-slider-to-wrapper registration supplied an invalid
formatter pointer. The old avatar UI is immediately destroyed/rebuilt without
explicit registration cleanup. Hidden controls may never receive OnDestroy.

## Implementation sequence

1. Introduce an owned-only registration erasure policy and one main-thread
   hierarchy cleanup helper. Include inactive numeric/list slider wrappers.
   Compare raw identities without dereferencing the registered wrapper. Never
   clear the shared registry or erase another owner's mapping. On an ownership
   mismatch or recoverable inspection failure, report it and stop that teardown.
2. Run cleanup before avatar panel destruction/rebuild, grip editor destruction,
   chat control content navigation, and chat/request surface teardown. Also
   release surviving main-menu registrations at controller shutdown. No scans
   during ordinary per-frame updates.
3. Add lifecycle logs with cleanup context, wrapper/erasure counts, mismatches,
   and avatar build/rebuild phases. Preventing stale state is the fix; catching
   SIGSEGV is not a recovery strategy.
4. Host-test owned-only removal, repeated cleanup, foreign-owner preservation,
   and simulated native-address reuse. Source checks must prove inactive
   descendants are included and cleanup precedes destruction/reconstruction.
5. Run host/tooling tests, ARM64 build, ELF and QMOD checks. Stop Beat Saber before
   receipt-guarded deployment; verify installed hashes. Leave game launch and
   in-headset interaction to the user, as in the preceding deployment.

## Required headset follow-up

Enable Black Heart from Setup without first visiting Fit, then repeat avatar
load/unload and profile changes. Logs should contain completed cleanup and
rebuild records instead of the formatter crash. Exercise grip editor close and
chat settings navigation too. Build/test success is not headset validation.

## Results

Implemented owned-only numeric/list slider cleanup in `src/ui/SliderLifetime.cpp`,
with the pointer-identity policy in `include/saberstage/ui/SliderRegistration.hpp`.
The hierarchy lookup uses Unity's non-generic Type overload so it does not depend
on AOT specializations for mod-created wrapper types. Both active and inactive
descendants are included. The avatar rebuild, grip editor, main-menu shutdown,
and chat/request teardown paths call the shared helper before destruction. No
menu dimensions, control positions, avatar settings, or dependencies changed.

Validation completed:

- All 15 host test suites passed, including the new slider-lifetime fixtures.
- All 64 tooling tests passed, including cleanup-before-destruction ordering.
- ARM64 build, private logger ELF boundary, and QMOD verification passed.
- `git diff --check` passed (only existing line-ending warnings).

Deployed to Quest 2 `1WMHH840QJ1046`, Beat Saber `1.40.8_7379`. Beat Saber was
force-stopped and verified stopped before replacement. The receipt-guarded
deployment verified all four installed library hashes. The game remains stopped
for the user to launch and test; avatar enable/rebuild behavior is not yet
verified on the headset.

- Mod library SHA-256:
  `21e0533bdb09a1a20d498cc52ce23312a847e3eb974ac6d8889c85e8f7cc3f13`
- QMOD SHA-256:
  `b83198780ea11a033a25e58abaee50998782d3af72be20a5840853395ac8766f`
- Build log: `diagnostics/avatar-enable-crash-20260903/build-fix.txt`
- Deployment log: `diagnostics/avatar-enable-crash-20260903/deploy-fix.txt`

The original crash proves a stale formatter registration was consulted, but does
not identify which destroyed wrapper originally owned that stale address. This
fix closes SaberStage's verified teardown gap without modifying BSML/Qounters
or clearing registrations belonging to other controls/mods. The headset follow-up
above remains necessary to confirm this resolves the reported reproduction.
