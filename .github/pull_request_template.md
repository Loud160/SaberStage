## Summary

Describe what changed, why it is needed, and the intended behavior that remains
unchanged.

## Contribution certification

- [ ] Every commit is signed off with `Signed-off-by:` under DCO 1.1.
- [ ] I have read `CONTRIBUTING.md` and intentionally submit this contribution
      under its separate inbound MIT grant.
- [ ] I understand that SaberStage is distributed outbound under
      GPL-3.0-only with the additional GPLv3 section 7 terms.

## Validation

- [ ] Host C++ tests passed.
- [ ] Repository/tooling invariant tests passed.
- [ ] Quest ARM64 build passed.
- [ ] QMOD package validation passed.
- [ ] On-device validation is described below, including anything not tested.
- [ ] Documentation and user-facing error text were updated where needed.
- [ ] No credentials, private logs, copyrighted media, generated build output,
      or device backups are included.

## Threading, lifetime, and storage

Explain any worker, Unity-object, settings, file, network, or teardown impact.
State why the chosen ownership and recovery boundaries are safe.

## On-device result

List headset model, Beat Saber build, relevant mod stack, QMOD hash, test steps,
and observed result—or state explicitly that the change is build-only.
